#include "engine.h"
#include "ant.h"
#include "renderer.h"
#include "scheduler.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WORKERS = 2, QUANTUM = 64, MIN_SERVICE = 16, MAX_SIDE = 960 };

struct AndroidEngine {
    World world;
    AntColony colony;
    Scheduler scheduler;
    Lfsr32 rng;
    pthread_t workers[WORKERS];
    size_t started;
    bool colony_ready, scheduler_ready;
    uint8_t *snapshot;
};

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000) + (uint64_t)ts.tv_nsec / 1000;
}

static void *worker(void *arg)
{
    AndroidEngine *engine = arg;
    for (;;) {
        size_t grant;
        Ant *ant = scheduler_acquire(&engine->scheduler, now_us(), &grant);
        if (!ant) return NULL;
        size_t executed = ant_execute_quantum(ant, &engine->colony, &engine->world, grant);
        scheduler_release(&engine->scheduler, ant, executed, now_us());
    }
}

AndroidEngine *android_engine_create(int width, int height, uint32_t seed, int ants, int divisor)
{
    if (width < 16 || height < 16 || width > MAX_SIDE || height > MAX_SIDE) return NULL;
    if (ants < 2 || ants > 8 || divisor < 1 || divisor > 100) return NULL;
    // AntColony contains an alignas(64) member; plain malloc need not satisfy it.
    AndroidEngine *engine = NULL;
    if (posix_memalign((void **)&engine, _Alignof(AndroidEngine), sizeof(*engine)) != 0) return NULL;
    memset(engine, 0, sizeof(*engine));
    if (world_init(&engine->world, width, height) != 0) goto fail;
    if (ant_colony_init(&engine->colony, &engine->world) != 0) goto fail;
    engine->colony_ready = true;
    rng_seed(&engine->rng, seed ? seed : rng_entropy_seed());
    for (size_t i = 0; i < (size_t)ants; ++i) {
        const TurmiteRule *rule = rules_get(rng_uniform(&engine->rng, (uint32_t)rules_count()));
        ant_randomize(&engine->colony.ants[i], &engine->colony, &engine->world, &engine->rng, rule);
    }
    atomic_store(&engine->colony.active_population, (size_t)ants);
    if (scheduler_init(&engine->scheduler, &engine->colony, SCHED_WFQ, QUANTUM) != 0) goto fail;
    engine->scheduler_ready = true;
    scheduler_set_min_service(&engine->scheduler, MIN_SERVICE);
    scheduler_set_token_rate_divisor(&engine->scheduler, (uint32_t)divisor);
    engine->snapshot = malloc(engine->world.cells);
    if (!engine->snapshot) goto fail;
    for (size_t i = 0; i < WORKERS; ++i) {
        if (pthread_create(&engine->workers[i], NULL, worker, engine) != 0) goto fail;
        ++engine->started;
    }
    return engine;
fail:
    android_engine_destroy(engine);
    return NULL;
}

bool android_engine_frame(AndroidEngine *engine, uint32_t *argb, size_t capacity)
{
    if (!engine || !argb || capacity < engine->world.cells) return false;
    for (size_t i = 0; i < engine->world.cells; ++i)
        engine->snapshot[i] = world_load(&engine->world, i);
    RenderFrame frame = { (size_t)engine->world.width, (size_t)engine->world.height, engine->snapshot };
    return render_argb(&frame, RENDER_BASE_PALETTE, argb, capacity);
}

size_t android_engine_population(const AndroidEngine *engine)
{
    return engine ? scheduler_active_population(&engine->scheduler) : 0;
}

uint32_t android_engine_divisor(const AndroidEngine *engine)
{
    return engine ? scheduler_get_token_rate_divisor(&engine->scheduler) : 0;
}

uint64_t android_engine_instructions(const AndroidEngine *engine)
{
    uint64_t count = 0;
    if (engine) for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i)
        count += ant_instruction_count(&engine->colony, i);
    return count;
}

void android_engine_destroy(AndroidEngine *engine)
{
    if (!engine) return;
    if (engine->scheduler_ready) scheduler_stop(&engine->scheduler);
    for (size_t i = 0; i < engine->started; ++i) pthread_join(engine->workers[i], NULL);
    if (engine->scheduler_ready) scheduler_destroy(&engine->scheduler);
    if (engine->colony_ready) ant_colony_destroy(&engine->colony);
    world_destroy(&engine->world);
    free(engine->snapshot);
    free(engine);
}
