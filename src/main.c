#include "ant.h"
#include "dump.h"
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
#include <poll.h>

#define DEFAULT_WIDTH 1200
#define DEFAULT_HEIGHT 800
#define CELL_SIZE 4
#define DEFAULT_WORKERS 2
#define DEFAULT_ANTS 8
#define DEFAULT_QUANTUM 32
#define DEFAULT_MINUTES 5.0
#define MIN_QUANTUM 1
#define MAX_QUANTUM 4096
#define DEFAULT_DUMP_PAGES 127
#define DEFAULT_DUMP_INTERVAL 1.0
#define MAX_DUMP_PAGES 1023

static double mono_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static uint64_t mono_microseconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

typedef struct {
    World world;
    AntColony colony;
    Scheduler scheduler;
    Renderer renderer;
    DumpCapture dump;
    Lfsr32 rng;
    uint32_t seed;
    int width;
    int height;
    int cell_size;
    size_t workers;
    size_t initial_ants;
    size_t quantum;
    double lifetime_minutes;
    size_t dump_pages;
    double dump_interval;
    const char *dump_root;
    bool hud_visible;
    _Atomic bool quit;
    bool restart;
    bool debug_dump;
    bool paused;
    bool headless;
    bool stdin_eof;
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

    while (!atomic_load_explicit(&u->quit, memory_order_acquire)) {
        size_t quantum = 0;
        Ant *ant = scheduler_acquire(&u->scheduler, mono_microseconds(), &quantum);
        if (!ant) break;

        size_t executed = ant_execute_quantum(ant, &u->colony, &u->world, quantum);
        scheduler_release(&u->scheduler, ant, executed, mono_microseconds());
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
        ant_randomize(ant, &u->colony, &u->world, &u->rng,
                      rules_pick(rng_uniform(&u->rng, (uint32_t)rules_count())));
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

    universe_seed(u);

    if (scheduler_init(&u->scheduler, &u->colony, SCHED_WFQ, u->quantum) != 0) {
        fprintf(stderr, "scheduler initialization failed\n");
        world_destroy(&u->world);
        return -1;
    }

    if (dump_capture_init(&u->dump, &u->world, u->seed, u->dump_pages,
                          u->dump_interval, u->started_at) != 0) {
        fprintf(stderr, "debug dump initialization failed\n");
        scheduler_destroy(&u->scheduler);
        world_destroy(&u->world);
        return -1;
    }

    u->hud_visible = true;
    if (!u->headless) {
        if (renderer_init(&u->renderer, u->width, u->height, u->cell_size, u->hud_visible) != 0) {
            fprintf(stderr, "renderer initialization failed\n");
            scheduler_destroy(&u->scheduler);
            world_destroy(&u->world);
            return -1;
        }
    }

    u->threads = calloc(u->workers, sizeof(*u->threads));
    if (!u->threads) {
        if (!u->headless) renderer_destroy(&u->renderer);
        scheduler_destroy(&u->scheduler);
        world_destroy(&u->world);
        return -1;
    }

    return 0;
}

static void universe_destroy(Universe *u)
{
    free(u->threads);
    dump_capture_destroy(&u->dump);
    if (!u->headless) renderer_destroy(&u->renderer);
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
            atomic_store_explicit(&u->quit, true, memory_order_release);
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
    printf("  --dump-pages N       retained debug pages: 1,3,7,...,1023 (default 127)\n");
    printf("  --dump-interval N    seconds between retained pages (default 1)\n");
    printf("  --dump-dir PATH      parent directory for debug dumps (default ./turmite-dumps)\n");
    printf("  --no-hud             hide developer HUD\n");
    printf("  --headless           run without SDL; type Q then Enter to debug-quit\n");
    printf("  --help               show this help\n");
}

static bool valid_population(size_t n)
{
    return n == 2 || n == 4 || n == 8 || n == 16 || n == 32;
}

static bool valid_dump_pages(size_t n)
{
    return n >= 1 && n <= MAX_DUMP_PAGES && ((n + 1) & n) == 0;
}

static void choose_new_seed(Universe *u)
{
    u->seed = rng_entropy_seed();
}

static void handle_key(Universe *u, SDL_Keycode key)
{
    if (key == SDLK_ESCAPE) {
        atomic_store_explicit(&u->quit, true, memory_order_release);
        return;
    }

    if (key == SDLK_q) {
        u->debug_dump = true;
        atomic_store_explicit(&u->quit, true, memory_order_release);
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

static void headless_poll_input(Universe *u)
{
    if (!u->headless || u->stdin_eof) return;

    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    int rc = poll(&pfd, 1, 0);
    if (rc <= 0 || !(pfd.revents & (POLLIN | POLLHUP))) return;

    char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n == 0) {
        u->stdin_eof = true;
        return;
    }
    if (n < 0) return;

    for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == 'q' || buf[i] == 'Q') {
            u->debug_dump = true;
            atomic_store_explicit(&u->quit, true, memory_order_release);
            return;
        }
    }
}

