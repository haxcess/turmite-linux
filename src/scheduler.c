#include "scheduler.h"

#include <float.h>
#include <time.h>

/* Linux implementation of the scheduling policy. Token accrual determines
 * when an ant may run; weighted fair credit determines which eligible ant gets
 * the next worker lease. The mutex protects lease/credit decisions, while ant
 * execution itself happens outside the lock. */

static uint64_t monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

/* Lazily refill one ant's token bucket from elapsed wall time. Q16 arithmetic
 * preserves sub-token accumulation while the bucket capacity bounds bursts. */
static inline void accrue_tokens(Scheduler *scheduler, size_t index, Ant *ant, uint64_t now_us)
{
    if (scheduler->draining) return;
    uint64_t last_us = scheduler->last_token_us[index];
    if (now_us <= last_us) return;
    uint64_t elapsed_us = now_us - last_us;
    uint32_t rate = atomic_load_explicit(&ant->token_rate, memory_order_relaxed);
    const uint32_t rate_scale = atomic_load_explicit(&scheduler->token_rate_scale, memory_order_relaxed);
    const uint32_t rate_divisor = atomic_load_explicit(&scheduler->token_rate_divisor, memory_order_relaxed);
    uint64_t effective_rate = (uint64_t)rate * (uint64_t)(rate_scale ? rate_scale : 1u);
    uint32_t cap_fp = atomic_load_explicit(&ant->token_capacity_fp, memory_order_relaxed);
    uint32_t current_fp = atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed);
    scheduler->last_token_us[index] = now_us;
    if (effective_rate == 0 || current_fp >= cap_fp) return;

    uint64_t room_fp = (uint64_t)cap_fp - current_fp;
    const uint64_t divisor = (uint64_t)(rate_divisor ? rate_divisor : 1u);
    uint64_t add_fp;
    /* Fast integer path covers normal dispatches and collision pauses. Slow
     * universes can sleep for seconds: do not truncate their elapsed time.
     * The cold floating path also avoids overflow at extreme rate scales. */
    if (effective_rate <= UINT32_MAX && elapsed_us <= UINT16_MAX) {
        add_fp = (effective_rate * elapsed_us * TOKEN_FP_ONE) / (1000000ull * divisor);
    } else {
        long double earned = (long double)effective_rate * elapsed_us * TOKEN_FP_ONE /
                             (1000000.0L * divisor);
        add_fp = earned >= room_fp ? room_fp : (uint64_t)earned;
    }
    if (add_fp >= room_fp) current_fp = cap_fp;
    else current_fp += (uint32_t)add_fp;
    /* Retain sub-Q16 elapsed time instead of losing it on frequent scans. */
    if (!add_fp) scheduler->last_token_us[index] = last_us;
    atomic_store_explicit(&ant->tokens_fp, current_fp, memory_order_relaxed);
}

static inline uint32_t flags_load(const Ant *ant)
{
    return atomic_load_explicit(&ant->flags, memory_order_acquire);
}

/* Halving is gradual: a draining ant stops earning tokens and retires only
 * after its existing budget has been consumed. */
static void retire_draining(Ant *ant, AntColony *colony)
{
    uint32_t f = flags_load(ant);
    if ((f & (ANT_F_DRAINING | ANT_F_ENABLED)) == (ANT_F_DRAINING | ANT_F_ENABLED)) {
        if (atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed) < TOKEN_FP_ONE) {
            atomic_fetch_or_explicit(&ant->flags, ANT_F_EXPIRED, memory_order_acq_rel);
            atomic_fetch_and_explicit(&ant->flags, ~(uint32_t)ANT_F_ENABLED, memory_order_acq_rel);
            atomic_fetch_and_explicit(&ant->flags, ~(uint32_t)ANT_F_DRAINING, memory_order_acq_rel);
            const size_t index = (size_t)(ant - colony->ants);
            ant_release_occupancy(ant, colony);
            atomic_fetch_and_explicit(&colony->enabled_mask, ~(UINT32_C(1) << index), memory_order_release);
            atomic_fetch_sub_explicit(&colony->active_population, 1u, memory_order_relaxed);
        }
    }
}

