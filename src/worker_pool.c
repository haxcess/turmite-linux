#include "worker_pool.h"
#include <stdlib.h>
#include <time.h>

struct WorkerPool {
    pthread_mutex_t lock;
    pthread_cond_t changed; /* suspension waits for outstanding leases */
    SchedulerWake wake;
    Scheduler **members;
    World **worlds;
    size_t *inflight;
    pthread_t *threads;
    size_t count, started;
    bool stopping;
};

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void unregister_waiter(SchedulerWake *wake)
{
    atomic_fetch_sub_explicit(&wake->waiters, 1u, memory_order_release);
}

static void *run_worker(void *arg)
{
    WorkerPool *pool = arg;
    bool registered = false;
    uint64_t generation = 0;
    pthread_mutex_lock(&pool->lock);
    while (!pool->stopping) {
        size_t slot = 0, grant = 0;
        Ant *ant = scheduler_acquire_group(pool->members, pool->count, now_us(), &slot, &grant);
        if (!ant) {
            uint64_t deadline = UINT64_MAX;
            for (size_t i = 0; i < pool->count; ++i) if (pool->members[i]) {
                /* Group selection just published these deadlines under the
                 * scheduler locks; pool.lock serializes other group scans. */
                uint64_t next = pool->members[i]->next_wake_us;
                if (next < deadline) deadline = next;
            }
            pthread_mutex_unlock(&pool->lock);
            pthread_mutex_lock(&pool->wake.lock);
            if (!registered) {
                atomic_fetch_add_explicit(&pool->wake.waiters, 1u, memory_order_release);
                registered = true;
                /* Always rescan after registration. A state change before
                 * registration need not signal an event with no waiters. */
            } else if (generation == pool->wake.generation) {
                if (deadline == UINT64_MAX) {
                    pthread_cond_wait(&pool->wake.changed, &pool->wake.lock);
                } else {
                    uint64_t earliest = now_us() + 1000u;
                    if (deadline < earliest) deadline = earliest;
                    struct timespec ts = { (time_t)(deadline / 1000000u),
                                          (long)(deadline % 1000000u) * 1000L };
                    pthread_cond_timedwait(&pool->wake.changed, &pool->wake.lock, &ts);
                }
            }
            generation = pool->wake.generation;
            pthread_mutex_unlock(&pool->wake.lock);
            pthread_mutex_lock(&pool->lock);
            continue;
        }
        if (registered) { unregister_waiter(&pool->wake); registered = false; }
        Scheduler *scheduler = pool->members[slot];
        World *world = pool->worlds[slot];
        ++pool->inflight[slot];
        pthread_mutex_unlock(&pool->lock);
        size_t executed = ant_execute_quantum(ant, scheduler->colony, world, grant);
        scheduler_release(scheduler, ant, executed, now_us());
        pthread_mutex_lock(&pool->lock);
        if (--pool->inflight[slot] == 0 && !pool->members[slot])
            pthread_cond_broadcast(&pool->changed);
    }
    if (registered) unregister_waiter(&pool->wake);
    pthread_mutex_unlock(&pool->lock);
    return NULL;
}

WorkerPool *worker_pool_create(size_t members, size_t workers)
{
    if (!members || !workers || workers > 8) return NULL;
    WorkerPool *pool = calloc(1, sizeof(*pool));
    if (!pool) return NULL;
    pool->count = members;
    atomic_init(&pool->wake.waiters, 0);
    pool->members = calloc(members, sizeof(*pool->members));
    pool->worlds = calloc(members, sizeof(*pool->worlds));
    pool->inflight = calloc(members, sizeof(*pool->inflight));
    pool->threads = calloc(workers, sizeof(*pool->threads));
    if (!pool->members || !pool->worlds || !pool->inflight || !pool->threads) goto fail;
    if (pthread_mutex_init(&pool->lock, NULL) != 0) goto fail;
    if (pthread_mutex_init(&pool->wake.lock, NULL) != 0) goto fail_mutex;
    if (pthread_cond_init(&pool->changed, NULL) != 0) goto fail_wake_mutex;
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) goto fail_changed;
    int error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!error) error = pthread_cond_init(&pool->wake.changed, &attr);
    pthread_condattr_destroy(&attr);
    if (error) goto fail_changed;
    for (size_t i = 0; i < workers; ++i) {
        if (pthread_create(&pool->threads[i], NULL, run_worker, pool) != 0) {
            worker_pool_destroy(pool);
            return NULL;
        }
        ++pool->started;
    }
    return pool;
fail_changed:
    pthread_cond_destroy(&pool->changed);
fail_wake_mutex:
    pthread_mutex_destroy(&pool->wake.lock);
fail_mutex:
    pthread_mutex_destroy(&pool->lock);
fail:
    free(pool->threads); free(pool->inflight); free(pool->worlds); free(pool->members); free(pool);
    return NULL;
}

void worker_pool_enable(WorkerPool *pool, size_t slot, Scheduler *scheduler, World *world)
{
    pthread_mutex_lock(&pool->lock);
    pthread_mutex_lock(&scheduler->lock);
    scheduler->wake = &pool->wake;
    pthread_mutex_unlock(&scheduler->lock);
    pool->worlds[slot] = world;
    pool->members[slot] = scheduler;
    scheduler_wake_event(&pool->wake);
    pthread_mutex_unlock(&pool->lock);
}

void worker_pool_suspend(WorkerPool *pool, size_t slot)
{
    if (!pool) return;
    pthread_mutex_lock(&pool->lock);
    Scheduler *scheduler = pool->members[slot];
    pool->members[slot] = NULL;
    if (scheduler) {
        pthread_mutex_lock(&scheduler->lock);
        scheduler->wake = NULL;
        pthread_mutex_unlock(&scheduler->lock);
    }
    scheduler_wake_event(&pool->wake);
    while (pool->inflight[slot]) pthread_cond_wait(&pool->changed, &pool->lock);
    pool->worlds[slot] = NULL;
    pthread_mutex_unlock(&pool->lock);
}

void worker_pool_destroy(WorkerPool *pool)
{
    if (!pool) return;
    pthread_mutex_lock(&pool->lock);
    pool->stopping = true;
    for (size_t i = 0; i < pool->count; ++i) if (pool->members[i]) {
        pthread_mutex_lock(&pool->members[i]->lock);
        pool->members[i]->wake = NULL;
        pthread_mutex_unlock(&pool->members[i]->lock);
    }
    scheduler_wake_event(&pool->wake);
    pthread_mutex_unlock(&pool->lock);
    for (size_t i = 0; i < pool->started; ++i) pthread_join(pool->threads[i], NULL);
    pthread_cond_destroy(&pool->wake.changed);
    pthread_cond_destroy(&pool->changed);
    pthread_mutex_destroy(&pool->wake.lock);
    pthread_mutex_destroy(&pool->lock);
    free(pool->threads); free(pool->inflight); free(pool->worlds); free(pool->members); free(pool);
}
