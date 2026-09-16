#include "ant.h"


static const int DX[4] = { 0, 1, 0, -1 };
static const int DY[4] = { -1, 0, 1, 0 };

static uint8_t apply_turn(uint8_t heading, TurnCode turn)
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

static double random_rate(Lfsr32 *rng)
{
    return 50000.0 + (double)rng_uniform(rng, 1950001u);
}

static uint32_t random_weight(Lfsr32 *rng)
{
    return 1u + rng_uniform(rng, 8u);
}

static double random_capacity(Lfsr32 *rng)
{
    return 128.0 + (double)rng_uniform(rng, 3969u);
}

static void reset_schedule(Ant *ant, Lfsr32 *rng)
{
    atomic_store_explicit(&ant->sched.token_rate, random_rate(rng), memory_order_relaxed);
    atomic_store_explicit(&ant->sched.token_capacity, random_capacity(rng), memory_order_relaxed);
    atomic_store_explicit(&ant->sched.tokens, 0.0, memory_order_relaxed);
    atomic_store_explicit(&ant->sched.weight, random_weight(rng), memory_order_relaxed);
    atomic_store_explicit(&ant->sched.quantum_hint, 1.0, memory_order_relaxed);
}

void ant_colony_zero(AntColony *colony)
{
    atomic_init(&colony->active_population, 0);
    atomic_init(&colony->collisions, 0);
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        colony->ants[i].id = (uint32_t)i;
        atomic_init(&colony->ants[i].x, 0);
        atomic_init(&colony->ants[i].y, 0);
        atomic_init(&colony->ants[i].heading, 0);
        atomic_init(&colony->ants[i].state, 0);
        atomic_init(&colony->ants[i].sched.tokens, 0.0);
        atomic_init(&colony->ants[i].sched.weight, 1u);
        atomic_init(&colony->ants[i].sched.quantum_hint, 1.0);
        atomic_init(&colony->ants[i].enabled, false);
        atomic_init(&colony->ants[i].leased, false);
        atomic_init(&colony->ants[i].clobbered, false);
        atomic_init(&colony->ants[i].expired, false);
        atomic_init(&colony->ants[i].draining, false);
        atomic_init(&colony->ants[i].instructions, 0);
        atomic_init(&colony->ants[i].mutations, 0);
        atomic_init(&colony->ants[i].rule, rules_get(0));
        atomic_init(&colony->ants[i].rule_index, 0);
        atomic_store_explicit(&colony->ants[i].sched.token_rate, 1000.0, memory_order_relaxed);
        atomic_store_explicit(&colony->ants[i].sched.token_capacity, 64.0, memory_order_relaxed);
        colony->ants[i].last_token_time = 0.0;
    }
}

void ant_randomize(Ant *ant, const World *world, Lfsr32 *rng, const TurmiteRule *rule, double now)
{
    atomic_store_explicit(&ant->rule, rule, memory_order_release);
    atomic_store_explicit(&ant->rule_index, (uint16_t)rules_index_of(rule), memory_order_release);
    atomic_store_explicit(&ant->x, (int)rng_uniform(rng, (uint32_t)world->width), memory_order_relaxed);
    atomic_store_explicit(&ant->y, (int)rng_uniform(rng, (uint32_t)world->height), memory_order_relaxed);
    atomic_store_explicit(&ant->heading, (uint8_t)rng_uniform(rng, 4u), memory_order_relaxed);
    atomic_store_explicit(&ant->state, (uint8_t)rng_uniform(rng, rule->states), memory_order_relaxed);
    reset_schedule(ant, rng);
    atomic_store_explicit(&ant->enabled, true, memory_order_release);
    atomic_store_explicit(&ant->leased, false, memory_order_release);
    atomic_store_explicit(&ant->clobbered, false, memory_order_release);
    atomic_store_explicit(&ant->expired, false, memory_order_release);
    atomic_store_explicit(&ant->draining, false, memory_order_release);
    atomic_store_explicit(&ant->instructions, 0, memory_order_relaxed);
    atomic_store_explicit(&ant->mutations, 0, memory_order_relaxed);
    ant->last_token_time = now;
}

