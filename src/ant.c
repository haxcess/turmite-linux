#include "ant.h"

static const int8_t DX[4] = { 0, 1, 0, -1 };
static const int8_t DY[4] = { -1, 0, 1, 0 };

static inline uint8_t apply_turn(uint8_t heading, TurnCode turn)
{
    switch (turn) {
        case TURN_R: return (uint8_t)((heading + 1u) & 3u);
        case TURN_L: return (uint8_t)((heading + 3u) & 3u);
        case TURN_B: return (uint8_t)((heading + 2u) & 3u);
        case TURN_N: return 0;
        case TURN_E: return 1;
        case TURN_S: return 2;
        case TURN_W: return 3;
        case TURN_F:
        case TURN_H:
        default: return heading;
    }
}

static inline uint32_t ant_rng_next(uint32_t *state)
{
    uint32_t old = *state;
    uint32_t next = old >> 1;
    if (old & 1u) next ^= 0x80200003u;
    *state = next ? next : 1u;
    return next;
}

static inline uint32_t ant_rng_uniform(uint32_t *state, uint32_t upper_exclusive)
{
    return upper_exclusive ? ant_rng_next(state) % upper_exclusive : 0;
}

static uint32_t random_rate(uint32_t *state)
{
    return 50000u + ant_rng_uniform(state, 1950001u);
}

static uint32_t random_weight(uint32_t *state)
{
    return 1u + ant_rng_uniform(state, 8u);
}

static uint32_t random_capacity_fp(uint32_t *state)
{
    uint32_t whole = 128u + ant_rng_uniform(state, 3969u);
    return whole * TOKEN_FP_ONE;
}

static inline uint32_t flags_load(const Ant *ant)
{
    return atomic_load_explicit(&ant->flags, memory_order_acquire);
}

static inline void flags_or(Ant *ant, uint32_t bits)
{
    atomic_fetch_or_explicit(&ant->flags, bits, memory_order_acq_rel);
}

static inline void flags_and(Ant *ant, uint32_t bits)
{
    atomic_fetch_and_explicit(&ant->flags, bits, memory_order_acq_rel);
}

static inline uint32_t pack_position(uint32_t x, uint32_t y)
{
    return ((y & UINT32_C(0xffff)) << 16) | (x & UINT32_C(0xffff));
}

static inline uint32_t packed_x(uint32_t p) { return p & UINT32_C(0xffff); }
static inline uint32_t packed_y(uint32_t p) { return p >> 16; }

static inline size_t ant_index_of(const AntColony *colony, const Ant *ant)
{
    return (size_t)(ant - colony->ants);
}

static inline void publish_position(AntColony *colony, size_t index, uint32_t x, uint32_t y)
{
    atomic_store_explicit(&colony->positions[index], pack_position(x, y), memory_order_relaxed);
}

static void reset_schedule(Ant *ant, uint32_t *state)
{
    atomic_store_explicit(&ant->token_rate, random_rate(state), memory_order_relaxed);
    atomic_store_explicit(&ant->token_capacity_fp, random_capacity_fp(state), memory_order_relaxed);
    atomic_store_explicit(&ant->tokens_fp, 0u, memory_order_relaxed);
    atomic_store_explicit(&ant->weight, random_weight(state), memory_order_relaxed);
}

void ant_colony_zero(AntColony *colony)
{
    atomic_init(&colony->active_population, 0);
    atomic_init(&colony->collisions, 0);
    atomic_init(&colony->enabled_mask, 0);
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        Ant *ant = &colony->ants[i];
        atomic_init(&ant->flags, 0);
        atomic_init(&colony->positions[i], 0);
        atomic_init(&ant->heading, 0);
        atomic_init(&ant->state, 0);
        atomic_init(&ant->rule_index, 0);
        atomic_init(&ant->tokens_fp, 0);
        atomic_init(&ant->token_rate, 1000u);
        atomic_init(&ant->token_capacity_fp, 64u * TOKEN_FP_ONE);
        atomic_init(&ant->weight, 1u);
        ant->rule = rules_get(0);
        atomic_init(&colony->stats[i].instructions, 0);
        atomic_init(&colony->stats[i].mutations, 0);
        ant->rng_state = (uint32_t)(0x9E3779B9u ^ (uint32_t)i);
    }
}

