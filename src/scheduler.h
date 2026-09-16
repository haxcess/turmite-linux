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
    double fair_credit[TURMITE_MAX_ANTS];
    uint64_t last_token_us[TURMITE_MAX_ANTS];
    _Atomic size_t quantum;
} Scheduler;

int scheduler_init(Scheduler *scheduler, AntColony *colony, SchedulerPolicy policy, size_t quantum);
void scheduler_destroy(Scheduler *scheduler);
Ant *scheduler_acquire(Scheduler *scheduler, uint64_t now_us, size_t *granted_quantum);
void scheduler_release(Scheduler *scheduler, Ant *ant, size_t executed, uint64_t now_us);
void scheduler_set_quantum(Scheduler *scheduler, size_t quantum);
size_t scheduler_get_quantum(const Scheduler *scheduler);
uint64_t scheduler_get_dispatches(const Scheduler *scheduler);
void scheduler_set_paused(Scheduler *scheduler, bool paused);
bool scheduler_is_paused(const Scheduler *scheduler);
void scheduler_wake_all(Scheduler *scheduler);
void scheduler_stop(Scheduler *scheduler);

int scheduler_double_population(Scheduler *scheduler, World *world, Lfsr32 *rng, uint64_t now_us);
int scheduler_begin_halving(Scheduler *scheduler, size_t target_population);
size_t scheduler_active_population(const Scheduler *scheduler);

double scheduler_fair_credit(const Scheduler *scheduler, size_t ant_index);

#endif
