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
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        Ant *ant = &colony->ants[i];
        atomic_init(&ant->flags, 0);
        atomic_init(&ant->x, 0);
        atomic_init(&ant->y, 0);
        atomic_init(&ant->heading, 0);
        atomic_init(&ant->state, 0);
        atomic_init(&ant->rule_index, 0);
        atomic_init(&ant->tokens_fp, 0);
        atomic_init(&ant->token_rate, 1000u);
        atomic_init(&ant->token_capacity_fp, 64u * TOKEN_FP_ONE);
        atomic_init(&ant->weight, 1u);
        atomic_init(&ant->instructions, 0);
        atomic_init(&ant->mutations, 0);
        ant->rule = rules_get(0);
        ant->last_token_us = 0;
        ant->rng_state = (uint32_t)(0x9E3779B9u ^ (uint32_t)i);
    }
}

static void reseed_local_rng(Ant *ant, uint32_t seed)
{
    ant->rng_state = seed ? seed : 1u;
}

void ant_randomize(Ant *ant, const World *world, Lfsr32 *rng, const TurmiteRule *rule, uint64_t now_us)
{
    /* Seed the ant-local generator from the universe RNG once. */
    reseed_local_rng(ant, rng_next(rng));

    ant->rule = rule;
    atomic_store_explicit(&ant->rule_index, (uint16_t)rules_index_of(rule), memory_order_relaxed);
    atomic_store_explicit(&ant->x, ant_rng_uniform(&ant->rng_state, (uint32_t)world->width), memory_order_relaxed);
    atomic_store_explicit(&ant->y, ant_rng_uniform(&ant->rng_state, (uint32_t)world->height), memory_order_relaxed);
    atomic_store_explicit(&ant->heading, (uint8_t)ant_rng_uniform(&ant->rng_state, 4u), memory_order_relaxed);
    atomic_store_explicit(&ant->state, (uint8_t)ant_rng_uniform(&ant->rng_state, rule->states), memory_order_relaxed);
    reset_schedule(ant, &ant->rng_state);
    atomic_store_explicit(&ant->instructions, 0, memory_order_relaxed);
    atomic_store_explicit(&ant->mutations, 0, memory_order_relaxed);
    ant->last_token_us = now_us;
    atomic_store_explicit(&ant->flags, ANT_F_ENABLED, memory_order_release);
}

