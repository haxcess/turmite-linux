#include "worker_pool.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static void delay(long ns) { struct timespec t = {0, ns}; nanosleep(&t, NULL); }
static double cpu_time(void)
{
    struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void await_dispatch(Scheduler *s, uint64_t before)
{
    for (unsigned i = 0; i < 2000; ++i) {
        if (scheduler_get_dispatches(s) > before) return;
        delay(1000000L);
    }
    assert(!"lost wakeup");
}
int main(int argc, char **argv)
{
    (void)argv; bool measure_only = argc > 1;
    alarm(15);
    World w; AntColony c; Scheduler s; Lfsr32 rng;
    assert(world_init(&w, 32, 32) == 0 && ant_colony_init(&c, &w) == 0);
    rng_seed(&rng, 123);
    TurmiteRule hold = {.states=1, .colors=1, .table={{{0, TURN_H, 0, false}}}};
    ant_randomize(&c.ants[0], &c, &w, &rng, &hold);
    atomic_store(&c.active_population, 1);
    atomic_store(&c.ants[0].tokens_fp, 0); atomic_store(&c.ants[0].token_rate, 0);
    assert(scheduler_init(&s, &c, SCHED_WFQ, 16) == 0);
    WorkerPool *p = worker_pool_create(1, 2); assert(p);
    worker_pool_enable(p, 0, &s, &w);
    delay(20000000L);
    uint64_t scans = scheduler_get_empty_scans(&s); double cpu = cpu_time();
    delay(200000000L);
    uint64_t empty = scheduler_get_empty_scans(&s) - scans;
    printf("idle_200ms: scans=%llu cpu_seconds=%.6f\n", (unsigned long long)empty, cpu_time() - cpu);
    if (!measure_only) assert(empty <= 2);
    /* A setter must interrupt an indefinite sleep, even when all workers wait. */
    atomic_store(&c.ants[0].token_rate, 100000);
    scheduler_set_token_rate_divisor(&s, 1);
    await_dispatch(&s, 0);
    scheduler_set_paused(&s, true); delay(20000000L);
    uint64_t before = scheduler_get_dispatches(&s);
    scans = scheduler_get_empty_scans(&s); delay(100000000L);
    assert(scheduler_get_dispatches(&s) == before);
    if (!measure_only) assert(scheduler_get_empty_scans(&s) - scans <= 2);
    scheduler_set_paused(&s, false); await_dispatch(&s, before);
    /* A future refill deadline must also be interrupted by a faster rate. */
    worker_pool_suspend(p, 0);
    atomic_store(&c.ants[0].tokens_fp, 0); atomic_store(&c.ants[0].token_rate, 1);
    scheduler_set_min_service(&s, 16);
    worker_pool_enable(p, 0, &s, &w); delay(20000000L);
    before = scheduler_get_dispatches(&s); scans = scheduler_get_empty_scans(&s);
    delay(100000000L); assert(scheduler_get_dispatches(&s) == before);
    if (!measure_only) assert(scheduler_get_empty_scans(&s) - scans <= 2);
    scheduler_set_token_rate_scale(&s, 100000); await_dispatch(&s, before);
    for (unsigned i = 0; i < 100; ++i) {
        worker_pool_suspend(p, 0);
        scheduler_set_paused(&s, true);
        worker_pool_enable(p, 0, &s, &w);
        scheduler_set_paused(&s, false);
    }
    before = scheduler_get_dispatches(&s); await_dispatch(&s, before);
    worker_pool_suspend(p, 0); worker_pool_destroy(p);
    scheduler_destroy(&s); ant_colony_destroy(&c); world_destroy(&w);
    puts("worker idle ok: zero-rate/paused/deadline sleep, configuration wakeup, membership races, shutdown");
}