static void reseed_local_rng(Ant *ant, uint32_t seed)
{
    ant->rng_state = seed ? seed : 1u;
}

void ant_randomize(Ant *ant, AntColony *colony, const World *world, Lfsr32 *rng, const TurmiteRule *rule)
{
    /* Seed the ant-local generator from the universe RNG once. */
    reseed_local_rng(ant, rng_next(rng));

    ant->rule = rule;
    atomic_store_explicit(&ant->rule_index, (uint16_t)rules_index_of(rule), memory_order_relaxed);
    const uint32_t x = ant_rng_uniform(&ant->rng_state, (uint32_t)world->width);
    const uint32_t y = ant_rng_uniform(&ant->rng_state, (uint32_t)world->height);
    const size_t index = ant_index_of(colony, ant);
    publish_position(colony, index, x, y);
    atomic_store_explicit(&ant->heading, (uint8_t)ant_rng_uniform(&ant->rng_state, 4u), memory_order_relaxed);
    atomic_store_explicit(&ant->state, (uint8_t)ant_rng_uniform(&ant->rng_state, rule->states), memory_order_relaxed);
    reset_schedule(ant, &ant->rng_state);
    atomic_store_explicit(&ant->flags, ANT_F_ENABLED, memory_order_release);
    atomic_fetch_or_explicit(&colony->enabled_mask, UINT32_C(1) << index, memory_order_release);
}

