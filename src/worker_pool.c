#include "worker_pool.h"
#include <stdlib.h>
#include <time.h>

struct WorkerPool {
    pthread_mutex_t lock;
    pthread_cond_t changed;
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

static void *run_worker(void *arg)
{
    WorkerPool *pool = arg;
    pthread_mutex_lock(&pool->lock);
    while (!pool->stopping) {
        size_t slot = 0, grant = 0;
        Ant *ant = scheduler_acquire_group(pool->members, pool->count, now_us(), &slot, &grant);
        if (!ant) {
            struct timespec deadline;
            clock_gettime(CLOCK_MONOTONIC, &deadline);
            deadline.tv_nsec += 1000000L;
            if (deadline.tv_nsec >= 1000000000L) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&pool->changed, &pool->lock, &deadline);
            continue;
        }
        Scheduler *scheduler = pool->members[slot];
        World *world = pool->worlds[slot];
        ++pool->inflight[slot];
        pthread_mutex_unlock(&pool->lock);
        size_t executed = ant_execute_quantum(ant, scheduler->colony, world, grant);
        scheduler_release(scheduler, ant, executed, now_us());
        pthread_mutex_lock(&pool->lock);
        --pool->inflight[slot];
        pthread_cond_broadcast(&pool->changed);
    }
    pthread_mutex_unlock(&pool->lock);
    return NULL;
}

WorkerPool *worker_pool_create(size_t members, size_t workers)
{
    if (!members || !workers || workers > 8) return NULL;
    WorkerPool *pool = calloc(1, sizeof(*pool));
    if (!pool) return NULL;
    pool->count = members;
    pool->members = calloc(members, sizeof(*pool->members));
    pool->worlds = calloc(members, sizeof(*pool->worlds));
    pool->inflight = calloc(members, sizeof(*pool->inflight));
    pool->threads = calloc(workers, sizeof(*pool->threads));
    if (!pool->members || !pool->worlds || !pool->inflight || !pool->threads) goto fail;
    if (pthread_mutex_init(&pool->lock, NULL) != 0) goto fail;
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) goto fail_mutex;
    int error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!error) error = pthread_cond_init(&pool->changed, &attr);
    pthread_condattr_destroy(&attr);
    if (error) goto fail_mutex;
    for (size_t i = 0; i < workers; ++i) {
        if (pthread_create(&pool->threads[i], NULL, run_worker, pool) != 0) {
            worker_pool_destroy(pool);
            return NULL;
        }
        ++pool->started;
    }
    return pool;
fail_mutex:
    pthread_mutex_destroy(&pool->lock);
fail:
    free(pool->threads); free(pool->inflight); free(pool->worlds); free(pool->members); free(pool);
    return NULL;
}

void worker_pool_enable(WorkerPool *pool, size_t slot, Scheduler *scheduler, World *world)
{
    pthread_mutex_lock(&pool->lock);
    pool->worlds[slot] = world;
    pool->members[slot] = scheduler;
    pthread_cond_broadcast(&pool->changed);
    pthread_mutex_unlock(&pool->lock);
}

void worker_pool_suspend(WorkerPool *pool, size_t slot)
{
    if (!pool) return;
    pthread_mutex_lock(&pool->lock);
    pool->members[slot] = NULL;
    while (pool->inflight[slot]) pthread_cond_wait(&pool->changed, &pool->lock);
    pool->worlds[slot] = NULL;
    pthread_mutex_unlock(&pool->lock);
}

void worker_pool_destroy(WorkerPool *pool)
{
    if (!pool) return;
    pthread_mutex_lock(&pool->lock);
    pool->stopping = true;
    pthread_cond_broadcast(&pool->changed);
    pthread_mutex_unlock(&pool->lock);
    for (size_t i = 0; i < pool->started; ++i) pthread_join(pool->threads[i], NULL);
    pthread_cond_destroy(&pool->changed);
    pthread_mutex_destroy(&pool->lock);
    free(pool->threads); free(pool->inflight); free(pool->worlds); free(pool->members); free(pool);
}
