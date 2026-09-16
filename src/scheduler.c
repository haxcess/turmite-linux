#include "scheduler.h"

#include <float.h>
#include <time.h>

static uint64_t monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static inline void accrue_tokens(Scheduler *scheduler, size_t index, Ant *ant, uint64_t now_us)
{
    uint64_t last_us = scheduler->last_token_us[index];
    if (now_us <= last_us) return;
    uint64_t elapsed_us = now_us - last_us;
    uint32_t rate = atomic_load_explicit(&ant->token_rate, memory_order_relaxed);
    uint32_t cap_fp = atomic_load_explicit(&ant->token_capacity_fp, memory_order_relaxed);
    uint32_t current_fp = atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed);
    scheduler->last_token_us[index] = now_us;
    if (rate == 0 || current_fp >= cap_fp) return;

    uint64_t room_fp = (uint64_t)cap_fp - current_fp;
    /* All configured buckets fill in well under one second, so clamping the
     * elapsed interval avoids overflow and collapses the old multi-division
     * accrual calculation to one 64-bit division. */
    if (elapsed_us > 1000000ull) elapsed_us = 1000000ull;
    uint64_t add_fp = ((uint64_t)rate * elapsed_us * TOKEN_FP_ONE) / 1000000ull;
    if (add_fp >= room_fp) current_fp = cap_fp;
    else current_fp += (uint32_t)add_fp;
    atomic_store_explicit(&ant->tokens_fp, current_fp, memory_order_relaxed);
}

static inline uint32_t flags_load(const Ant *ant)
{
    return atomic_load_explicit(&ant->flags, memory_order_acquire);
}

static void retire_draining(Ant *ant, AntColony *colony)
{
    uint32_t f = flags_load(ant);
    if ((f & (ANT_F_DRAINING | ANT_F_ENABLED)) == (ANT_F_DRAINING | ANT_F_ENABLED)) {
        double tokens = ant_tokens(ant);
        if (tokens < 1.0) {
            atomic_fetch_or_explicit(&ant->flags, ANT_F_EXPIRED, memory_order_acq_rel);
            atomic_fetch_and_explicit(&ant->flags, ~(uint32_t)ANT_F_ENABLED, memory_order_acq_rel);
            atomic_fetch_and_explicit(&ant->flags, ~(uint32_t)ANT_F_DRAINING, memory_order_acq_rel);
            atomic_fetch_sub_explicit(&colony->active_population, 1u, memory_order_relaxed);
        }
    }
}

static inline bool runnable(const Ant *ant)
{
    uint32_t f = flags_load(ant);
    return (f & ANT_F_ENABLED) && !(f & (ANT_F_LEASED | ANT_F_CLOBBERED | ANT_F_EXPIRED | ANT_F_HALTED));
}

static void reincarnate_clobbered(Scheduler *scheduler, Ant *ant, uint64_t now_us)
{
    uint32_t f = flags_load(ant);
    if ((f & (ANT_F_CLOBBERED | ANT_F_LEASED)) != ANT_F_CLOBBERED) return;
    uint32_t expected = f;
    uint32_t desired = (f & ~ANT_F_CLOBBERED) | ANT_F_ENABLED;
    if (atomic_compare_exchange_strong_explicit(&ant->flags, &expected, desired,
                                                 memory_order_acq_rel, memory_order_relaxed)) {
        ant_mutate_in_place(ant);
        size_t idx = (size_t)(ant - scheduler->colony->ants);
        scheduler->last_token_us[idx] = now_us;
        atomic_fetch_add_explicit(&scheduler->colony->stats[idx].mutations, 1u, memory_order_relaxed);
    }
}

Ant *scheduler_acquire(Scheduler *scheduler, uint64_t now_us, size_t *granted_quantum)
{
    pthread_mutex_lock(&scheduler->lock);

    while (!scheduler->stopping) {
        for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
            reincarnate_clobbered(scheduler, &scheduler->colony->ants[i], now_us);
        }

        if (atomic_load_explicit(&scheduler->paused, memory_order_acquire)) {
            pthread_cond_wait(&scheduler->work_available, &scheduler->lock);
            continue;
        }

        Ant *best = NULL;
        double best_score = -DBL_MAX;

        for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
            Ant *ant = &scheduler->colony->ants[i];
            uint32_t f = flags_load(ant);
            if (!(f & ANT_F_ENABLED) || (f & (ANT_F_LEASED | ANT_F_CLOBBERED | ANT_F_EXPIRED))) continue;

            accrue_tokens(scheduler, i, ant, now_us);
            retire_draining(ant, scheduler->colony);
            if (!runnable(ant)) continue;
            if (atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed) < TOKEN_FP_ONE) continue;

            uint32_t weight = atomic_load_explicit(&ant->weight, memory_order_relaxed);
            if (weight == 0) weight = 1;
            scheduler->fair_credit[i] += (double)weight;

            double score = scheduler->fair_credit[i];
            if (score > best_score || (score == best_score && (!best || i < (size_t)(best - scheduler->colony->ants)))) {
                best = ant;
                best_score = score;
            }
        }

        if (best) {
            size_t grant = scheduler->quantum ? atomic_load_explicit(&scheduler->quantum, memory_order_relaxed) : 1u;
            uint32_t whole_tokens = atomic_load_explicit(&best->tokens_fp, memory_order_relaxed) >> TOKEN_FP_SHIFT;
            if (whole_tokens == 0) {
                pthread_mutex_unlock(&scheduler->lock);
                continue;
            }
            if (grant > (size_t)whole_tokens) grant = (size_t)whole_tokens;
            atomic_fetch_or_explicit(&best->flags, ANT_F_LEASED, memory_order_acq_rel);
            atomic_fetch_add_explicit(&scheduler->dispatches, 1u, memory_order_relaxed);
            if (granted_quantum) *granted_quantum = grant;
            pthread_mutex_unlock(&scheduler->lock);
            return best;
        }

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&scheduler->work_available, &scheduler->lock, &ts);
        now_us = monotonic_us();
    }

    pthread_mutex_unlock(&scheduler->lock);
    return NULL;
}