void ant_clone(Ant *dst, AntColony *colony, const Ant *src, const World *world, Lfsr32 *rng)
{
    const TurmiteRule *src_rule = src->rule;
    dst->rule = src_rule;
    atomic_store_explicit(&dst->rule_index, atomic_load_explicit(&src->rule_index, memory_order_relaxed), memory_order_relaxed);
    const uint32_t x = rng_uniform(rng, (uint32_t)world->width);
    const uint32_t y = rng_uniform(rng, (uint32_t)world->height);
    const size_t dst_index = ant_index_of(colony, dst);
    publish_position(colony, dst_index, x, y);
    atomic_store_explicit(&dst->heading, atomic_load_explicit(&src->heading, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->state, atomic_load_explicit(&src->state, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->token_rate, atomic_load_explicit(&src->token_rate, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->token_capacity_fp, atomic_load_explicit(&src->token_capacity_fp, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->tokens_fp, 0, memory_order_relaxed);
    atomic_store_explicit(&dst->weight, atomic_load_explicit(&src->weight, memory_order_relaxed), memory_order_relaxed);
    reseed_local_rng(dst, rng_next(rng));
    atomic_store_explicit(&dst->flags, ANT_F_ENABLED, memory_order_release);
    atomic_fetch_or_explicit(&colony->enabled_mask, UINT32_C(1) << dst_index, memory_order_release);
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

void ant_mutate_in_place(Ant *ant)
{
    const TurmiteRule *rule = rules_pick(ant_rng_uniform(&ant->rng_state, (uint32_t)rules_count()));
    ant->rule = rule;
    atomic_store_explicit(&ant->rule_index, (uint16_t)rules_index_of(rule), memory_order_relaxed);
    atomic_store_explicit(&ant->heading, (uint8_t)ant_rng_uniform(&ant->rng_state, 4u), memory_order_relaxed);
    atomic_store_explicit(&ant->state, (uint8_t)ant_rng_uniform(&ant->rng_state, rule->states), memory_order_relaxed);
    reset_schedule(ant, &ant->rng_state);
    atomic_fetch_and_explicit(&ant->flags,
                              ~(uint32_t)(ANT_F_CLOBBERED | ANT_F_EXPIRED | ANT_F_DRAINING | ANT_F_HALTED),
                              memory_order_acq_rel);
    atomic_fetch_or_explicit(&ant->flags, ANT_F_ENABLED, memory_order_release);
}

static Ant *find_collision(Ant *self, AntColony *colony, uint32_t x, uint32_t y)
{
    const size_t self_index = ant_index_of(colony, self);
    uint32_t candidates = atomic_load_explicit(&colony->enabled_mask, memory_order_relaxed);
    candidates &= ~(UINT32_C(1) << self_index);
    const uint32_t destination = pack_position(x, y);

    while (candidates) {
        const unsigned i = (unsigned)__builtin_ctz(candidates);
        candidates &= candidates - 1u;
        if (atomic_load_explicit(&colony->positions[i], memory_order_relaxed) == destination)
            return &colony->ants[i];
    }
    return NULL;
}

static void resolve_collision(Ant *self, Ant *other, AntColony *colony)
{
    if (!other || other == self) return;
    uint32_t self_health = atomic_load_explicit(&self->tokens_fp, memory_order_relaxed);
    uint32_t other_health = atomic_load_explicit(&other->tokens_fp, memory_order_relaxed);

    Ant *loser;
    if (self_health < other_health) loser = self;
    else if (other_health < self_health) loser = other;
    else loser = (self < other) ? other : self;

    uint32_t old_flags = flags_load(loser);
    while (!(old_flags & ANT_F_CLOBBERED)) {
        uint32_t desired = old_flags | ANT_F_CLOBBERED;
        if (atomic_compare_exchange_weak_explicit(&loser->flags, &old_flags, desired,
                                                   memory_order_acq_rel, memory_order_relaxed)) {
            atomic_fetch_add_explicit(&colony->collisions, 1u, memory_order_relaxed);
            break;
        }
    }
}

size_t ant_execute_quantum(Ant *ant, AntColony *colony, World *world, size_t quantum)
{
    uint32_t f = flags_load(ant);
    if (!(f & ANT_F_ENABLED)) return 0;
    if (f & ANT_F_HALTED) return 0;

    const size_t self_index = ant_index_of(colony, ant);
    const uint32_t initial_position = atomic_load_explicit(&colony->positions[self_index], memory_order_relaxed);
    uint32_t x = packed_x(initial_position);
    uint32_t y = packed_y(initial_position);
    uint8_t heading = atomic_load_explicit(&ant->heading, memory_order_relaxed) & 3u;
    uint8_t state = atomic_load_explicit(&ant->state, memory_order_relaxed);
    const TurmiteRule *rule = ant->rule;
    const size_t width = (size_t)world->width;
    size_t executed = 0;

    while (executed < quantum) {
        /* The scheduler grants no more instructions than the ant currently has
         * whole tokens for, so the hot path can consume one token with a single
         * relaxed RMW. A collision may clobber the ant while it is leased; the
         * current quantum is its task and may complete. */
        atomic_fetch_sub_explicit(&ant->tokens_fp, TOKEN_FP_ONE, memory_order_relaxed);

        const size_t idx = (size_t)y * width + x;
        const uint8_t color = atomic_load_explicit(&world->data[idx], memory_order_relaxed);
        const RuleAction *action = NULL;
        if (state < rule->states && color < rule->colors) {
            action = &rule->table[state][color];
        }
        RuleAction fallback = { color, TURN_F, state, false };
        if (!action) action = &fallback;

        /* Deliberately not a transaction: another worker may read/write this cell between these operations. */
        atomic_store_explicit(&world->data[idx], action->write_color, memory_order_relaxed);

        state = action->next_state;
        heading = apply_turn(heading, action->turn);
        if (action->halt) {
            /* A HALT is self-clobber: preserve position, then let the scheduler
             * reincarnate this ant with a new rule/state/schedule. */
            atomic_store_explicit(&ant->state, state, memory_order_relaxed);
            atomic_store_explicit(&ant->heading, heading, memory_order_relaxed);
            atomic_fetch_or_explicit(&ant->flags, ANT_F_CLOBBERED, memory_order_acq_rel);
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
            x = (uint32_t)nx;
            y = (uint32_t)ny;
            publish_position(colony, self_index, x, y);
            Ant *other = find_collision(ant, colony, x, y);
            if (other) resolve_collision(ant, other, colony);
        }

        ++executed;
    }

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
    return (double)atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed) / (double)TOKEN_FP_ONE;
}

double ant_token_rate(const Ant *ant)
{
    return (double)atomic_load_explicit(&ant->token_rate, memory_order_relaxed);
}

double ant_token_capacity(const Ant *ant)
{
    return (double)atomic_load_explicit(&ant->token_capacity_fp, memory_order_relaxed) / (double)TOKEN_FP_ONE;
}