void ant_clone(Ant *dst, const Ant *src, const World *world, Lfsr32 *rng, double now)
{
    const TurmiteRule *src_rule = atomic_load_explicit(&src->rule, memory_order_acquire);
    uint16_t src_rule_index = atomic_load_explicit(&src->rule_index, memory_order_acquire);
    atomic_store_explicit(&dst->rule, src_rule, memory_order_release);
    atomic_store_explicit(&dst->rule_index, src_rule_index, memory_order_release);
    atomic_store_explicit(&dst->x, (int)rng_uniform(rng, (uint32_t)world->width), memory_order_relaxed);
    atomic_store_explicit(&dst->y, (int)rng_uniform(rng, (uint32_t)world->height), memory_order_relaxed);
    atomic_store_explicit(&dst->heading, atomic_load_explicit(&src->heading, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->state, atomic_load_explicit(&src->state, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->sched.token_rate, atomic_load_explicit(&src->sched.token_rate, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->sched.token_capacity, atomic_load_explicit(&src->sched.token_capacity, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->sched.tokens, 0.0, memory_order_relaxed);
    atomic_store_explicit(&dst->sched.weight, atomic_load_explicit(&src->sched.weight, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->sched.quantum_hint, atomic_load_explicit(&src->sched.quantum_hint, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&dst->enabled, true, memory_order_release);
    atomic_store_explicit(&dst->leased, false, memory_order_release);
    atomic_store_explicit(&dst->clobbered, false, memory_order_release);
    atomic_store_explicit(&dst->expired, false, memory_order_release);
    atomic_store_explicit(&dst->draining, false, memory_order_release);
    atomic_store_explicit(&dst->instructions, 0, memory_order_relaxed);
    atomic_store_explicit(&dst->mutations, 0, memory_order_relaxed);
    dst->last_token_time = now;
}

uint16_t ant_rule_index(const Ant *ant)
{
    return ant ? atomic_load_explicit(&ant->rule_index, memory_order_acquire) : 0;
}

void ant_mutate_in_place(Ant *ant, Lfsr32 *rng, double now)
{
    const TurmiteRule *rule = rules_pick(rng_uniform(rng, (uint32_t)rules_count()));
    atomic_store_explicit(&ant->rule, rule, memory_order_release);
    atomic_store_explicit(&ant->rule_index, (uint16_t)rules_index_of(rule), memory_order_release);
    atomic_store_explicit(&ant->heading, (uint8_t)rng_uniform(rng, 4u), memory_order_relaxed);
    atomic_store_explicit(&ant->state, (uint8_t)rng_uniform(rng, rule->states), memory_order_relaxed);
    reset_schedule(ant, rng);
    ant->last_token_time = now;
    atomic_store_explicit(&ant->clobbered, false, memory_order_release);
    atomic_store_explicit(&ant->expired, false, memory_order_release);
    atomic_store_explicit(&ant->draining, false, memory_order_release);
    atomic_store_explicit(&ant->enabled, true, memory_order_release);
    atomic_fetch_add_explicit(&ant->mutations, 1u, memory_order_relaxed);
}

static void accrue_tokens(Ant *ant, double now)
{
    double elapsed = now - ant->last_token_time;
    if (elapsed <= 0.0) return;
    double tokens = atomic_load_explicit(&ant->sched.tokens, memory_order_relaxed);
    double token_rate = atomic_load_explicit(&ant->sched.token_rate, memory_order_relaxed);
    double token_capacity = atomic_load_explicit(&ant->sched.token_capacity, memory_order_relaxed);
    tokens += token_rate * elapsed;
    if (tokens > token_capacity) tokens = token_capacity;
    atomic_store_explicit(&ant->sched.tokens, tokens, memory_order_relaxed);
    ant->last_token_time = now;
}

static Ant *find_collision(Ant *self, AntColony *colony, int x, int y)
{
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        Ant *other = &colony->ants[i];
        if (other == self || !atomic_load_explicit(&other->enabled, memory_order_relaxed)) continue;
        int ox = atomic_load_explicit(&other->x, memory_order_relaxed);
        int oy = atomic_load_explicit(&other->y, memory_order_relaxed);
        if (ox == x && oy == y) return other;
    }
    return NULL;
}

static void resolve_collision(Ant *self, Ant *other, AntColony *colony)
{
    if (!other || self == other) return;
    double self_health = atomic_load_explicit(&self->sched.tokens, memory_order_relaxed);
    double other_health = atomic_load_explicit(&other->sched.tokens, memory_order_relaxed);

    Ant *loser = NULL;
    if (self_health < other_health) loser = self;
    else if (other_health < self_health) loser = other;
    else loser = (self->id < other->id) ? other : self;

    bool expected = false;
    if (atomic_compare_exchange_strong_explicit(
        &loser->clobbered, &expected, true,
        memory_order_acq_rel, memory_order_relaxed)) {
        atomic_fetch_add_explicit(&colony->collisions, 1u, memory_order_relaxed);
    }
}

int ant_execute_one(Ant *ant, AntColony *colony, World *world, Lfsr32 *rng, double now)
{
    if (!atomic_load_explicit(&ant->enabled, memory_order_acquire)) return 0;

    if (atomic_load_explicit(&ant->clobbered, memory_order_acquire)) {
        bool expected = true;
        if (atomic_compare_exchange_strong_explicit(&ant->clobbered, &expected, false, memory_order_acq_rel, memory_order_relaxed)) {
            ant_mutate_in_place(ant, rng, now);
        }
        return 0;
    }

    accrue_tokens(ant, now);
    double tokens = atomic_load_explicit(&ant->sched.tokens, memory_order_relaxed);
    if (tokens < 1.0) return 0;
    double after_spend = atomic_load_explicit(&ant->sched.tokens, memory_order_relaxed) - 1.0;
    if (after_spend < 0.0) after_spend = 0.0;
    atomic_store_explicit(&ant->sched.tokens, after_spend, memory_order_relaxed);

    int x = atomic_load_explicit(&ant->x, memory_order_relaxed);
    int y = atomic_load_explicit(&ant->y, memory_order_relaxed);
    uint8_t heading = atomic_load_explicit(&ant->heading, memory_order_relaxed) & 3u;
    uint8_t state = atomic_load_explicit(&ant->state, memory_order_relaxed);

    size_t idx = world_index(world, x, y);
    uint8_t color = world_load(world, idx);
    const TurmiteRule *rule = atomic_load_explicit(&ant->rule, memory_order_acquire);
    const RuleAction *action = NULL;
    if (rule && state < rule->states && color < rule->colors) {
        action = &rule->table[state][color];
    }
    static const RuleAction fallback = { 0, TURN_F, 0, false };
    if (!action) action = &fallback;

    /* Intentionally not a transaction: another worker can read/write the same cell between these operations. */
    world_store(world, idx, action->write_color);

    state = action->next_state;
    heading = apply_turn(heading, action->turn);
    atomic_store_explicit(&ant->state, state, memory_order_relaxed);
    atomic_store_explicit(&ant->heading, heading, memory_order_relaxed);

    if (action->halt) {
        atomic_store_explicit(&ant->enabled, false, memory_order_release);
        atomic_store_explicit(&ant->expired, true, memory_order_release);
        atomic_fetch_sub_explicit(&colony->active_population, 1u, memory_order_relaxed);
    } else if (action->turn != TURN_H) {
        x += DX[heading];
        y += DY[heading];
        if (x < 0) x += world->width;
        if (x >= world->width) x -= world->width;
        if (y < 0) y += world->height;
        if (y >= world->height) y -= world->height;
        atomic_store_explicit(&ant->x, x, memory_order_relaxed);
        atomic_store_explicit(&ant->y, y, memory_order_relaxed);
        Ant *other = find_collision(ant, colony, x, y);
        if (other) resolve_collision(ant, other, colony);
    }

    atomic_fetch_add_explicit(&ant->instructions, 1u, memory_order_relaxed);
    return 1;
}
