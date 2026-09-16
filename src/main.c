#include "ant.h"
#include "renderer.h"
#include "rng.h"
#include "rules.h"
#include "scheduler.h"
#include "world.h"

#include <SDL2/SDL.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_WIDTH 1200
#define DEFAULT_HEIGHT 800
#define CELL_SIZE 4
#define DEFAULT_WORKERS 2
#define DEFAULT_ANTS 8
#define DEFAULT_QUANTUM 32
#define DEFAULT_MINUTES 5.0
#define MIN_QUANTUM 1
#define MAX_QUANTUM 4096

static double mono_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

typedef struct {
    World world;
    AntColony colony;
    Scheduler scheduler;
    Renderer renderer;
    Lfsr32 rng;
    uint32_t seed;
    int width;
    int height;
    int cell_size;
    size_t workers;
    size_t initial_ants;
    size_t quantum;
    double lifetime_minutes;
    bool hud_visible;
    bool quit;
    bool restart;
    bool paused;
    double started_at;
    pthread_t *threads;
} Universe;

typedef struct {
    Universe *u;
    size_t worker_id;
} WorkerArg;

static void *worker_main(void *arg)
{
    WorkerArg *wa = arg;
    Universe *u = wa->u;
    (void)wa->worker_id;

    while (!u->quit) {
        double now = mono_seconds();
        Ant *ant = scheduler_acquire(&u->scheduler, now, &u->rng);
        if (!ant) break;

        size_t quantum = scheduler_get_quantum(&u->scheduler);
        size_t executed = 0;

        for (; executed < quantum && !u->quit; ++executed) {
            now = mono_seconds();
            int did = ant_execute_one(ant, &u->colony, &u->world, &u->rng, now);

            if (atomic_load_explicit(&ant->clobbered, memory_order_acquire)) {
                bool expected = true;
                if (atomic_compare_exchange_strong_explicit(
                        &ant->clobbered, &expected, false,
                        memory_order_acq_rel, memory_order_relaxed)) {
                    ant_mutate_in_place(ant, &u->rng, now);
                }
                break;
            }

            if (did == 0) break;
            if (!atomic_load_explicit(&ant->enabled, memory_order_acquire)) break;
        }

        scheduler_release(&u->scheduler, ant, executed, mono_seconds());
    }

    return NULL;
}

static void universe_seed(Universe *u)
{
    world_clear(&u->world);
    ant_colony_zero(&u->colony);
    rng_seed(&u->rng, u->seed);

    double now = mono_seconds();
    for (size_t i = 0; i < u->initial_ants; ++i) {
        Ant *ant = &u->colony.ants[i];
        ant_randomize(ant, &u->world, &u->rng,
                      rules_pick(rng_uniform(&u->rng, (uint32_t)rules_count())), now);
    }
    atomic_store_explicit(&u->colony.active_population, u->initial_ants, memory_order_release);
    u->started_at = now;
}

static int universe_init(Universe *u, uint32_t seed)
{
    u->seed = seed;
    u->cell_size = CELL_SIZE;

    if (world_init(&u->world, u->width / u->cell_size, u->height / u->cell_size) != 0) {
        fprintf(stderr, "world allocation failed\n");
        return -1;
    }

    ant_colony_zero(&u->colony);
    universe_seed(u);

    if (scheduler_init(&u->scheduler, &u->colony, SCHED_WFQ, u->quantum) != 0) {
        fprintf(stderr, "scheduler initialization failed\n");
        world_destroy(&u->world);
        return -1;
    }

    u->hud_visible = true;
    if (renderer_init(&u->renderer, u->width, u->height, u->cell_size, u->hud_visible) != 0) {
        fprintf(stderr, "renderer initialization failed\n");
        scheduler_destroy(&u->scheduler);
        world_destroy(&u->world);
        return -1;
    }

    u->threads = calloc(u->workers, sizeof(*u->threads));
    if (!u->threads) {
        renderer_destroy(&u->renderer);
        scheduler_destroy(&u->scheduler);
        world_destroy(&u->world);
        return -1;
    }

    return 0;
}

static void universe_destroy(Universe *u)
{
    free(u->threads);
    renderer_destroy(&u->renderer);
    scheduler_destroy(&u->scheduler);
    world_destroy(&u->world);
}

