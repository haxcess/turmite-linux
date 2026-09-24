/* Worst-case adjacent-ant false sharing: two independent stationary machines. */
#include "ant.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static AntColony colony;
static World world;

static void *run(void *arg)
{
    Ant *ant = arg;
    for (unsigned i = 0; i < 100000; ++i) {
        atomic_store_explicit(&ant->tokens_fp, 256 * TOKEN_FP_ONE, memory_order_relaxed);
        ant_execute_quantum(ant, &colony, &world, 256);
    }
    return NULL;
}

int main(void)
{
    assert(world_init(&world, 300, 200) == 0 && ant_colony_init(&colony, &world) == 0);
    Lfsr32 rng;
    rng_seed(&rng, 123);
    TurmiteRule hold = {.states = 1, .colors = 1, .table = {{{0, TURN_H, 0, false}}}};
    for (unsigned i = 0; i < 2; ++i) {
        ant_randomize(&colony.ants[i], &colony, &world, &rng, &hold);
        ant_release_occupancy(&colony.ants[i], &colony);
        /* Separate tape cache lines so this measures ant metadata contention. */
        atomic_store(&colony.positions[i], i * 128);
        atomic_store(&colony.occupancy[i * 128], i + 1);
    }
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pthread_t threads[2];
    for (unsigned i = 0; i < 2; ++i)
        assert(pthread_create(&threads[i], NULL, run, &colony.ants[i]) == 0);
    for (unsigned i = 0; i < 2; ++i) pthread_join(threads[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("ant_bytes=%zu seconds=%.6f\n", sizeof(Ant),
           (double)(end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9);
    ant_colony_destroy(&colony);
    world_destroy(&world);
}
