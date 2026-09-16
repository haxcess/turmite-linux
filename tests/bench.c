#include "ant.h"
#include "rng.h"
#include "rules.h"
#include "scheduler.h"
#include "world.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BENCH_MAX_WORKERS 64

typedef struct { Scheduler *scheduler; AntColony *colony; World *world; _Atomic bool *stop; } Worker;
static uint64_t now_us(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull; }
static void *run(void *arg) {
    Worker *w = arg;
    while (!atomic_load_explicit(w->stop, memory_order_relaxed)) {
        size_t grant = 0;
        Ant *ant = scheduler_acquire(w->scheduler, now_us(), &grant);
        if (!ant) break;
        size_t executed = ant_execute_quantum(ant, w->colony, w->world, grant);
        scheduler_release(w->scheduler, ant, executed, now_us());
    }
    return NULL;
}
int main(int argc, char **argv) {
    size_t ants = argc > 1 ? (size_t)strtoul(argv[1], NULL, 0) : 32;
    size_t quantum = argc > 2 ? (size_t)strtoul(argv[2], NULL, 0) : 256;
    double seconds = argc > 3 ? strtod(argv[3], NULL) : 3.0;
    size_t workers = argc > 4 ? (size_t)strtoul(argv[4], NULL, 0) : 2;
    uint32_t rate_scale = argc > 5 ? (uint32_t)strtoul(argv[5], NULL, 0) : 1u;
    size_t min_service = argc > 6 ? (size_t)strtoul(argv[6], NULL, 0) : 16u;
    if (!(ants == 2 || ants == 4 || ants == 8 || ants == 16 || ants == 32) || quantum == 0 || seconds <= 0 || workers == 0 || workers > BENCH_MAX_WORKERS || rate_scale == 0 || min_service == 0) return 2;
    World world; AntColony colony; Scheduler scheduler; Lfsr32 rng;
    if (world_init(&world, 300, 200) != 0) return 1;
    if (ant_colony_init(&colony, &world) != 0) return 1;
    rng_seed(&rng, UINT32_C(0x12345678));
    for (size_t i = 0; i < ants; ++i) {
        ant_randomize(&colony.ants[i], &colony, &world, &rng, rules_get(i % rules_count()));
    }
    atomic_store(&colony.active_population, ants);
    if (scheduler_init(&scheduler, &colony, SCHED_WFQ, quantum) != 0) return 1;
    scheduler_set_token_rate_scale(&scheduler, rate_scale);
    scheduler_set_min_service(&scheduler, min_service);
    _Atomic bool stop = false; pthread_t threads[BENCH_MAX_WORKERS]; Worker w = { &scheduler, &colony, &world, &stop };
    for (size_t i = 0; i < workers; ++i) pthread_create(&threads[i], NULL, run, &w);
    struct timespec ts = { (time_t)seconds, (long)((seconds - (time_t)seconds) * 1e9) };
    nanosleep(&ts, NULL);
    atomic_store(&stop, true); scheduler_stop(&scheduler); scheduler_wake_all(&scheduler);
    for (size_t i = 0; i < workers; ++i) pthread_join(threads[i], NULL);
    uint64_t instructions = 0; for (size_t i = 0; i < ants; ++i) instructions += ant_instruction_count(&colony, i);
    const uint64_t dispatches = scheduler_get_dispatches(&scheduler);
    const uint64_t granted = scheduler_get_granted_instructions(&scheduler);
    const uint64_t empty_scans = scheduler_get_empty_scans(&scheduler);
    const uint64_t idle_waits = scheduler_get_idle_waits(&scheduler);
    printf("ants=%zu workers=%zu quantum=%zu min_service=%zu rate_scale=%u seconds=%.3f instructions=%llu dispatches=%llu avg_exec_per_dispatch=%.2f avg_grant=%.2f empty_scans=%llu idle_waits=%llu collisions=%llu population=%zu\n",
           ants, workers, quantum, min_service, rate_scale, seconds,
           (unsigned long long)instructions, (unsigned long long)dispatches,
           dispatches ? (double)instructions / (double)dispatches : 0.0,
           dispatches ? (double)granted / (double)dispatches : 0.0,
           (unsigned long long)empty_scans, (unsigned long long)idle_waits,
           (unsigned long long)atomic_load(&colony.collisions), atomic_load(&colony.active_population));
    scheduler_destroy(&scheduler); ant_colony_destroy(&colony); world_destroy(&world); return 0;
}