static inline bool runnable(const Ant *ant)
{
    uint32_t f = flags_load(ant);
    return (f & ANT_F_ENABLED) && !(f & (ANT_F_LEASED | ANT_F_CLOBBERED | ANT_F_EXPIRED | ANT_F_HALTED | ANT_F_DISPLACED | ANT_F_WAITING));
}

/* Only idle ants can have their private rule edited. Clear the old event
 * before reclaiming occupancy: a concurrent new collision then stays pending.
 * WAITING prevents repeated mutations while a winner still occupies the cell. */
static void recover_ant(Scheduler *scheduler, Ant *ant, uint64_t now_us)
{
    uint32_t f = flags_load(ant);
    if (!(f & ANT_F_ENABLED) || (f & ANT_F_LEASED)) return;
    const size_t index = (size_t)(ant - scheduler->colony->ants);
    if (f & (ANT_F_CLOBBERED | ANT_F_HALTED)) {
        if (!(f & ANT_F_HALTED)) {
            if (!scheduler->recovery_at_us[index]) {
                scheduler->recovery_at_us[index] = now_us + COLLISION_PAUSE_US;
                return;
            }
            if (now_us < scheduler->recovery_at_us[index]) return;
        }
        uint32_t desired = (f & ~(uint32_t)(ANT_F_CLOBBERED | ANT_F_HALTED | ANT_F_DISPLACED)) | ANT_F_WAITING;
        if (!atomic_compare_exchange_strong_explicit(&ant->flags, &f, desired,
                                                     memory_order_acq_rel, memory_order_relaxed)) return;
        scheduler->recovery_at_us[index] = 0;
        ant_release_occupancy(ant, scheduler->colony);
        if (f & ANT_F_HALTED) {
            const uint32_t remaining = atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed);
            ant_rebirth_random(ant, scheduler->colony);
            if (scheduler->draining) {
                atomic_store_explicit(&ant->tokens_fp, remaining, memory_order_relaxed);
                atomic_store_explicit(&ant->token_rate, 0, memory_order_relaxed);
                atomic_fetch_or_explicit(&ant->flags, ANT_F_DRAINING, memory_order_relaxed);
            }
            scheduler->last_token_us[index] = now_us;
            scheduler->fair_credit[index] = 0;
        } else if (scheduler->collision_mutation) {
            ant_mutate_in_place(ant, scheduler->colony);
            atomic_fetch_add_explicit(&scheduler->colony->stats[index].mutations, 1u, memory_order_relaxed);
        }
    }
    f = flags_load(ant);
    if ((f & ANT_F_WAITING) && !(f & (ANT_F_CLOBBERED | ANT_F_HALTED)) &&
        ant_try_reclaim_position(ant, scheduler->colony)) {
        /* Never clear a fresh collision reported during the occupancy claim. */
        atomic_fetch_and_explicit(&ant->flags, ~(uint32_t)ANT_F_WAITING, memory_order_release);
    }
}

/* Configuration changes and lease release wake sleeping standalone/pool workers.
 * No event mutex or condition-variable traffic when the pool is busy. */
void scheduler_wake_event(SchedulerWake *wake)
{
    if (!wake || !atomic_load_explicit(&wake->waiters, memory_order_acquire)) return;
    pthread_mutex_lock(&wake->lock);
    ++wake->generation;
    pthread_cond_broadcast(&wake->changed);
    pthread_mutex_unlock(&wake->lock);
}

static void notify_locked(Scheduler *scheduler)
{
    pthread_cond_broadcast(&scheduler->work_available);
    scheduler_wake_event(scheduler->wake);
}

static void earlier_deadline(Scheduler *scheduler, uint64_t deadline)
{
    if (deadline < scheduler->next_wake_us) scheduler->next_wake_us = deadline;
}

/* Called only for an otherwise runnable ant below its service threshold. */
static void token_deadline(Scheduler *scheduler, const Ant *ant, size_t index,
                           uint32_t tokens, size_t required)
{
    const uint64_t rate = (uint64_t)atomic_load_explicit(&ant->token_rate, memory_order_relaxed) *
        atomic_load_explicit(&scheduler->token_rate_scale, memory_order_relaxed);
    if (!rate || scheduler->draining) return;
    const uint32_t divisor = atomic_load_explicit(&scheduler->token_rate_divisor, memory_order_relaxed);
    long double delay = (long double)((uint64_t)required * TOKEN_FP_ONE - tokens) *
        1000000.0L * divisor / ((long double)rate * TOKEN_FP_ONE);
    uint64_t last = scheduler->last_token_us[index];
    if (delay >= (long double)(UINT64_MAX - last)) return;
    uint64_t us = (uint64_t)delay;
    if ((long double)us < delay) ++us;
    earlier_deadline(scheduler, last + us);
}

