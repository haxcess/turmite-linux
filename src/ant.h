#ifndef TURMITE_ANT_H
#define TURMITE_ANT_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include "rng.h"
#include "rules.h"
#include "world.h"

#define TURMITE_MAX_ANTS 32

/* Ant execution is split between worker-local state and shared metadata. A
 * worker leases one ant at a time, executes a burst, then publishes the cold
 * state needed by the scheduler/debugger. */

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
    ANT_F_DRAINING  = 1u << 4,
    ANT_F_HALTED    = 1u << 5,
    ANT_F_DISPLACED = 1u << 6,
    ANT_F_WAITING   = 1u << 7 /* mutated/reborn, awaiting residency */
};

typedef struct Ant Ant;
typedef struct AntColony AntColony;

/* Keep only cross-thread state atomic. The ant's rule, direction and
 * internal state are owned by its worker while leased; token health remains
 * atomic because another worker may inspect it during collision resolution. */
struct Ant {
    _Atomic uint32_t flags;
    _Atomic uint8_t heading;
    _Atomic uint8_t state;
    _Atomic uint16_t rule_index;

    const TurmiteRule *rule;

    _Atomic uint32_t tokens_fp;
    _Atomic uint32_t token_rate;
    _Atomic uint32_t token_capacity_fp;
    _Atomic uint32_t weight;

    /* Per-ant LFSR removes contention on one shared runtime RNG. */
    uint32_t rng_state;
};

typedef struct {
    _Atomic uint64_t instructions;
    _Atomic uint64_t mutations;
} AntStats;

struct AntColony {
    Ant ants[TURMITE_MAX_ANTS];
    AntStats stats[TURMITE_MAX_ANTS];

    /* Cold, private tables keep mutable rules out of the hot ant metadata. */
    TurmiteRule runtime_rules[TURMITE_MAX_ANTS];

    /* Published positions are compact snapshots, not the collision search.
     * Packing x/y into one word keeps the cross-thread metadata dense. */
    alignas(64) _Atomic uint32_t positions[TURMITE_MAX_ANTS];
    _Atomic uint32_t enabled_mask;

    /* Derived O(1) read-head index. 0 means empty; 1..32 are ant index + 1.
     * It provides exclusive residency without becoming part of the tape. */
    _Atomic uint8_t *occupancy;
    size_t occupancy_cells;
    uint32_t occupancy_width;

    _Atomic size_t active_population;
    _Atomic uint64_t collisions;
};

int ant_colony_init(AntColony *colony, const World *world);
void ant_colony_reset(AntColony *colony);
void ant_colony_destroy(AntColony *colony);
void ant_randomize(Ant *ant, AntColony *colony, const World *world, Lfsr32 *rng, const TurmiteRule *rule);
void ant_clone(Ant *dst, AntColony *colony, const Ant *src, const World *world, Lfsr32 *rng);
void ant_mutate_in_place(Ant *ant, AntColony *colony);
void ant_rebirth_random(Ant *ant, AntColony *colony);
uint16_t ant_rule_index(const Ant *ant);
uint32_t ant_packed_position(const AntColony *colony, size_t ant_index);
uint32_t ant_position_x(const AntColony *colony, size_t ant_index);
uint32_t ant_position_y(const AntColony *colony, size_t ant_index);
uint8_t ant_occupant_at(const AntColony *colony, uint32_t x, uint32_t y);
bool ant_try_reclaim_position(Ant *ant, AntColony *colony);
void ant_release_occupancy(Ant *ant, AntColony *colony);

/* Execute up to quantum instructions. Logical tape writes are intentionally
 * racy; occupancy claims are the separate mechanism that detects collisions. */
size_t ant_execute_quantum(Ant *ant, AntColony *colony, World *world, size_t quantum);

uint64_t ant_instruction_count(const AntColony *colony, size_t ant_index);
uint64_t ant_mutation_count(const AntColony *colony, size_t ant_index);

double ant_tokens(const Ant *ant);
double ant_token_rate(const Ant *ant);
double ant_token_capacity(const Ant *ant);

#endif