static bool run_universe(Universe *u)
{
    WorkerArg args[8];
    bool timed_out = false;
    if (universe_start_workers(u, args) != 0) return false;

    dump_capture_reset(&u->dump, &u->world, u->seed, u->started_at);
    dump_capture_now(&u->dump, &u->world, &u->colony, &u->scheduler, &u->rng, 0.0, mono_seconds());

    double next_frame = mono_seconds();
    const double frame_period = 1.0 / 60.0;

    while (!atomic_load_explicit(&u->quit, memory_order_acquire) && !u->restart) {
        if (!u->headless) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) atomic_store_explicit(&u->quit, true, memory_order_release);
                else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) handle_key(u, ev.key.keysym.sym);
            }
        } else {
            headless_poll_input(u);
        }

        double now = mono_seconds();
        double age = now - u->started_at;
        if (age >= u->lifetime_minutes * 60.0) {
            timed_out = true;
            break;
        }

        dump_capture_maybe(&u->dump, &u->world, &u->colony, &u->scheduler, &u->rng, age, now);

        if (!u->headless) {
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
        } else {
            /* Keep the control/dump thread cool while workers burn the universe. */
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000L };
            nanosleep(&ts, NULL);
        }
    }

    universe_stop_workers(u, u->workers);

    if (u->debug_dump) {
        double final_age = mono_seconds() - u->started_at;
        dump_capture_now(&u->dump, &u->world, &u->colony, &u->scheduler, &u->rng, final_age, mono_seconds());
        (void)dump_write(&u->dump, &u->world, &u->colony, &u->scheduler,
                         u->workers, scheduler_get_quantum(&u->scheduler),
                         u->lifetime_minutes, u->dump_root);
    }

    if (timed_out) u->restart = true;

    if (!u->headless && (u->restart || u->quit || timed_out)) {
        const Uint32 final_ms = 400;
        Uint32 start = SDL_GetTicks();
        while (SDL_GetTicks() - start < final_ms) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) atomic_store_explicit(&u->quit, true, memory_order_release);
            }
            renderer_render(&u->renderer, &u->world, &u->colony, &u->scheduler,
                            u->workers, u->seed, mono_seconds() - u->started_at,
                            atomic_load_explicit(&u->colony.collisions, memory_order_relaxed), true);
            SDL_Delay(16);
        }
    }

    return !atomic_load_explicit(&u->quit, memory_order_acquire) && (u->restart || timed_out);
}

int main(int argc, char **argv)
{
    size_t ants = DEFAULT_ANTS;
    size_t quantum = DEFAULT_QUANTUM;
    size_t workers = DEFAULT_WORKERS;
    size_t width = DEFAULT_WIDTH;
    size_t height = DEFAULT_HEIGHT;
    double minutes = DEFAULT_MINUTES;
    size_t dump_pages = DEFAULT_DUMP_PAGES;
    double dump_interval = DEFAULT_DUMP_INTERVAL;
    const char *dump_root = dump_default_output_root();
    bool hud = true;
    uint32_t explicit_seed = 0;
    bool has_seed = false;
    bool headless = false;

    static const struct option opts[] = {
        {"seed", required_argument, NULL, 's'},
        {"ants", required_argument, NULL, 'a'},
        {"quantum", required_argument, NULL, 'q'},
        {"workers", required_argument, NULL, 'w'},
        {"width", required_argument, NULL, 'x'},
        {"height", required_argument, NULL, 'y'},
        {"minutes", required_argument, NULL, 'm'},
        {"dump-pages", required_argument, NULL, 'd'},
        {"dump-interval", required_argument, NULL, 'i'},
        {"dump-dir", required_argument, NULL, 'D'},
        {"no-hud", no_argument, NULL, 'n'},
        {"headless", no_argument, NULL, 'H'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    for (;;) {
        int c = getopt_long(argc, argv, "s:a:q:w:x:y:m:d:i:D:nHh", opts, NULL);
        if (c == -1) break;
        switch (c) {
            case 's': if (!parse_seed(optarg, &explicit_seed)) { fprintf(stderr, "bad --seed\n"); return 2; } has_seed = true; break;
            case 'a': if (!parse_uint(optarg, &ants) || !valid_population(ants)) { fprintf(stderr, "--ants must be 2,4,8,16,32\n"); return 2; } break;
            case 'q': if (!parse_uint(optarg, &quantum) || quantum < MIN_QUANTUM || quantum > MAX_QUANTUM) { fprintf(stderr, "bad --quantum\n"); return 2; } break;
            case 'w': if (!parse_uint(optarg, &workers) || workers > 8) { fprintf(stderr, "--workers must be 1..8\n"); return 2; } break;
            case 'x': if (!parse_uint(optarg, &width)) { fprintf(stderr, "bad --width\n"); return 2; } break;
            case 'y': if (!parse_uint(optarg, &height)) { fprintf(stderr, "bad --height\n"); return 2; } break;
            case 'm': minutes = strtod(optarg, NULL); if (minutes <= 0.0) { fprintf(stderr, "bad --minutes\n"); return 2; } break;
            case 'd': if (!parse_uint(optarg, &dump_pages) || !valid_dump_pages(dump_pages)) { fprintf(stderr, "--dump-pages must be 1,3,7,...,1023\n"); return 2; } break;
            case 'i': dump_interval = strtod(optarg, NULL); if (dump_interval <= 0.0) { fprintf(stderr, "bad --dump-interval\n"); return 2; } break;
            case 'D': dump_root = optarg; break;
            case 'n': hud = false; break;
            case 'H': headless = true; break;
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
    u.dump_pages = dump_pages;
    u.dump_interval = dump_interval;
    u.dump_root = dump_root;
    u.headless = headless;
    u.stdin_eof = false;
    atomic_init(&u.quit, false);

    if (universe_init(&u, has_seed ? explicit_seed : rng_entropy_seed()) != 0) return 1;
    u.renderer.hud_visible = hud;
    u.hud_visible = hud;

    bool restart = true;
    while (restart) {
        restart = run_universe(&u);
        if (!u.quit && restart) {
            choose_new_seed(&u);
            u.restart = false;
            u.debug_dump = false;
            u.paused = false;
            atomic_store_explicit(&u.quit, false, memory_order_release);
            universe_seed(&u);
            scheduler_destroy(&u.scheduler);
            if (scheduler_init(&u.scheduler, &u.colony, SCHED_WFQ, u.quantum) != 0) break;
        }
        if (u.quit) break;
    }

    universe_destroy(&u);
    return 0;
}
