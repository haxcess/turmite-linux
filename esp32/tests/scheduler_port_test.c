#include "scheduler.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

/* Match IDF's unsupported condition-attribute initialization. */
int __wrap_pthread_condattr_init(pthread_condattr_t *attr)
{
    (void)attr;
    return ENOSYS;
}

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int main(void)
{
    World world;
    AntColony colony;
    Scheduler scheduler;
    Lfsr32 rng;
    assert(world_init(&world, 16, 16) == 0);
    assert(ant_colony_init(&colony, &world) == 0);
    rng_seed(&rng, 42);
    Ant *ant = &colony.ants[0];
    assert(ant_spawn_random(ant, &colony, &world, &rng));
    atomic_store(&colony.active_population, 1);
    atomic_store(&ant->tokens_fp, 0);
    atomic_store(&ant->token_capacity_fp, TOKEN_FP_ONE);
    atomic_store(&ant->token_rate, 100); // One token every 10ms.
    assert(scheduler_init(&scheduler, &colony, SCHED_WFQ, 1) == 0);
    size_t grant = 0;
    assert(scheduler_acquire(&scheduler, now_us(), &grant) == ant);
    assert(grant == 1);
    /* A monotonic timestamp passed as realtime would repeatedly time out. */
    assert(scheduler_get_idle_waits(&scheduler) < 20);
    scheduler_release(&scheduler, ant, 0, now_us());
    scheduler_stop(&scheduler);
    scheduler_destroy(&scheduler);
    ant_colony_destroy(&colony);
    world_destroy(&world);
    puts("ESP scheduler: ENOSYS attributes avoided; realtime wait does not spin");
}
