#ifndef TURMITE_SCHEDULER_H
#define TURMITE_SCHEDULER_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "ant.h"

/* The scheduler is a token-gated weighted-fair dispatcher. Ants accumulate
 * execution credit over wall time; workers lease runnable ants for bounded
 * quanta, so logical ants outnumber physical worker threads. */
typedef enum {
    SCHED_WFQ = 0
} SchedulerPolicy;

typedef struct {
    /* Linux synchronization protects leases and fair-credit bookkeeping. */
    pthread_mutex_t lock;
    pthread_cond_t work_available;
    AntColony *colony;
    SchedulerPolicy policy;
    bool stopping;
    _Atomic bool paused;

    /* Profiling counters are observational and do not affect scheduling. */
    _Atomic uint64_t dispatches;
    _Atomic uint64_t empty_scans;
    _Atomic uint64_t idle_waits;
    _Atomic uint64_t granted_instructions;

    /* fair_credit rises by ant weight while eligible and falls by work done. */
    double fair_credit[TURMITE_MAX_ANTS];
    uint64_t last_token_us[TURMITE_MAX_ANTS];

    _Atomic size_t quantum;
    _Atomic size_t min_service;
    _Atomic uint32_t token_rate_scale;
    _Atomic uint32_t token_rate_divisor;
} Scheduler;

int scheduler_init(Scheduler *scheduler, AntColony *colony, SchedulerPolicy policy, size_t quantum);
void scheduler_destroy(Scheduler *scheduler);
Ant *scheduler_acquire(Scheduler *scheduler, uint64_t now_us, size_t *granted_quantum);
void scheduler_release(Scheduler *scheduler, Ant *ant, size_t executed, uint64_t now_us);
void scheduler_set_quantum(Scheduler *scheduler, size_t quantum);
void scheduler_set_min_service(Scheduler *scheduler, size_t min_service);
void scheduler_set_token_rate_scale(Scheduler *scheduler, uint32_t scale);
void scheduler_set_token_rate_divisor(Scheduler *scheduler, uint32_t divisor);
uint32_t scheduler_get_token_rate_divisor(const Scheduler *scheduler);
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

/* Population changes are expressed as scheduler policy: doubling clones healthy
 * ants into free slots, while halving marks weak ants to drain naturally. */
int scheduler_double_population(Scheduler *scheduler, World *world, Lfsr32 *rng, uint64_t now_us);
int scheduler_begin_halving(Scheduler *scheduler, size_t target_population);
size_t scheduler_active_population(const Scheduler *scheduler);

double scheduler_fair_credit(const Scheduler *scheduler, size_t ant_index);

#endif