/* Caller holds this scheduler's lock. Recover every enabled ant before credit
 * selection so collision ownership retains the original ordering. */
static Ant *candidate_locked(Scheduler *scheduler, uint64_t now_us, double *score)
{
    scheduler->next_wake_us = UINT64_MAX;
    if (scheduler->stopping) return NULL;
    uint32_t enabled = atomic_load_explicit(&scheduler->colony->enabled_mask, memory_order_acquire);
    for (uint32_t bits = enabled; bits; bits &= bits - 1u) {
        size_t i = (size_t)__builtin_ctz(bits);
        Ant *ant = &scheduler->colony->ants[i];
        uint32_t f = flags_load(ant);
        if (f & ANT_F_LEASED) continue;
        if (scheduler->draining) retire_draining(ant, scheduler->colony);
        if (f & (ANT_F_CLOBBERED | ANT_F_HALTED | ANT_F_WAITING))
            recover_ant(scheduler, ant, now_us);
        if (scheduler->recovery_at_us[i] > now_us)
            earlier_deadline(scheduler, scheduler->recovery_at_us[i]);
        else if (flags_load(ant) & (ANT_F_WAITING | ANT_F_CLOBBERED | ANT_F_HALTED))
            /* A later recovery in this scan can free a waiting ant's cell. */
            earlier_deadline(scheduler, now_us + 1000u);
    }
    if (atomic_load_explicit(&scheduler->paused, memory_order_acquire)) {
        scheduler->next_wake_us = UINT64_MAX;
        return NULL;
    }
    Ant *best = NULL;
    double best_score = -DBL_MAX;
    size_t q = atomic_load_explicit(&scheduler->quantum, memory_order_relaxed);
    size_t minimum = atomic_load_explicit(&scheduler->min_service, memory_order_relaxed);
    if (!q) q = 1;
    if (!minimum) minimum = 1;
    if (minimum > q) minimum = q;

    for (uint32_t bits = enabled; bits; bits &= bits - 1u) {
        size_t i = (size_t)__builtin_ctz(bits);
        Ant *ant = &scheduler->colony->ants[i];
        uint32_t f = flags_load(ant);
        if (!(f & ANT_F_ENABLED) || (f & (ANT_F_LEASED | ANT_F_CLOBBERED | ANT_F_EXPIRED | ANT_F_DISPLACED))) continue;
        accrue_tokens(scheduler, i, ant, now_us);
        if (f & ANT_F_DRAINING) retire_draining(ant, scheduler->colony);
        if (!runnable(ant)) continue;
        uint32_t tokens = atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed);
        size_t whole = tokens >> TOKEN_FP_SHIFT;
        size_t capacity = atomic_load_explicit(&ant->token_capacity_fp, memory_order_relaxed) >> TOKEN_FP_SHIFT;
        size_t required = minimum < capacity ? minimum : capacity;
        if ((f & ANT_F_DRAINING) || !required) required = 1;
        if (whole < required) continue;
        uint32_t weight = atomic_load_explicit(&ant->weight, memory_order_relaxed);
        scheduler->fair_credit[i] += (double)(weight ? weight : 1);
        double credit = scheduler->fair_credit[i];
        /* Ascending bit order already gives the lower-index tie break. */
        if (credit > best_score) { best = ant; best_score = credit; }
    }
    if (best) {
        if (score) *score = best_score;
    } else {
        /* Deadline arithmetic belongs to the idle path, not every dispatch. */
        for (uint32_t bits = enabled; bits; bits &= bits - 1u) {
            size_t i = (size_t)__builtin_ctz(bits);
            Ant *ant = &scheduler->colony->ants[i];
            if (!runnable(ant)) continue;
            uint32_t tokens = atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed);
            size_t capacity = atomic_load_explicit(&ant->token_capacity_fp, memory_order_relaxed) >> TOKEN_FP_SHIFT;
            size_t required = minimum < capacity ? minimum : capacity;
            if ((flags_load(ant) & ANT_F_DRAINING) || !required) required = 1;
            if ((tokens >> TOKEN_FP_SHIFT) < required)
                token_deadline(scheduler, ant, i, tokens, required);
        }
    }
    return best;
}

