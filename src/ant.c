#include "ant.h"

#include <stdlib.h>
#include <string.h>

/* Ant execution is intentionally split into two kinds of shared state:
 *   - tape colors are relaxed last-writer-wins memory;
 *   - occupancy[] enforces one resident read-head per cell and resolves contact.
 * A worker keeps position/heading/state local for a quantum and publishes the
 * cold snapshot once the burst ends. */

static const int DX[4] = {0, 1, 0, -1};
static const int DY[4] = {-1, 0, 1, 0};

static inline uint32_t pack_position(uint32_t x, uint32_t y)
{
    return (y << 16) | x;
}

static inline uint32_t packed_x(uint32_t p)
{
    return p & 0xffffu;
}

static inline uint32_t packed_y(uint32_t p)
{
    return p >> 16;
}

static inline size_t ant_index_of(const AntColony *colony, const Ant *ant)
{
    return (size_t)(ant - colony->ants);
}

static inline uint8_t ant_owner_id(size_t index)
{
    return (uint8_t)(index + 1u);
}

static inline size_t occupancy_index(const AntColony *colony, uint32_t x, uint32_t y)
{
    return (size_t)y * (size_t)colony->occupancy_width + x;
}

/* Each ant owns a private Galois LFSR stream, avoiding contention on the
 * universe RNG during mutation and lifecycle operations. */
static uint32_t ant_rng_uniform(uint32_t *state, uint32_t limit)
{
    if (limit <= 1u) return 0u;
    *state = lfsr32_advance(*state);
    return *state % limit;
}

/* Scheduler parameters are part of the ant's mutable phenotype: rate controls
 * long-term instruction supply, capacity controls burst size, and weight
 * controls relative WFQ service. */
static uint32_t random_rate(uint32_t *state)
{
    return 50000u + ant_rng_uniform(state, 1950001u);
}

static uint32_t random_capacity_fp(uint32_t *state)
{
    const uint32_t whole = 128u + ant_rng_uniform(state, 3969u);
    return whole << TOKEN_FP_SHIFT;
}

static uint32_t random_weight(uint32_t *state)
{
    return 1u + ant_rng_uniform(state, 16u);
}

static void reset_schedule(Ant *ant, uint32_t *state)
{
    atomic_store_explicit(&ant->token_rate, random_rate(state), memory_order_relaxed);
    const uint32_t cap = random_capacity_fp(state);
    atomic_store_explicit(&ant->token_capacity_fp, cap, memory_order_relaxed);
    atomic_store_explicit(&ant->tokens_fp, cap, memory_order_relaxed);
    atomic_store_explicit(&ant->weight, random_weight(state), memory_order_relaxed);
}

/* Convert relative/absolute rule turns into the four internal compass headings. */
static uint8_t apply_turn(uint8_t heading, TurnCode turn)
{
    switch (turn) {
        case TURN_L: return (heading + 3u) & 3u;
        case TURN_R: return (heading + 1u) & 3u;
        case TURN_B: return (heading + 2u) & 3u;
        case TURN_N: return 0u;
        case TURN_E: return 1u;
        case TURN_S: return 2u;
        case TURN_W: return 3u;
        case TURN_H: return heading;
        default: return heading;
    }
}

static inline uint32_t flags_load(const Ant *ant)
{
    return atomic_load_explicit(&ant->flags, memory_order_acquire);
}

static inline void publish_position(AntColony *colony, size_t index, uint32_t x, uint32_t y)
{
    atomic_store_explicit(&colony->positions[index], pack_position(x, y), memory_order_relaxed);
}

/* Initial placement probes random cells until it can atomically claim an empty
 * occupancy byte. Normal-sized worlds make exhaustion effectively impossible. */
static bool claim_random_empty_position(AntColony *colony, const World *world,
                                        size_t ant_index, uint32_t *rng_state,
                                        uint32_t *out_x, uint32_t *out_y)
{
    const uint8_t self_id = ant_owner_id(ant_index);
    for (size_t attempt = 0; attempt < world->cells; ++attempt) {
        const uint32_t x = ant_rng_uniform(rng_state, (uint32_t)world->width);
        const uint32_t y = ant_rng_uniform(rng_state, (uint32_t)world->height);
        const size_t cell = occupancy_index(colony, x, y);
        uint8_t expected = 0;
        if (atomic_compare_exchange_strong_explicit(&colony->occupancy[cell], &expected, self_id,
                                                    memory_order_acq_rel,
                                                    memory_order_relaxed)) {
            *out_x = x;
            *out_y = y;
            return true;
        }
    }
    return false;
}