static int universe_start_workers(Universe *u, WorkerArg *args)
{
    for (size_t i = 0; i < u->workers; ++i) {
        args[i].u = u;
        args[i].worker_id = i;
        if (pthread_create(&u->threads[i], NULL, worker_main, &args[i]) != 0) {
            fprintf(stderr, "pthread_create failed for worker %zu\n", i);
            u->quit = true;
            scheduler_stop(&u->scheduler);
            for (size_t j = 0; j < i; ++j) pthread_join(u->threads[j], NULL);
            return -1;
        }
    }
    return 0;
}

static void universe_stop_workers(Universe *u, size_t created)
{
    scheduler_stop(&u->scheduler);
    scheduler_wake_all(&u->scheduler);
    for (size_t i = 0; i < created; ++i) pthread_join(u->threads[i], NULL);
}

static bool parse_uint(const char *s, size_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno || !end || *end || v == 0 || v > SIZE_MAX) return false;
    *out = (size_t)v;
    return true;
}

static bool parse_seed(const char *s, uint32_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno || !end || *end || v == 0 || v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("  --seed HEX         deterministic universe seed\n");
    printf("  --ants N            2,4,8,16,32\n");
    printf("  --quantum N         instructions per dispatch (1..4096)\n");
    printf("  --workers N         Linux worker pthreads (default 2)\n");
    printf("  --width N            window width (multiple of 4)\n");
    printf("  --height N           window height (multiple of 4)\n");
    printf("  --minutes N          universe lifetime (default 5)\n");
    printf("  --no-hud             hide developer HUD\n");
    printf("  --help               show this help\n");
}

static bool valid_population(size_t n)
{
    return n == 2 || n == 4 || n == 8 || n == 16 || n == 32;
}

static void choose_new_seed(Universe *u)
{
    u->seed = rng_entropy_seed();
}

static void handle_key(Universe *u, SDL_Keycode key)
{
    if (key == SDLK_ESCAPE) {
        u->quit = true;
        return;
    }

    if (key == SDLK_SPACE) {
        u->paused = !u->paused;
        scheduler_set_paused(&u->scheduler, u->paused);
        return;
    }

    if (key == SDLK_h) {
        u->hud_visible = !u->hud_visible;
        u->renderer.hud_visible = u->hud_visible;
        return;
    }

    if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
        size_t q = scheduler_get_quantum(&u->scheduler);
        if (key == SDLK_LEFTBRACKET) q = q > MIN_QUANTUM ? q / 2 : 1;
        else q = q < MAX_QUANTUM ? q * 2 : MAX_QUANTUM;
        if (q < MIN_QUANTUM) q = MIN_QUANTUM;
        if (q > MAX_QUANTUM) q = MAX_QUANTUM;
        scheduler_set_quantum(&u->scheduler, q);
        return;
    }

    if (key == SDLK_r) {
        u->restart = true;
        return;
    }

    if (key == SDLK_EQUALS || key == SDLK_KP_PLUS) {
        size_t current = scheduler_active_population(&u->scheduler);
        if (current < TURMITE_MAX_ANTS) {
            (void)scheduler_double_population(&u->scheduler, &u->world, &u->rng, mono_seconds());
        }
        return;
    }

    if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
        size_t current = scheduler_active_population(&u->scheduler);
        if (current > 2) {
            (void)scheduler_begin_halving(&u->scheduler, current / 2);
        }
        return;
    }
}

static bool run_universe(Universe *u)
{
    WorkerArg args[8];
    bool timed_out = false;
    if (universe_start_workers(u, args) != 0) return false;

    double next_frame = mono_seconds();
    const double frame_period = 1.0 / 60.0;

    while (!u->quit && !u->restart) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) u->quit = true;
            else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) handle_key(u, ev.key.keysym.sym);
        }

        double now = mono_seconds();
        double age = now - u->started_at;
        if (age >= u->lifetime_minutes * 60.0) {
            /* Prototype watchdog sequence: stop computation, leave the final graphic visible briefly. */
            timed_out = true;
            break;
        }

        uint64_t collisions = atomic_load_explicit(&u->colony.collisions, memory_order_relaxed);
        renderer_render(&u->renderer, &u->world, &u->colony, &u->scheduler,
                        u->workers, u->seed, age, collisions, u->paused);

        next_frame += frame_period;
        double sleep_for = next_frame - mono_seconds();
        if (sleep_for > 0.0) {
            Uint32 ms = (Uint32)(sleep_for * 1000.0);
            if (ms > 0) SDL_Delay(ms);
        } else {
            next_frame = mono_seconds();
        }
    }

    universe_stop_workers(u, u->workers);
    if (timed_out) u->restart = true;

    /* Give the final frame a moment on screen. */
    if (u->restart || u->quit || timed_out) {
        const Uint32 final_ms = 400;
        Uint32 start = SDL_GetTicks();
        while (SDL_GetTicks() - start < final_ms) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) u->quit = true;
            }
            renderer_render(&u->renderer, &u->world, &u->colony, &u->scheduler,
                            u->workers, u->seed, mono_seconds() - u->started_at,
                            atomic_load_explicit(&u->colony.collisions, memory_order_relaxed), true);
            SDL_Delay(16);
        }
    }

    return !u->quit && (u->restart || timed_out);
}