static Ant *lease_locked(Scheduler *scheduler, Ant *ant, size_t *granted_quantum)
{
    size_t grant = atomic_load_explicit(&scheduler->quantum, memory_order_relaxed);
    if (!grant) grant = 1;
    size_t whole = atomic_load_explicit(&ant->tokens_fp, memory_order_relaxed) >> TOKEN_FP_SHIFT;
    if (grant > whole) grant = whole;
    atomic_fetch_or_explicit(&ant->flags, ANT_F_LEASED, memory_order_acq_rel);
    atomic_store_explicit(&scheduler->dispatches,
        atomic_load_explicit(&scheduler->dispatches, memory_order_relaxed) + 1u, memory_order_relaxed);
    atomic_store_explicit(&scheduler->granted_instructions,
        atomic_load_explicit(&scheduler->granted_instructions, memory_order_relaxed) + grant, memory_order_relaxed);
    if (granted_quantum) *granted_quantum = grant;
    return ant;
}

Ant *scheduler_acquire(Scheduler *scheduler, uint64_t now_us, size_t *granted_quantum)
{
    pthread_mutex_lock(&scheduler->lock);
    while (!scheduler->stopping) {
        Ant *best = candidate_locked(scheduler, now_us, NULL);
        if (best) {
            lease_locked(scheduler, best, granted_quantum);
            pthread_mutex_unlock(&scheduler->lock);
            return best;
        }
        atomic_fetch_add_explicit(&scheduler->empty_scans, 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(&scheduler->idle_waits, 1u, memory_order_relaxed);
        uint64_t deadline = scheduler->next_wake_us;
        if (deadline == UINT64_MAX) {
            pthread_cond_wait(&scheduler->work_available, &scheduler->lock);
        } else {
            /* Preserve millisecond coalescing for tiny token deficits. Longer
             * waits follow the actual refill/recovery deadline. */
            uint64_t earliest = monotonic_us() + 1000u;
            if (deadline < earliest) deadline = earliest;
            struct timespec ts = { (time_t)(deadline / 1000000u),
                                  (long)(deadline % 1000000u) * 1000L };
            pthread_cond_timedwait(&scheduler->work_available, &scheduler->lock, &ts);
        }
        now_us = monotonic_us();
    }
    pthread_mutex_unlock(&scheduler->lock);
    return NULL;
}

/* Nonblocking selection across every attached universe. Membership/lifetime
 * belong to the caller. Entries must be distinct; NULL entries are suspended.
 * Lock order is registration order, matching every group acquisition. */
Ant *scheduler_acquire_group(Scheduler *const *members, size_t count,
                             uint64_t now_us, size_t *selected, size_t *grant)
{
    for (size_t i = 0; i < count; ++i)
        if (members[i]) pthread_mutex_lock(&members[i]->lock);
    Ant *best = NULL;
    double best_score = -DBL_MAX;
    size_t winner = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!members[i]) continue;
        double score = 0;
        Ant *candidate = candidate_locked(members[i], now_us, &score);
        if (candidate && (!best || score > best_score)) {
            best = candidate; best_score = score; winner = i;
        }
    }
    if (best) {
        /* Credit is relative. Recenter the shared domain so newly registered
         * universes start near current service, not behind years of credit. */
        for (size_t i = 0; i < count; ++i) if (members[i] &&
            !atomic_load_explicit(&members[i]->paused, memory_order_relaxed) &&
            !members[i]->stopping)
            for (size_t j = 0; j < TURMITE_MAX_ANTS; ++j)
                members[i]->fair_credit[j] -= best_score;
        lease_locked(members[winner], best, grant);
        if (selected) *selected = winner;
    } else {
        for (size_t i = 0; i < count; ++i) if (members[i]) {
            atomic_fetch_add_explicit(&members[i]->empty_scans, 1u, memory_order_relaxed);
            atomic_fetch_add_explicit(&members[i]->idle_waits, 1u, memory_order_relaxed);
        }
    }
    for (size_t i = count; i > 0; --i)
        if (members[i-1]) pthread_mutex_unlock(&members[i-1]->lock);
    return best;
}