int ant_colony_init(AntColony *colony, const World *world)
{
    if (!colony || !world || world->width <= 0 || world->height <= 0) return -1;
    memset(colony, 0, sizeof(*colony));
    colony->occupancy_width = (uint32_t)world->width;
    colony->occupancy_cells = world->cells;
    colony->occupancy = calloc(colony->occupancy_cells, sizeof(*colony->occupancy));
    if (!colony->occupancy) return -1;

    atomic_init(&colony->enabled_mask, 0);
    atomic_init(&colony->active_population, 0);
    atomic_init(&colony->collisions, 0);
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        atomic_init(&colony->positions[i], 0);
        atomic_init(&colony->stats[i].instructions, 0);
        atomic_init(&colony->stats[i].mutations, 0);
    }
    return 0;
}

void ant_colony_reset(AntColony *colony)
{
    if (!colony) return;
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        memset(&colony->ants[i], 0, sizeof(colony->ants[i]));
        atomic_store_explicit(&colony->positions[i], 0, memory_order_relaxed);
        atomic_store_explicit(&colony->stats[i].instructions, 0, memory_order_relaxed);
        atomic_store_explicit(&colony->stats[i].mutations, 0, memory_order_relaxed);
    }
    if (colony->occupancy) {
        for (size_t i = 0; i < colony->occupancy_cells; ++i)
            atomic_store_explicit(&colony->occupancy[i], 0, memory_order_relaxed);
    }
    atomic_store_explicit(&colony->enabled_mask, 0, memory_order_relaxed);
    atomic_store_explicit(&colony->active_population, 0, memory_order_relaxed);
    atomic_store_explicit(&colony->collisions, 0, memory_order_relaxed);
}

void ant_colony_destroy(AntColony *colony)
{
    if (!colony) return;
    free(colony->occupancy);
    colony->occupancy = NULL;
    colony->occupancy_cells = 0;
    colony->occupancy_width = 0;
}

/* A fresh ant receives independent behavioral and scheduling state, then claims
 * a random empty read-head location before becoming enabled. */
void ant_randomize(Ant *ant, AntColony *colony, const World *world, Lfsr32 *rng, const TurmiteRule *rule)
{
    if (!ant || !colony || !world || !rng || !rule) return;
    const size_t index = ant_index_of(colony, ant);
    uint32_t seed = rng_next(rng);
    if (seed == 0) seed = (uint32_t)(index + 1u);
    ant->rng_state = seed;
    ant->rule = rule;
    atomic_store_explicit(&ant->rule_index, (uint16_t)rules_index_of(rule), memory_order_relaxed);
    atomic_store_explicit(&ant->heading, (uint8_t)ant_rng_uniform(&ant->rng_state, 4u), memory_order_relaxed);
    atomic_store_explicit(&ant->state, (uint8_t)ant_rng_uniform(&ant->rng_state, rule->states), memory_order_relaxed);
    reset_schedule(ant, &ant->rng_state);

    uint32_t x = 0, y = 0;
    if (!claim_random_empty_position(colony, world, index, &ant->rng_state, &x, &y)) return;
    publish_position(colony, index, x, y);
    atomic_store_explicit(&ant->flags, ANT_F_ENABLED, memory_order_release);
    atomic_fetch_or_explicit(&colony->enabled_mask, UINT32_C(1) << index, memory_order_release);
}

/* Population doubling copies the source ant's phenotype but starts the clone
 * at a different location with a fresh full token bucket and RNG stream. */
