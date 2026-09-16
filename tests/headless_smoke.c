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
    if (ant_colony_init(&colony, &world) != 0) return 1;
    rng_seed(&rng, UINT32_C(0x12345678));
    for (size_t i = 0; i < 8; ++i) {
        ant_randomize(&colony.ants[i], &colony, &world, &rng, rules_get(i % rules_count()));
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

    /* Validate the derived occupancy index after concurrent execution. Each
     * resident id may appear at most once and must agree with its published
     * authoritative position. Displaced/clobbered ants are allowed no slot. */
    bool seen[TURMITE_MAX_ANTS] = { false };
    for (size_t cell = 0; cell < colony.occupancy_cells; ++cell) {
        uint8_t owner = atomic_load_explicit(&colony.occupancy[cell], memory_order_relaxed);
        if (!owner) continue;
        if (owner > TURMITE_MAX_ANTS || seen[owner - 1u]) {
            fprintf(stderr, "smoke failed: invalid/duplicate occupancy owner %u\n", owner);
            scheduler_destroy(&scheduler);
            ant_colony_destroy(&colony);
            world_destroy(&world);
            return 4;
        }
        seen[owner - 1u] = true;
        const uint32_t p = ant_packed_position(&colony, owner - 1u);
        const uint32_t x = p & UINT32_C(0xffff);
        const uint32_t y = p >> 16;
        const size_t expected_cell = (size_t)y * (size_t)world.width + (size_t)x;
        if (expected_cell != cell) {
            fprintf(stderr, "smoke failed: stale occupancy owner %u\n", owner);
            scheduler_destroy(&scheduler);
            ant_colony_destroy(&colony);
            world_destroy(&world);
            return 5;
        }
    }

    size_t population = atomic_load(&colony.active_population);
    if (population != 8) {
        fprintf(stderr, "smoke failed: population changed unexpectedly: %zu\n", population);
        scheduler_destroy(&scheduler);
        ant_colony_destroy(&colony);
        world_destroy(&world);
        return 3;
    }
    printf("smoke ok: instructions=%llu collisions=%llu population=%zu\n",
           (unsigned long long)(ant_instruction_count(&colony, 0)),
           (unsigned long long)(atomic_load(&colony.collisions)),
           population);

    scheduler_destroy(&scheduler);
    ant_colony_destroy(&colony);
    world_destroy(&world);
    return 0;
}
