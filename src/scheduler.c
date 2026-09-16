#include "scheduler.h"

#include <float.h>
#include <time.h>

static void accrue_tokens(Ant *ant, double now)
{
    double elapsed = now - ant->last_token_time;
    if (elapsed <= 0.0) return;

    double tokens = atomic_load_explicit(&ant->sched.tokens, memory_order_relaxed);
    tokens += ant->sched.token_rate * elapsed;
    if (tokens > ant->sched.token_capacity) tokens = ant->sched.token_capacity;
    atomic_store_explicit(&ant->sched.tokens, tokens, memory_order_relaxed);
    ant->last_token_time = now;
}

int scheduler_init(Scheduler *scheduler, AntColony *colony, SchedulerPolicy policy, size_t quantum)
{
    if (!scheduler || !colony) return -1;
    scheduler->colony = colony;
    scheduler->policy = policy;
    scheduler->stopping = false;
    atomic_init(&scheduler->dispatches, 0);
    atomic_init(&scheduler->paused, false);
    scheduler->quantum = quantum ? quantum : 1;
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) scheduler->fair_credit[i] = 0.0;

    if (pthread_mutex_init(&scheduler->lock, NULL) != 0) return -1;
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) {
        pthread_mutex_destroy(&scheduler->lock);
        return -1;
    }
    (void)pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (pthread_cond_init(&scheduler->work_available, &attr) != 0) {
        pthread_condattr_destroy(&attr);
        pthread_mutex_destroy(&scheduler->lock);
        return -1;
    }
    pthread_condattr_destroy(&attr);
    return 0;
}

void scheduler_destroy(Scheduler *scheduler)
{
    if (!scheduler) return;
    pthread_cond_destroy(&scheduler->work_available);
    pthread_mutex_destroy(&scheduler->lock);
}

static bool runnable(const Ant *ant)
{
    return atomic_load_explicit(&ant->enabled, memory_order_acquire) &&
           !atomic_load_explicit(&ant->leased, memory_order_acquire) &&
           !atomic_load_explicit(&ant->clobbered, memory_order_acquire) &&
           !atomic_load_explicit(&ant->expired, memory_order_acquire);
}

static void retire_draining(Ant *ant, AntColony *colony)
{
    if (atomic_load_explicit(&ant->draining, memory_order_relaxed)) {
        double tokens = atomic_load_explicit(&ant->sched.tokens, memory_order_relaxed);
        if (tokens < 1.0) {
            atomic_store_explicit(&ant->enabled, false, memory_order_release);
            atomic_store_explicit(&ant->expired, true, memory_order_release);
            atomic_store_explicit(&ant->draining, false, memory_order_release);
            atomic_fetch_sub_explicit(&colony->active_population, 1u, memory_order_relaxed);
        }
    }
}

Ant *scheduler_acquire(Scheduler *scheduler, double now, Lfsr32 *rng)
{
    pthread_mutex_lock(&scheduler->lock);

    while (!scheduler->stopping) {
        /* A clobbered ant that is not leased can be reincarnated by whichever worker next needs work. */
        for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
            Ant *ant = &scheduler->colony->ants[i];
            if (atomic_load_explicit(&ant->clobbered, memory_order_acquire) &&
                !atomic_load_explicit(&ant->leased, memory_order_acquire)) {
                bool expected = true;
                if (atomic_compare_exchange_strong_explicit(
                        &ant->clobbered, &expected, false,
                        memory_order_acq_rel, memory_order_relaxed)) {
                    ant_mutate_in_place(ant, rng, now);
                }
            }
        }

        if (atomic_load_explicit(&scheduler->paused, memory_order_acquire)) {
            pthread_cond_wait(&scheduler->work_available, &scheduler->lock);
            continue;
        }

        Ant *best = NULL;
        double best_score = -DBL_MAX;

        /* Weighted-fair/deficit-like selection. Credit accumulates while an ant is runnable;
           actual service consumes credit. This is intentionally compact rather than RFC-exact WFQ. */
        for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
            Ant *ant = &scheduler->colony->ants[i];
            if (!atomic_load_explicit(&ant->enabled, memory_order_acquire)) continue;
            if (atomic_load_explicit(&ant->leased, memory_order_acquire)) continue;
            if (atomic_load_explicit(&ant->clobbered, memory_order_acquire)) continue;
            if (atomic_load_explicit(&ant->expired, memory_order_acquire)) continue;

            accrue_tokens(ant, now);
            retire_draining(ant, scheduler->colony);
            if (!runnable(ant)) continue;

            double tokens = atomic_load_explicit(&ant->sched.tokens, memory_order_relaxed);
            if (tokens < 1.0) continue;

            uint32_t weight = atomic_load_explicit(&ant->sched.weight, memory_order_relaxed);
            if (weight == 0) weight = 1;
            scheduler->fair_credit[i] += (double)weight;

            double score = scheduler->fair_credit[i];
            if (score > best_score || (score == best_score && (!best || ant->id < best->id))) {
                best = ant;
                best_score = score;
            }
        }

        if (best) {
            atomic_store_explicit(&best->leased, true, memory_order_release);
            atomic_fetch_add_explicit(&scheduler->dispatches, 1u, memory_order_relaxed);
            pthread_mutex_unlock(&scheduler->lock);
            return best;
        }

        /* No eligible ant. Sleep until signalled or until enough time has passed for tokens to accumulate. */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&scheduler->work_available, &scheduler->lock, &ts);
    }

    pthread_mutex_unlock(&scheduler->lock);
    return NULL;
}

