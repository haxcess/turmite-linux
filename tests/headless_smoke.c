#include "ant.h"
#include "rng.h"
#include "rules.h"
#include "scheduler.h"
#include "world.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
#include <stdint.h>

static _Atomic bool stop_flag;

typedef struct {
    Scheduler *scheduler;
    AntColony *colony;
    World *world;
} Worker;

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static void *run(void *arg)
{
    Worker *w = arg;
    while (!atomic_load_explicit(&stop_flag, memory_order_relaxed)) {
        size_t grant = 0;
        Ant *ant = scheduler_acquire(w->scheduler, now_us(), &grant);
        if (!ant) break;
        size_t n = ant_execute_quantum(ant, w->colony, w->world, grant);
        scheduler_release(w->scheduler, ant, n, now_us());
    }
    return NULL;
}

int main(void)
{
    World world;
    AntColony colony;
    Scheduler scheduler;
    Lfsr32 rng;
    pthread_t t0, t1;
    Worker worker;

    if (world_init(&world, 300, 200) != 0) return 1;
    ant_colony_zero(&colony);
    rng_seed(&rng, UINT32_C(0x12345678));
    for (size_t i = 0; i < 8; ++i) {
        ant_randomize(&colony.ants[i], &world, &rng, rules_get(i % rules_count()));
    }
    atomic_store(&colony.active_population, 8);
    if (scheduler_init(&scheduler, &colony, SCHED_WFQ, 32) != 0) return 2;

    atomic_store(&stop_flag, false);
    worker.scheduler = &scheduler;
    worker.colony = &colony;
    worker.world = &world;
    pthread_create(&t0, NULL, run, &worker);
    pthread_create(&t1, NULL, run, &worker);

    struct timespec sleep_for = { .tv_sec = 0, .tv_nsec = 250000000L };
    nanosleep(&sleep_for, NULL);
    atomic_store(&stop_flag, true);
    scheduler_stop(&scheduler);
    scheduler_wake_all(&scheduler);
    pthread_join(t0, NULL);
    pthread_join(t1, NULL);

    size_t population = atomic_load(&colony.active_population);
    if (population != 8) {
        fprintf(stderr, "smoke failed: population changed unexpectedly: %zu\n", population);
        scheduler_destroy(&scheduler);
        world_destroy(&world);
        return 3;
    }
    printf("smoke ok: instructions=%llu collisions=%llu population=%zu\n",
           (unsigned long long)(ant_instruction_count(&colony, 0)),
           (unsigned long long)(atomic_load(&colony.collisions)),
           population);

    scheduler_destroy(&scheduler);
    world_destroy(&world);
    return 0;
}