void scheduler_release(Scheduler *scheduler, Ant *ant, size_t executed, uint64_t now_us)
{
    pthread_mutex_lock(&scheduler->lock);
    const size_t idx = (size_t)(ant - scheduler->colony->ants);
    scheduler->fair_credit[idx] -= (double)executed;
    /* Instruction statistics also have one writer under this lock. */
    atomic_store_explicit(&scheduler->colony->stats[idx].instructions,
        atomic_load_explicit(&scheduler->colony->stats[idx].instructions, memory_order_relaxed) + executed,
        memory_order_relaxed);
    atomic_fetch_and_explicit(&ant->flags, ~(uint32_t)ANT_F_LEASED, memory_order_release);
    accrue_tokens(scheduler, idx, ant, now_us);
    notify_locked(scheduler);
    pthread_mutex_unlock(&scheduler->lock);
}

uint64_t scheduler_get_dispatches(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->dispatches, memory_order_relaxed) : 0;
}

uint64_t scheduler_get_empty_scans(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->empty_scans, memory_order_relaxed) : 0;
}

uint64_t scheduler_get_idle_waits(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->idle_waits, memory_order_relaxed) : 0;
}

uint64_t scheduler_get_granted_instructions(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->granted_instructions, memory_order_relaxed) : 0;
}

void scheduler_set_paused(Scheduler *scheduler, bool paused)
{
    pthread_mutex_lock(&scheduler->lock);
    atomic_store_explicit(&scheduler->paused, paused && !scheduler->draining, memory_order_release);
    notify_locked(scheduler);
    pthread_mutex_unlock(&scheduler->lock);
}

void scheduler_set_token_rate_scale(Scheduler *scheduler, uint32_t scale)
{
    atomic_store_explicit(&scheduler->token_rate_scale, scale ? scale : 1u, memory_order_release);
    scheduler_wake_all(scheduler);
}

void scheduler_set_token_rate_divisor(Scheduler *scheduler, uint32_t divisor)
{
    atomic_store_explicit(&scheduler->token_rate_divisor, divisor ? divisor : 1u, memory_order_release);
    scheduler_wake_all(scheduler);
}

uint32_t scheduler_get_token_rate_divisor(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->token_rate_divisor, memory_order_relaxed) : 1u;
}

void scheduler_set_quantum(Scheduler *scheduler, size_t quantum)
{
    atomic_store_explicit(&scheduler->quantum, quantum ? quantum : 1u, memory_order_release);
    scheduler_wake_all(scheduler);
}

void scheduler_set_min_service(Scheduler *scheduler, size_t min_service)
{
    atomic_store_explicit(&scheduler->min_service, min_service ? min_service : 1u, memory_order_release);
    scheduler_wake_all(scheduler);
}

size_t scheduler_get_quantum(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->quantum, memory_order_relaxed) : 1u;
}

size_t scheduler_get_min_service(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->min_service, memory_order_relaxed) : 1u;
}

void scheduler_wake_all(Scheduler *scheduler)
{
    pthread_mutex_lock(&scheduler->lock);
    notify_locked(scheduler);
    pthread_mutex_unlock(&scheduler->lock);
}

void scheduler_stop(Scheduler *scheduler)
{
    pthread_mutex_lock(&scheduler->lock);
    scheduler->stopping = true;
    atomic_store_explicit(&scheduler->paused, false, memory_order_release);
    notify_locked(scheduler);
    pthread_mutex_unlock(&scheduler->lock);
}

size_t scheduler_active_population(const Scheduler *scheduler)
{
    return atomic_load_explicit(&scheduler->colony->active_population, memory_order_relaxed);
}