void ant_clone(Ant *dst, AntColony *colony, const Ant *src, const World *world, Lfsr32 *rng)
{
    if (!dst || !src || !colony || !world || !rng) return;
    const size_t index = ant_index_of(colony, dst);
    dst->rng_state = rng_next(rng);
    if (dst->rng_state == 0) dst->rng_state = (uint32_t)(index + 1u);
    if (ant_rule_index(src) == RULE_INDEX_RUNTIME) {
        colony->runtime_rules[index] = *src->rule;
        dst->rule = &colony->runtime_rules[index];
    } else {
        dst->rule = src->rule;
    }
    atomic_store_explicit(&dst->rule_index, atomic_load_explicit(&src->rule_index, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->heading, atomic_load_explicit(&src->heading, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->state, atomic_load_explicit(&src->state, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->token_rate, atomic_load_explicit(&src->token_rate, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->token_capacity_fp, atomic_load_explicit(&src->token_capacity_fp, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->tokens_fp, atomic_load_explicit(&dst->token_capacity_fp, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->weight, atomic_load_explicit(&src->weight, memory_order_relaxed), memory_order_relaxed);

    uint32_t x = 0, y = 0;
    if (!claim_random_empty_position(colony, world, index, &dst->rng_state, &x, &y)) return;
    publish_position(colony, index, x, y);
    atomic_store_explicit(&dst->flags, ANT_F_ENABLED, memory_order_release);
    atomic_fetch_or_explicit(&colony->enabled_mask, UINT32_C(1) << index, memory_order_release);
}

uint16_t ant_rule_index(const Ant *ant)
{
    return ant ? atomic_load_explicit(&ant->rule_index, memory_order_relaxed) : 0;
}

uint32_t ant_packed_position(const AntColony *colony, size_t ant_index)
{
    if (!colony || ant_index >= TURMITE_MAX_ANTS) return 0;
    return atomic_load_explicit(&colony->positions[ant_index], memory_order_relaxed);
}

uint32_t ant_position_x(const AntColony *colony, size_t ant_index)
{
    return packed_x(ant_packed_position(colony, ant_index));
}

uint32_t ant_position_y(const AntColony *colony, size_t ant_index)
{
    return packed_y(ant_packed_position(colony, ant_index));
}

/* Scheduler owns these operations after the worker releases its lease.
 * Collision edits only the rule; HALT creates a fresh behavioral phenotype. */
void ant_mutate_in_place(Ant *ant, AntColony *colony)
{
    TurmiteRule *private_rule = &colony->runtime_rules[ant_index_of(colony, ant)];
    *private_rule = *ant->rule;
    rules_mutate(private_rule, &ant->rng_state);
    ant->rule = private_rule;
    atomic_store_explicit(&ant->rule_index, RULE_INDEX_RUNTIME, memory_order_relaxed);
}

void ant_rebirth_random(Ant *ant, AntColony *colony)
{
    TurmiteRule *private_rule = &colony->runtime_rules[ant_index_of(colony, ant)];
    rules_generate(private_rule, &ant->rng_state);
    ant->rule = private_rule;
    atomic_store_explicit(&ant->rule_index, RULE_INDEX_RUNTIME, memory_order_relaxed);
    atomic_store_explicit(&ant->heading, (uint8_t)ant_rng_uniform(&ant->rng_state, 4u), memory_order_relaxed);
    atomic_store_explicit(&ant->state, (uint8_t)ant_rng_uniform(&ant->rng_state, private_rule->states), memory_order_relaxed);
    reset_schedule(ant, &ant->rng_state);
    atomic_fetch_and_explicit(&ant->flags, ~(uint32_t)(ANT_F_DRAINING | ANT_F_EXPIRED), memory_order_acq_rel);
}

uint8_t ant_occupant_at(const AntColony *colony, uint32_t x, uint32_t y)
{
    if (!colony || !colony->occupancy || x >= colony->occupancy_width) return 0;
    const size_t cell = occupancy_index(colony, x, y);
    if (cell >= colony->occupancy_cells) return 0;
    return atomic_load_explicit(&colony->occupancy[cell], memory_order_relaxed);
}

void ant_release_occupancy(Ant *ant, AntColony *colony)
{
    if (!ant || !colony || !colony->occupancy) return;
    const size_t index = ant_index_of(colony, ant);
    const uint32_t p = atomic_load_explicit(&colony->positions[index], memory_order_relaxed);
    const size_t cell = occupancy_index(colony, packed_x(p), packed_y(p));
    if (cell >= colony->occupancy_cells) return;
    uint8_t expected = ant_owner_id(index);
    (void)atomic_compare_exchange_strong_explicit(&colony->occupancy[cell], &expected, 0,
                                                  memory_order_acq_rel,
                                                  memory_order_relaxed);
}

bool ant_try_reclaim_position(Ant *ant, AntColony *colony)
{
    if (!ant || !colony || !colony->occupancy) return false;
    const size_t index = ant_index_of(colony, ant);
    const uint32_t p = atomic_load_explicit(&colony->positions[index], memory_order_relaxed);
    const size_t cell = occupancy_index(colony, packed_x(p), packed_y(p));
    if (cell >= colony->occupancy_cells) return false;
    const uint8_t self_id = ant_owner_id(index);
    uint8_t owner = atomic_load_explicit(&colony->occupancy[cell], memory_order_relaxed);
    if (owner == self_id) return true;
    if (owner != 0) return false;
    return atomic_compare_exchange_strong_explicit(&colony->occupancy[cell], &owner, self_id,
                                                   memory_order_acq_rel,
                                                   memory_order_relaxed);
}

static bool mark_collision_loser(Ant *loser, AntColony *colony)
{
    const uint32_t old = atomic_fetch_or_explicit(&loser->flags,
                                                   ANT_F_CLOBBERED | ANT_F_DISPLACED,
                                                   memory_order_acq_rel);
    if (!(old & ANT_F_CLOBBERED)) {
        atomic_fetch_add_explicit(&colony->collisions, 1u, memory_order_relaxed);
        return true;
    }
    return false;
}

/* Move the read head by transferring ownership between occupancy bytes.
 * Empty destinations take one CAS. On contact, token balance is "health";
 * healthier ant wins, with lower ant index as deterministic tie-breaker.
 * A winner replaces the resident byte; the loser becomes displaced/clobbered
 * and is paused/mutated by the scheduler before reclaiming its position. */
static bool move_claim(Ant *self, AntColony *colony,
                       uint32_t old_x, uint32_t old_y,
                       uint32_t new_x, uint32_t new_y)
{
    const size_t self_index = ant_index_of(colony, self);
    const uint8_t self_id = ant_owner_id(self_index);
    const size_t old_cell = occupancy_index(colony, old_x, old_y);
    const size_t new_cell = occupancy_index(colony, new_x, new_y);

    if (old_cell != new_cell && old_cell < colony->occupancy_cells) {
        uint8_t expected = self_id;
        (void)atomic_compare_exchange_strong_explicit(&colony->occupancy[old_cell], &expected, 0,
                                                      memory_order_acq_rel,
                                                      memory_order_relaxed);
    }

    for (;;) {
        uint8_t owner = 0;
        if (atomic_compare_exchange_weak_explicit(&colony->occupancy[new_cell], &owner, self_id,
                                                  memory_order_acq_rel,
                                                  memory_order_relaxed))
            return true;
        if (owner == self_id) return true;

        const size_t other_index = (size_t)(owner - 1u);
        if (owner == 0 || other_index >= TURMITE_MAX_ANTS) {
            if (owner != 0) {
                uint8_t expected = owner;
                (void)atomic_compare_exchange_weak_explicit(&colony->occupancy[new_cell], &expected, 0,
                                                            memory_order_acq_rel,
                                                            memory_order_relaxed);
            }
            continue;
        }

        Ant *other = &colony->ants[other_index];
        const uint32_t self_health = atomic_load_explicit(&self->tokens_fp, memory_order_relaxed);
        const uint32_t other_health = atomic_load_explicit(&other->tokens_fp, memory_order_relaxed);
        const bool self_wins = self_health > other_health ||
                               (self_health == other_health && self_index < other_index);

        if (!self_wins) {
            (void)mark_collision_loser(self, colony);
            return false;
        }

        uint8_t expected = owner;
        if (atomic_compare_exchange_weak_explicit(&colony->occupancy[new_cell], &expected, self_id,
                                                  memory_order_acq_rel,
                                                  memory_order_relaxed)) {
            (void)mark_collision_loser(other, colony);
            return true;
        }
    }
}

size_t ant_execute_quantum(Ant *ant, AntColony *colony, World *world, size_t quantum)
{
    uint32_t f = flags_load(ant);
    if (!(f & ANT_F_ENABLED)) return 0;
    if (f & (ANT_F_HALTED | ANT_F_CLOBBERED | ANT_F_WAITING | ANT_F_DISPLACED)) return 0;

    const size_t self_index = ant_index_of(colony, ant);
    const uint32_t initial_position = atomic_load_explicit(&colony->positions[self_index], memory_order_relaxed);
    uint32_t x = packed_x(initial_position);
    uint32_t y = packed_y(initial_position);
    uint8_t heading = atomic_load_explicit(&ant->heading, memory_order_relaxed) & 3u;
    uint8_t state = atomic_load_explicit(&ant->state, memory_order_relaxed);
    const TurmiteRule *rule = ant->rule;
    const size_t width = (size_t)world->width;
    size_t executed = 0;

    /* One instruction follows the classic turmite cycle:
     * read tape -> choose table action -> write -> change state/heading -> move.
     * Only whole-token grants enter this loop, so one relaxed token decrement is
     * enough per instruction. Tape read/write remains deliberately non-atomic as
     * a transaction even though each individual byte access is atomic. */
    while (executed < quantum) {
        /* A concurrent collision may finish this instruction, never the grant. */
        if (flags_load(ant) & (ANT_F_CLOBBERED | ANT_F_HALTED)) break;
        atomic_fetch_sub_explicit(&ant->tokens_fp, TOKEN_FP_ONE, memory_order_relaxed);

        const size_t idx = (size_t)y * width + x;
        const uint8_t color = atomic_load_explicit(&world->data[idx], memory_order_relaxed);
        const RuleAction *action = NULL;
        if (state < rule->states && color < rule->colors)
            action = &rule->table[state][color];
        RuleAction fallback = { color, TURN_F, state, false };
        if (!action) action = &fallback;

        /* Presentation observes the tape independently of ant execution. */
        world_store_cell(world, idx, action->write_color);

        state = action->next_state;
        heading = apply_turn(heading, action->turn);
        if (action->halt) {
            atomic_store_explicit(&ant->state, state, memory_order_relaxed);
            atomic_store_explicit(&ant->heading, heading, memory_order_relaxed);
            atomic_fetch_or_explicit(&ant->flags, ANT_F_HALTED, memory_order_acq_rel);
            ++executed;
            break;
        }

        if (action->turn != TURN_H) {
            int nx = (int)x + DX[heading];
            int ny = (int)y + DY[heading];
            if (nx < 0) nx += world->width;
            else if (nx >= world->width) nx -= world->width;
            if (ny < 0) ny += world->height;
            else if (ny >= world->height) ny -= world->height;
            const uint32_t old_x = x;
            const uint32_t old_y = y;
            x = (uint32_t)nx;
            y = (uint32_t)ny;
            if (!move_claim(ant, colony, old_x, old_y, x, y)) {
                ++executed;
                break;
            }
        }

        ++executed;
    }

    /* Publish cold state only once per burst. This keeps the instruction loop
     * cache-local and avoids a shared position store on every move. */
    publish_position(colony, self_index, x, y);
    atomic_store_explicit(&ant->state, state, memory_order_relaxed);
    atomic_store_explicit(&ant->heading, heading, memory_order_relaxed);
    return executed;
}

uint64_t ant_instruction_count(const AntColony *colony, size_t ant_index)
{
    if (!colony || ant_index >= TURMITE_MAX_ANTS) return 0;
    return atomic_load_explicit(&colony->stats[ant_index].instructions, memory_order_relaxed);
}

uint64_t ant_mutation_count(const AntColony *colony, size_t ant_index)
{
    if (!colony || ant_index >= TURMITE_MAX_ANTS) return 0;
    return atomic_load_explicit(&colony->stats[ant_index].mutations, memory_order_relaxed);
}

double ant_tokens(const Ant *ant)
{
    return ant ? (double)atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed) / TOKEN_FP_ONE : 0.0;
}

double ant_token_rate(const Ant *ant)
{
    return ant ? (double)atomic_load_explicit(&ant->token_rate, memory_order_relaxed) : 0.0;
}

double ant_token_capacity(const Ant *ant)
{
    return ant ? (double)atomic_load_explicit(&ant->token_capacity_fp, memory_order_relaxed) / TOKEN_FP_ONE : 0.0;
}
