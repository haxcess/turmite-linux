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
    if (!(ants == 2 || ants == 4 || ants == 8 || ants == 16 || ants == 32) || quantum == 0 || seconds <= 0) return 2;
    World world; AntColony colony; Scheduler scheduler; Lfsr32 rng;
    if (world_init(&world, 300, 200) != 0) return 1;
    ant_colony_zero(&colony); rng_seed(&rng, UINT32_C(0x12345678));
    for (size_t i = 0; i < ants; ++i) ant_randomize(&colony.ants[i], &world, &rng, rules_get(i % rules_count()));
    atomic_store(&colony.active_population, ants);
    if (scheduler_init(&scheduler, &colony, SCHED_WFQ, quantum) != 0) return 1;
    _Atomic bool stop = false; pthread_t threads[2]; Worker w = { &scheduler, &colony, &world, &stop };
    pthread_create(&threads[0], NULL, run, &w); pthread_create(&threads[1], NULL, run, &w);
    struct timespec ts = { (time_t)seconds, (long)((seconds - (time_t)seconds) * 1e9) };
    nanosleep(&ts, NULL);
    atomic_store(&stop, true); scheduler_stop(&scheduler); scheduler_wake_all(&scheduler);
    pthread_join(threads[0], NULL); pthread_join(threads[1], NULL);
    uint64_t instructions = 0; for (size_t i = 0; i < ants; ++i) instructions += ant_instruction_count(&colony, i);
    printf("ants=%zu quantum=%zu seconds=%.3f instructions=%llu dispatches=%llu collisions=%llu population=%zu\n",
           ants, quantum, seconds, (unsigned long long)instructions,
           (unsigned long long)scheduler_get_dispatches(&scheduler),
           (unsigned long long)atomic_load(&colony.collisions),
           atomic_load(&colony.active_population));
    scheduler_destroy(&scheduler); world_destroy(&world); return 0;
}