int scheduler_init(Scheduler *scheduler, AntColony *colony, SchedulerPolicy policy, size_t quantum)
{
    if (!scheduler || !colony) return -1;
    scheduler->colony = colony;
    scheduler->policy = policy;
    scheduler->wake = NULL;
    scheduler->next_wake_us = UINT64_MAX;
    scheduler->collision_mutation = false;
    scheduler->stopping = false;
    scheduler->draining = false;
    atomic_init(&scheduler->dispatches, 0);
    atomic_init(&scheduler->empty_scans, 0);
    atomic_init(&scheduler->idle_waits, 0);
    atomic_init(&scheduler->granted_instructions, 0);
    atomic_init(&scheduler->paused, false);
    atomic_init(&scheduler->quantum, quantum ? quantum : 1u);
    atomic_init(&scheduler->min_service, 1u);
    atomic_init(&scheduler->token_rate_scale, 1u);
    atomic_init(&scheduler->token_rate_divisor, 1u);
    const uint64_t now_us = monotonic_us();
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        scheduler->fair_credit[i] = 0.0;
        scheduler->last_token_us[i] = now_us;
        scheduler->recovery_at_us[i] = 0;
    }

    /* The condition variable uses CLOCK_MONOTONIC so wall-clock adjustments do
     * not perturb token-service timing. */
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

int scheduler_spawn_ant(Scheduler *scheduler, World *world, Lfsr32 *rng, uint64_t now_us)
{
    if (!scheduler || !world || !rng) return 0;
    pthread_mutex_lock(&scheduler->lock);
    AntColony *colony = scheduler->colony;
    uint32_t enabled = atomic_load_explicit(&colony->enabled_mask, memory_order_relaxed);
    if (scheduler->stopping || scheduler->draining || enabled == UINT32_MAX) {
        pthread_mutex_unlock(&scheduler->lock);
        return 0;
    }
    size_t slot = (size_t)__builtin_ctz(~enabled);
    int added = ant_spawn_random(&colony->ants[slot], colony, world, rng);
    if (added) {
        scheduler->last_token_us[slot] = now_us;
        scheduler->recovery_at_us[slot] = 0;
        scheduler->fair_credit[slot] = 0;
        atomic_store_explicit(&colony->stats[slot].instructions, 0, memory_order_relaxed);
        atomic_store_explicit(&colony->stats[slot].mutations, 0, memory_order_relaxed);
        atomic_fetch_add_explicit(&colony->active_population, 1u, memory_order_relaxed);
        notify_locked(scheduler);
    }
    pthread_mutex_unlock(&scheduler->lock);
    return added;
}

int scheduler_begin_culling(Scheduler *scheduler, size_t target_population)
{
    pthread_mutex_lock(&scheduler->lock);
    size_t current = atomic_load_explicit(&scheduler->colony->active_population, memory_order_relaxed);
    if (target_population >= current) {
        pthread_mutex_unlock(&scheduler->lock);
        return 0;
    }

    /* Resource pressure chooses the weakest available ants first. Setting their
     * generation rate to zero lets remaining tokens drain before retirement. */
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

    notify_locked(scheduler);
    pthread_mutex_unlock(&scheduler->lock);
    return (int)marked;
}

void scheduler_begin_drain(Scheduler *scheduler)
{
    pthread_mutex_lock(&scheduler->lock);
    scheduler->draining = true;
    atomic_store_explicit(&scheduler->paused, false, memory_order_release);
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        Ant *ant = &scheduler->colony->ants[i];
        if (!(flags_load(ant) & ANT_F_ENABLED)) continue;
        /* Leased ants finish against their existing balance; refill on release
         * is disabled by the scheduler-wide flag. No worker-owned state edits. */
        atomic_store_explicit(&ant->token_rate, 0, memory_order_relaxed);
        atomic_fetch_or_explicit(&ant->flags, ANT_F_DRAINING, memory_order_relaxed);
    }
    notify_locked(scheduler);
    pthread_mutex_unlock(&scheduler->lock);
}

bool scheduler_drain_complete(Scheduler *scheduler)
{
    pthread_mutex_lock(&scheduler->lock);
    bool complete = scheduler->draining;
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        Ant *ant = &scheduler->colony->ants[i];
        if (flags_load(ant) & ANT_F_LEASED) { complete = false; continue; }
        if (scheduler->draining) retire_draining(ant, scheduler->colony);
        if (flags_load(ant) & ANT_F_ENABLED) complete = false;
    }
    pthread_mutex_unlock(&scheduler->lock);
    return complete;
}