void scheduler_release(Scheduler *scheduler, Ant *ant, size_t executed, uint64_t now_us)
{
    pthread_mutex_lock(&scheduler->lock);
    const size_t idx = (size_t)(ant - scheduler->colony->ants);
    scheduler->fair_credit[idx] -= (double)executed;
    atomic_fetch_add_explicit(&scheduler->colony->stats[idx].instructions, executed, memory_order_relaxed);
    atomic_fetch_and_explicit(&ant->flags, ~(uint32_t)ANT_F_LEASED, memory_order_release);
    accrue_tokens(scheduler, idx, ant, now_us);
    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
}

uint64_t scheduler_get_dispatches(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->dispatches, memory_order_relaxed) : 0;
}

void scheduler_set_paused(Scheduler *scheduler, bool paused)
{
    pthread_mutex_lock(&scheduler->lock);
    atomic_store_explicit(&scheduler->paused, paused, memory_order_release);
    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
}

bool scheduler_is_paused(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->paused, memory_order_acquire) : false;
}

void scheduler_set_quantum(Scheduler *scheduler, size_t quantum)
{
    atomic_store_explicit(&scheduler->quantum, quantum ? quantum : 1u, memory_order_release);
    scheduler_wake_all(scheduler);
}

size_t scheduler_get_quantum(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->quantum, memory_order_relaxed) : 1u;
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

double scheduler_fair_credit(const Scheduler *scheduler, size_t ant_index)
{
    if (!scheduler || ant_index >= TURMITE_MAX_ANTS) return 0.0;
    pthread_mutex_lock((pthread_mutex_t *)&scheduler->lock);
    double c = scheduler->fair_credit[ant_index];
    pthread_mutex_unlock((pthread_mutex_t *)&scheduler->lock);
    return c;
}

int scheduler_init(Scheduler *scheduler, AntColony *colony, SchedulerPolicy policy, size_t quantum)
{
    if (!scheduler || !colony) return -1;
    scheduler->colony = colony;
    scheduler->policy = policy;
    scheduler->stopping = false;
    atomic_init(&scheduler->dispatches, 0);
    atomic_init(&scheduler->paused, false);
    atomic_init(&scheduler->quantum, quantum ? quantum : 1u);
    const uint64_t now_us = monotonic_us();
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        scheduler->fair_credit[i] = 0.0;
        scheduler->last_token_us[i] = now_us;
    }

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

int scheduler_double_population(Scheduler *scheduler, World *world, Lfsr32 *rng, uint64_t now_us)
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
        if (flags_load(dst) & ANT_F_ENABLED) continue;

        Ant *src = NULL;
        uint32_t best_tokens = 0;
        for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
            Ant *candidate = &scheduler->colony->ants[i];
            uint32_t f = flags_load(candidate);
            if (candidate == dst || !(f & ANT_F_ENABLED) || (f & ANT_F_LEASED)) continue;
            uint32_t tokens = atomic_load_explicit(&candidate->tokens_fp, memory_order_relaxed);
            if (!src || tokens > best_tokens) {
                best_tokens = tokens;
                src = candidate;
            }
        }
        if (!src) break;
        ant_clone(dst, src, world, rng);
        size_t dst_index = slot;
        scheduler->last_token_us[dst_index] = now_us;
        atomic_store_explicit(&scheduler->colony->stats[dst_index].instructions, 0, memory_order_relaxed);
        atomic_store_explicit(&scheduler->colony->stats[dst_index].mutations, 0, memory_order_relaxed);
        ++added;
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
        uint32_t weakest = UINT32_MAX;
        for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
            Ant *ant = &scheduler->colony->ants[i];
            uint32_t f = flags_load(ant);
            if (!(f & ANT_F_ENABLED) || (f & ANT_F_LEASED) || (f & ANT_F_DRAINING)) continue;
            uint32_t tokens = atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed);
            if (!victim || tokens < weakest) {
                weakest = tokens;
                victim = ant;
            }
        }
        if (!victim) break;
        atomic_fetch_or_explicit(&victim->flags, ANT_F_DRAINING, memory_order_acq_rel);
        atomic_store_explicit(&victim->token_rate, 0u, memory_order_relaxed);
        ++marked;
    }

    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->lock);
    return (int)marked;
}
