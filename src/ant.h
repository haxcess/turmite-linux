#ifndef TURMITE_ANT_H
#define TURMITE_ANT_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "rng.h"
#include "rules.h"
#include "world.h"

#define TURMITE_MAX_ANTS 32

/* Token balances use Q16 fixed-point. One whole token is TOKEN_FP_ONE.
 * This preserves fractional accumulation without putting a floating-point
 * value in every ant or doing a floating-point operation per instruction. */
#define TOKEN_FP_SHIFT 16u
#define TOKEN_FP_ONE (UINT32_C(1) << TOKEN_FP_SHIFT)

enum {
    ANT_F_ENABLED   = 1u << 0,
    ANT_F_LEASED    = 1u << 1,
    ANT_F_CLOBBERED = 1u << 2,
    ANT_F_EXPIRED   = 1u << 3,
    ANT_F_DRAINING  = 1u << 4
};

typedef struct Ant Ant;
typedef struct AntColony AntColony;

/* Keep only cross-thread state atomic. The ant's rule, direction and
 * internal state are owned by its worker while leased; x/y and token health
 * remain atomic because collision detection can inspect a running ant. */
struct Ant {
    _Atomic uint32_t flags;
    _Atomic uint32_t x;
    _Atomic uint32_t y;
    _Atomic uint8_t heading;
    _Atomic uint8_t state;
    _Atomic uint16_t rule_index;

    const TurmiteRule *rule;

    _Atomic uint32_t tokens_fp;
    _Atomic uint32_t token_rate;
    _Atomic uint32_t token_capacity_fp;
    _Atomic uint32_t weight;

    _Atomic uint64_t instructions;
    _Atomic uint64_t mutations;

    /* Scheduler-only time bookkeeping. Not read by the dump path. */
    uint64_t last_token_us;

    /* Per-ant LFSR removes contention on one shared runtime RNG. */
    uint32_t rng_state;
};

struct AntColony {
    Ant ants[TURMITE_MAX_ANTS];
    _Atomic size_t active_population;
    _Atomic uint64_t collisions;
};

void ant_colony_zero(AntColony *colony);
void ant_randomize(Ant *ant, const World *world, Lfsr32 *rng, const TurmiteRule *rule, uint64_t now_us);
void ant_clone(Ant *dst, const Ant *src, const World *world, Lfsr32 *rng, uint64_t now_us);
void ant_mutate_in_place(Ant *ant, uint64_t now_us);
uint16_t ant_rule_index(const Ant *ant);

/* Execute up to quantum instructions. Returns the number actually executed. */
size_t ant_execute_quantum(Ant *ant, AntColony *colony, World *world, size_t quantum);

/* Fixed-point token helpers used by scheduler/debug paths. */
double ant_tokens(const Ant *ant);
double ant_token_rate(const Ant *ant);
double ant_token_capacity(const Ant *ant);

#endif
