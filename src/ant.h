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

typedef struct {
    double token_rate;
    double token_capacity;
    _Atomic double tokens;
    _Atomic uint32_t weight;
    _Atomic double quantum_hint;
} ScheduleParams;

typedef struct Ant Ant;
typedef struct AntColony AntColony;

struct Ant {
    uint32_t id;
    _Atomic int x;
    _Atomic int y;
    _Atomic uint8_t heading;
    _Atomic uint8_t state;
    const TurmiteRule *rule;

    ScheduleParams sched;

    _Atomic bool enabled;
    _Atomic bool leased;
    _Atomic bool clobbered;
    _Atomic bool expired;
    _Atomic bool draining;
    _Atomic uint64_t instructions;
    _Atomic uint64_t mutations;
    double last_token_time;
};

struct AntColony {
    Ant ants[TURMITE_MAX_ANTS];
    _Atomic size_t active_population;
    _Atomic uint64_t collisions;
};

void ant_colony_zero(AntColony *colony);
void ant_randomize(Ant *ant, const World *world, Lfsr32 *rng, const TurmiteRule *rule, double now);
void ant_clone(Ant *dst, const Ant *src, const World *world, Lfsr32 *rng, double now);
void ant_mutate_in_place(Ant *ant, Lfsr32 *rng, double now);

/* Returns 1 when an instruction actually executed. */
int ant_execute_one(Ant *ant, AntColony *colony, World *world, Lfsr32 *rng, double now);

#endif
