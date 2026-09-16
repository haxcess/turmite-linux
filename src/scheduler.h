#ifndef TURMITE_SCHEDULER_H
#define TURMITE_SCHEDULER_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "ant.h"

typedef enum {
    SCHED_WFQ = 0
} SchedulerPolicy;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t work_available;
    AntColony *colony;
    SchedulerPolicy policy;
    bool stopping;
    _Atomic bool paused;
    _Atomic uint64_t dispatches;
    _Atomic uint64_t empty_scans;
    _Atomic uint64_t idle_waits;
    _Atomic uint64_t granted_instructions;
    double fair_credit[TURMITE_MAX_ANTS];
    uint64_t last_token_us[TURMITE_MAX_ANTS];
    _Atomic size_t quantum;
    _Atomic size_t min_service;
    _Atomic uint32_t token_rate_scale;
} Scheduler;

int scheduler_init(Scheduler *scheduler, AntColony *colony, SchedulerPolicy policy, size_t quantum);
void scheduler_destroy(Scheduler *scheduler);
Ant *scheduler_acquire(Scheduler *scheduler, uint64_t now_us, size_t *granted_quantum);
void scheduler_release(Scheduler *scheduler, Ant *ant, size_t executed, uint64_t now_us);
void scheduler_set_quantum(Scheduler *scheduler, size_t quantum);
void scheduler_set_min_service(Scheduler *scheduler, size_t min_service);
void scheduler_set_token_rate_scale(Scheduler *scheduler, uint32_t scale);
size_t scheduler_get_quantum(const Scheduler *scheduler);
size_t scheduler_get_min_service(const Scheduler *scheduler);
uint64_t scheduler_get_dispatches(const Scheduler *scheduler);
uint64_t scheduler_get_empty_scans(const Scheduler *scheduler);
uint64_t scheduler_get_idle_waits(const Scheduler *scheduler);
uint64_t scheduler_get_granted_instructions(const Scheduler *scheduler);
void scheduler_set_paused(Scheduler *scheduler, bool paused);
bool scheduler_is_paused(const Scheduler *scheduler);
void scheduler_wake_all(Scheduler *scheduler);
void scheduler_stop(Scheduler *scheduler);

int scheduler_double_population(Scheduler *scheduler, World *world, Lfsr32 *rng, uint64_t now_us);
int scheduler_begin_halving(Scheduler *scheduler, size_t target_population);
size_t scheduler_active_population(const Scheduler *scheduler);

double scheduler_fair_credit(const Scheduler *scheduler, size_t ant_index);

#endif
