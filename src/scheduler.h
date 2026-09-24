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
/* Pause begins once the losing worker has returned its lease. */
#define COLLISION_PAUSE_US UINT64_C(50000)

typedef enum {
    SCHED_WFQ = 0
} SchedulerPolicy;

/* Shared wake event. Lock order: scheduler -> event; never hold event.lock
 * while acquiring a scheduler or pool lock. Registration is scheduler-owned. */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    _Atomic unsigned waiters;
    uint64_t generation;
} SchedulerWake;

void scheduler_wake_event(SchedulerWake *wake);

typedef struct {
    /* Linux synchronization protects leases and fair-credit bookkeeping. */
    pthread_mutex_t lock;
    pthread_cond_t work_available;
    AntColony *colony;
    SchedulerPolicy policy;
    SchedulerWake *wake; /* registered pool event, protected by lock */
    uint64_t next_wake_us; /* next token/recovery deadline; UINT64_MAX = event only */
    bool collision_mutation; /* configure before workers start; default false */
    bool stopping;
    bool draining; /* lock-owned: debug quit forbids all new token creation */
    _Atomic bool paused;

    /* Profiling counters are observational and do not affect scheduling. */
    _Atomic uint64_t dispatches;
    _Atomic uint64_t empty_scans;
    _Atomic uint64_t idle_waits;
    _Atomic uint64_t granted_instructions;

    /* fair_credit rises by ant weight while eligible and falls by work done. */
    double fair_credit[TURMITE_MAX_ANTS];
    uint64_t last_token_us[TURMITE_MAX_ANTS];
    uint64_t recovery_at_us[TURMITE_MAX_ANTS]; /* scheduler-lock owned */

    _Atomic size_t quantum;
    _Atomic size_t min_service;
    _Atomic uint32_t token_rate_scale;
    _Atomic uint32_t token_rate_divisor;
} Scheduler;

int scheduler_init(Scheduler *scheduler, AntColony *colony, SchedulerPolicy policy, size_t quantum);
void scheduler_destroy(Scheduler *scheduler);
Ant *scheduler_acquire(Scheduler *scheduler, uint64_t now_us, size_t *granted_quantum);
/* Nonblocking global selection; caller keeps distinct member objects alive.
 * NULL members are skipped. Never mix group and standalone workers on a member. */
Ant *scheduler_acquire_group(Scheduler *const *members, size_t count,
                             uint64_t now_us, size_t *selected, size_t *grant);
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
void scheduler_wake_all(Scheduler *scheduler);
void scheduler_stop(Scheduler *scheduler);
/* Resume paused work, stop refill, consume final partial batches. Idempotent. */
void scheduler_begin_drain(Scheduler *scheduler);
/* True only once no enabled ant or outstanding lease can execute more work. */
bool scheduler_drain_complete(Scheduler *scheduler);

/* Add exactly one independent random library ant. Returns 1 on success, 0
 * when stopped/draining, full, or unable to claim a cell. Safe with active leases. */
int scheduler_spawn_ant(Scheduler *scheduler, World *world, Lfsr32 *rng, uint64_t now_us);
/* Culling marks weak ants to drain naturally. */
int scheduler_begin_culling(Scheduler *scheduler, size_t target_population);
size_t scheduler_active_population(const Scheduler *scheduler);

#endif
