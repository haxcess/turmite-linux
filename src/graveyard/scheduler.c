/* GRAVEYARD — retired code, kept verbatim for reference and possible
 * reinstatement. Not part of the build: not in the Makefile's SRC list, and
 * not compiled by any test target. If one of these gets a real caller again,
 * move its prototype back into scheduler.h and its body back into
 * scheduler.c (matching where the comment below says it used to live), then
 * delete it from here.
 *
 * Retired: 2026-09, src/ consistency audit. Both compiled cleanly and were
 * implemented correctly, but had zero callers anywhere in src/ or tests/ —
 * including tests/scheduler_capacity_test.c, the dedicated scheduler test.
 */

#include "../scheduler.h"

/* Was declared in scheduler.h next to scheduler_set_paused(); defined in
 * scheduler.c immediately after it. */
bool scheduler_is_paused(const Scheduler *scheduler)
{
    return scheduler ? atomic_load_explicit(&scheduler->paused, memory_order_acquire) : false;
}

/* Was declared in scheduler.h as the last prototype in the file; defined in
 * scheduler.c immediately after scheduler_active_population(). */
double scheduler_fair_credit(const Scheduler *scheduler, size_t ant_index)
{
    if (!scheduler || ant_index >= TURMITE_MAX_ANTS) return 0.0;
    pthread_mutex_lock((pthread_mutex_t *)&scheduler->lock);
    double c = scheduler->fair_credit[ant_index];
    pthread_mutex_unlock((pthread_mutex_t *)&scheduler->lock);
    return c;
}