int main(int argc, char **argv)
{
    size_t ants = DEFAULT_ANTS;
    size_t quantum = DEFAULT_QUANTUM;
    size_t workers = DEFAULT_WORKERS;
    size_t width = DEFAULT_WIDTH;
    size_t height = DEFAULT_HEIGHT;
    double minutes = DEFAULT_MINUTES;
    bool hud = true;
    uint32_t explicit_seed = 0;
    bool has_seed = false;

    static const struct option opts[] = {
        {"seed", required_argument, NULL, 's'},
        {"ants", required_argument, NULL, 'a'},
        {"quantum", required_argument, NULL, 'q'},
        {"workers", required_argument, NULL, 'w'},
        {"width", required_argument, NULL, 'x'},
        {"height", required_argument, NULL, 'y'},
        {"minutes", required_argument, NULL, 'm'},
        {"no-hud", no_argument, NULL, 'n'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    for (;;) {
        int c = getopt_long(argc, argv, "s:a:q:w:x:y:m:nh", opts, NULL);
        if (c == -1) break;
        switch (c) {
            case 's': if (!parse_seed(optarg, &explicit_seed)) { fprintf(stderr, "bad --seed\n"); return 2; } has_seed = true; break;
            case 'a': if (!parse_uint(optarg, &ants) || !valid_population(ants)) { fprintf(stderr, "--ants must be 2,4,8,16,32\n"); return 2; } break;
            case 'q': if (!parse_uint(optarg, &quantum) || quantum < MIN_QUANTUM || quantum > MAX_QUANTUM) { fprintf(stderr, "bad --quantum\n"); return 2; } break;
            case 'w': if (!parse_uint(optarg, &workers) || workers > 8) { fprintf(stderr, "--workers must be 1..8\n"); return 2; } break;
            case 'x': if (!parse_uint(optarg, &width)) { fprintf(stderr, "bad --width\n"); return 2; } break;
            case 'y': if (!parse_uint(optarg, &height)) { fprintf(stderr, "bad --height\n"); return 2; } break;
            case 'm': minutes = strtod(optarg, NULL); if (minutes <= 0.0) { fprintf(stderr, "bad --minutes\n"); return 2; } break;
            case 'n': hud = false; break;
            case 'h': usage(argv[0]); return 0;
            default: usage(argv[0]); return 2;
        }
    }

    width = (width / CELL_SIZE) * CELL_SIZE;
    height = (height / CELL_SIZE) * CELL_SIZE;
    if (width < 160 || height < 120) {
        fprintf(stderr, "window is too small\n");
        return 2;
    }

    Universe u;
    memset(&u, 0, sizeof(u));
    u.width = (int)width;
    u.height = (int)height;
    u.workers = workers;
    u.initial_ants = ants;
    u.quantum = quantum;
    u.lifetime_minutes = minutes;

    if (universe_init(&u, has_seed ? explicit_seed : rng_entropy_seed()) != 0) return 1;
    u.renderer.hud_visible = hud;
    u.hud_visible = hud;

    bool restart = true;
    while (restart) {
        restart = run_universe(&u);
        if (!u.quit && restart) {
            choose_new_seed(&u);
            u.restart = false;
            u.paused = false;
            u.quit = false;
            universe_seed(&u);
            scheduler_destroy(&u.scheduler);
            if (scheduler_init(&u.scheduler, &u.colony, SCHED_WFQ, u.quantum) != 0) break;
        }
        if (u.quit) break;
    }

    universe_destroy(&u);
    return 0;
}
