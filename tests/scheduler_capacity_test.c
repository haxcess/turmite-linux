#include "ant.h"
#include "scheduler.h"
#include "world.h"
#include "rules.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static void *stop_later(void *arg)
{
    struct timespec delay = {.tv_nsec = 50000000L};
    nanosleep(&delay, NULL);
    scheduler_stop(arg);
    return NULL;
}

int main(void)
{
    /* An unreachable threshold hangs acquire; bound the regression. */
    alarm(3);
    World world;
    AntColony colony;
    Scheduler scheduler;
    Lfsr32 rng;
    assert(world_init(&world, 160, 120) == 0);
    assert(ant_colony_init(&colony, &world) == 0);
    rng_seed(&rng, 123);
    Ant *ant = &colony.ants[0];
    ant_randomize(ant, &colony, &world, &rng, rules_get(0));
    assert(scheduler_init(&scheduler, &colony, SCHED_WFQ, 790) == 0);
    scheduler_set_min_service(&scheduler, 1000);
    scheduler_set_token_rate_divisor(&scheduler, 3);
    const unsigned capacities[] = {128, 789, 790, 4096};
    for (size_t i = 0; i < sizeof(capacities)/sizeof(capacities[0]); ++i) {
        atomic_store(&ant->token_capacity_fp, capacities[i] << TOKEN_FP_SHIFT);
        atomic_store(&ant->tokens_fp, capacities[i] << TOKEN_FP_SHIFT);
        size_t grant = 0;
        Ant *leased = scheduler_acquire(&scheduler, scheduler.last_token_us[0], &grant);
        assert(leased == ant);
        assert(grant == (capacities[i] < 790 ? capacities[i] : 790));
        scheduler_release(&scheduler, leased, 0, scheduler.last_token_us[0]);
    }
    /* Draining ants can still spend a partial bucket. */
    atomic_store(&ant->token_rate, 0);
    atomic_store(&ant->token_capacity_fp, 128u << TOKEN_FP_SHIFT);
    atomic_store(&ant->tokens_fp, 7u << TOKEN_FP_SHIFT);
    atomic_fetch_or(&ant->flags, ANT_F_DRAINING);
    size_t grant = 0;
    assert(scheduler_acquire(&scheduler, scheduler.last_token_us[0], &grant) == ant);
    assert(grant == 7);
    scheduler_release(&scheduler, ant, 0, scheduler.last_token_us[0]);
    atomic_fetch_and(&ant->flags, ~(uint32_t)ANT_F_DRAINING);
    /* Normal batching must still wait below capacity; rate zero prevents refill. */
    atomic_store(&ant->tokens_fp, 127u << TOKEN_FP_SHIFT);
    pthread_t stopper;
    assert(pthread_create(&stopper, NULL, stop_later, &scheduler) == 0);
    assert(scheduler_acquire(&scheduler, scheduler.last_token_us[0], &grant) == NULL);
    pthread_join(stopper, NULL);
    scheduler_destroy(&scheduler);
    ant_colony_destroy(&colony);
    world_destroy(&world);
    alarm(0);
    puts("capacity regression ok: full buckets dispatch, partial buckets wait, draining bypasses batching");
}