void ant_clone(Ant *dst, const Ant *src, const World *world, Lfsr32 *rng, uint64_t now_us)
{
    const TurmiteRule *src_rule = src->rule;
    dst->rule = src_rule;
    atomic_store_explicit(&dst->rule_index, atomic_load_explicit(&src->rule_index, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->x, rng_uniform(rng, (uint32_t)world->width), memory_order_relaxed);
    atomic_store_explicit(&dst->y, rng_uniform(rng, (uint32_t)world->height), memory_order_relaxed);
    atomic_store_explicit(&dst->heading, atomic_load_explicit(&src->heading, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->state, atomic_load_explicit(&src->state, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->token_rate, atomic_load_explicit(&src->token_rate, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->token_capacity_fp, atomic_load_explicit(&src->token_capacity_fp, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->tokens_fp, 0, memory_order_relaxed);
    atomic_store_explicit(&dst->weight, atomic_load_explicit(&src->weight, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->instructions, 0, memory_order_relaxed);
    atomic_store_explicit(&dst->mutations, 0, memory_order_relaxed);
    reseed_local_rng(dst, rng_next(rng));
    dst->last_token_us = now_us;
    atomic_store_explicit(&dst->flags, ANT_F_ENABLED, memory_order_release);
}

uint16_t ant_rule_index(const Ant *ant)
{
    return ant ? atomic_load_explicit(&ant->rule_index, memory_order_relaxed) : 0;
}

void ant_mutate_in_place(Ant *ant, uint64_t now_us)
{
    const TurmiteRule *rule = rules_pick(ant_rng_uniform(&ant->rng_state, (uint32_t)rules_count()));
    ant->rule = rule;
    atomic_store_explicit(&ant->rule_index, (uint16_t)rules_index_of(rule), memory_order_relaxed);
    atomic_store_explicit(&ant->heading, (uint8_t)ant_rng_uniform(&ant->rng_state, 4u), memory_order_relaxed);
    atomic_store_explicit(&ant->state, (uint8_t)ant_rng_uniform(&ant->rng_state, rule->states), memory_order_relaxed);
    reset_schedule(ant, &ant->rng_state);
    ant->last_token_us = now_us;
    atomic_fetch_add_explicit(&ant->mutations, 1u, memory_order_relaxed);
    atomic_fetch_and_explicit(&ant->flags,
                              ~(uint32_t)(ANT_F_CLOBBERED | ANT_F_EXPIRED | ANT_F_DRAINING),
                              memory_order_acq_rel);
    atomic_fetch_or_explicit(&ant->flags, ANT_F_ENABLED, memory_order_release);
}

static inline void accrue_tokens(Ant *ant, uint64_t now_us)
{
    if (now_us <= ant->last_token_us) return;
    uint64_t elapsed_us = now_us - ant->last_token_us;
    uint32_t rate = atomic_load_explicit(&ant->token_rate, memory_order_relaxed);
    uint32_t cap_fp = atomic_load_explicit(&ant->token_capacity_fp, memory_order_relaxed);
    uint32_t current_fp = atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed);
    ant->last_token_us = now_us;
    if (rate == 0 || current_fp >= cap_fp) return;

    /* Once the bucket is full, additional elapsed time is irrelevant. */
    uint64_t room_fp = (uint64_t)cap_fp - current_fp;
    uint64_t max_elapsed_us = ((room_fp + TOKEN_FP_ONE - 1u) / TOKEN_FP_ONE * 1000000ull + rate - 1u) / rate;
    if (elapsed_us > max_elapsed_us) elapsed_us = max_elapsed_us;

    const uint64_t whole = (uint64_t)rate * elapsed_us;
    const uint64_t whole_tokens = whole / 1000000ull;
    const uint64_t rem = whole % 1000000ull;
    uint64_t add_fp = whole_tokens * TOKEN_FP_ONE + (rem * TOKEN_FP_ONE) / 1000000ull;
    if (add_fp >= room_fp) current_fp = cap_fp;
    else current_fp += (uint32_t)add_fp;
    atomic_store_explicit(&ant->tokens_fp, current_fp, memory_order_relaxed);
}

static Ant *find_collision(Ant *self, AntColony *colony, uint32_t x, uint32_t y)
{
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        Ant *other = &colony->ants[i];
        if (other == self) continue;
        uint32_t f = flags_load(other);
        if (!(f & ANT_F_ENABLED)) continue;
        if (atomic_load_explicit(&other->x, memory_order_relaxed) != x) continue;
        if (atomic_load_explicit(&other->y, memory_order_relaxed) != y) continue;
        return other;
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

    uint32_t x = atomic_load_explicit(&ant->x, memory_order_relaxed);
    uint32_t y = atomic_load_explicit(&ant->y, memory_order_relaxed);
    uint8_t heading = atomic_load_explicit(&ant->heading, memory_order_relaxed) & 3u;
    uint8_t state = atomic_load_explicit(&ant->state, memory_order_relaxed);
    const TurmiteRule *rule = ant->rule;
    const size_t width = (size_t)world->width;
    size_t executed = 0;

    while (executed < quantum) {
        f = flags_load(ant);
        if (f & ANT_F_CLOBBERED) break;
        if (!(f & ANT_F_ENABLED)) break;

        uint32_t token_fp = atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed);
        if (token_fp < TOKEN_FP_ONE) break;
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
        atomic_store_explicit(&ant->state, state, memory_order_relaxed);
        atomic_store_explicit(&ant->heading, heading, memory_order_relaxed);

        if (action->halt) {
            atomic_fetch_or_explicit(&ant->flags, ANT_F_EXPIRED, memory_order_acq_rel);
            atomic_fetch_and_explicit(&ant->flags, ~ANT_F_ENABLED, memory_order_acq_rel);
            atomic_fetch_sub_explicit(&colony->active_population, 1u, memory_order_relaxed);
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
            atomic_store_explicit(&ant->x, x, memory_order_relaxed);
            atomic_store_explicit(&ant->y, y, memory_order_relaxed);
            Ant *other = find_collision(ant, colony, x, y);
            if (other) resolve_collision(ant, other, colony);
        }

        atomic_fetch_add_explicit(&ant->instructions, 1u, memory_order_relaxed);
        ++executed;
    }

    return executed;
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