void scheduler_release(Scheduler *scheduler, Ant *ant, size_t executed, double now)
{
    pthread_mutex_lock(&scheduler->lock);
    size_t idx = ant->id % TURMITE_MAX_ANTS;
    scheduler->fair_credit[idx] -= (double)executed;
    atomic_store_explicit(&ant->leased, false, memory_order_release);
    accrue_tokens(ant, now);
    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
}

void scheduler_set_paused(Scheduler *scheduler, bool paused)
{
    pthread_mutex_lock(&scheduler->lock);
    atomic_store_explicit(&scheduler->paused, paused, memory_order_release);
    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
}

void scheduler_set_quantum(Scheduler *scheduler, size_t quantum)
{
    pthread_mutex_lock(&scheduler->lock);
    scheduler->quantum = quantum ? quantum : 1;
    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
}

size_t scheduler_get_quantum(Scheduler *scheduler)
{
    pthread_mutex_lock(&scheduler->lock);
    size_t q = scheduler->quantum;
    pthread_mutex_unlock(&scheduler->lock);
    return q;
}

void scheduler_wake_all(Scheduler *scheduler)
{
    pthread_mutex_lock(&scheduler->lock);
    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
}

void scheduler_stop(Scheduler *scheduler)
{
    pthread_mutex_lock(&scheduler->lock);
    scheduler->stopping = true;
    atomic_store_explicit(&scheduler->paused, false, memory_order_release);
    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
}

size_t scheduler_active_population(const Scheduler *scheduler)
{
    return atomic_load_explicit(&scheduler->colony->active_population, memory_order_relaxed);
}

int scheduler_double_population(Scheduler *scheduler, World *world, Lfsr32 *rng, double now)
{
    pthread_mutex_lock(&scheduler->lock);
    size_t current = atomic_load_explicit(&scheduler->colony->active_population, memory_order_relaxed);
    if (current >= TURMITE_MAX_ANTS) {
        pthread_mutex_unlock(&scheduler->lock);
        return 0;
    }
    size_t target = current * 2;
    if (target > TURMITE_MAX_ANTS) target = TURMITE_MAX_ANTS;

    size_t added = 0;
    for (size_t slot = 0; slot < TURMITE_MAX_ANTS && current + added < target; ++slot) {
        Ant *dst = &scheduler->colony->ants[slot];
        if (atomic_load_explicit(&dst->enabled, memory_order_relaxed)) continue;

        /* Choose a live source. Prefer an existing ant with the most tokens so cloning has visible inheritance. */
        Ant *src = NULL;
        double best_tokens = -1.0;
        for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
            Ant *candidate = &scheduler->colony->ants[i];
            if (candidate == dst || !atomic_load_explicit(&candidate->enabled, memory_order_relaxed) ||
                atomic_load_explicit(&candidate->leased, memory_order_relaxed)) continue;
            double tokens = atomic_load_explicit(&candidate->sched.tokens, memory_order_relaxed);
            if (tokens > best_tokens) {
                best_tokens = tokens;
                src = candidate;
            }
        }
        if (!src) break;
        ant_clone(dst, src, world, rng, now);
        added++;
    }

    atomic_store_explicit(&scheduler->colony->active_population, current + added, memory_order_release);
    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
    return (int)added;
}

int scheduler_begin_halving(Scheduler *scheduler, size_t target_population)
{
    pthread_mutex_lock(&scheduler->lock);
    size_t current = atomic_load_explicit(&scheduler->colony->active_population, memory_order_relaxed);
    if (target_population >= current) {
        pthread_mutex_unlock(&scheduler->lock);
        return 0;
    }

    size_t need = current - target_population;
    size_t marked = 0;

    while (marked < need) {
        Ant *victim = NULL;
        double weakest = DBL_MAX;

        for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
            Ant *ant = &scheduler->colony->ants[i];
            if (!atomic_load_explicit(&ant->enabled, memory_order_relaxed)) continue;
            if (atomic_load_explicit(&ant->leased, memory_order_relaxed)) continue;
            if (atomic_load_explicit(&ant->draining, memory_order_relaxed)) continue;
            double tokens = atomic_load_explicit(&ant->sched.tokens, memory_order_relaxed);
            if (tokens < weakest) {
                weakest = tokens;
                victim = ant;
            }
        }

        if (!victim) break;
        atomic_store_explicit(&victim->draining, true, memory_order_release);
        victim->sched.token_rate = 0.0;
        marked++;
    }

    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
    return (int)marked;
}
