#include "ant.h"
#include "dump.h"
#include "renderer_sdl.h"
#include "rng.h"
#include "rules.h"
#include "scheduler.h"
#include "worker_pool.h"
#include "world.h"

#include <SDL2/SDL.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

/* Linux orchestration: main owns SDL windows/events/presentation. Each monitor
 * has an independent universe controller for lifecycle, capture and frame
 * preparation, plus a shared process-wide ant worker pool. SDL is never called by those threads. */

#define DEFAULT_WIDTH 1200
#define DEFAULT_HEIGHT 800
#define DEFAULT_CELL_SIZE 2
#define DISPLAY_FRAME_SLOTS 3
#define CONTROL_QUEUE_SIZE 64
#define DEFAULT_WORKERS 2
#define DEFAULT_ANTS 3
#define DEFAULT_QUANTUM 320
#define DEFAULT_MIN_SERVICE 1600
#define DEFAULT_TOKEN_RATE_DIVISOR 1
#define DEFAULT_MINUTES 1.1
#define MIN_QUANTUM 1
#define MAX_QUANTUM 4096
#define DEFAULT_DUMP_PAGES 127
#define DEFAULT_DUMP_INTERVAL 1.0
#define GLITTER_DENSITY_DEFAULT 0

/* Monotonic time drives simulation age and token accounting so wall-clock
 * adjustments cannot make ants gain or lose execution budget unexpectedly. */
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

/* One independent universe per selected display. Configuration is immutable
 * after startup. The controller owns simulation/control state; main owns SDL.
 * view_lock protects only O(1) command/frame handoffs, never world scans. */
typedef struct {
    World world;
    AntColony colony;
    Scheduler scheduler;
    Renderer renderer;
    RenderPaletteMode palette_mode;
    uint32_t palette[TURMITE_COLORS];
    uint8_t *display_cells;
    uint32_t *frame_pixels[DISPLAY_FRAME_SLOTS];
    RendererHud frame_hud[DISPLAY_FRAME_SLOTS];
    pthread_mutex_t view_lock;
    bool view_lock_initialized;
    bool scheduler_initialized;
    int ready_frame;
    int displayed_frame;
    SDL_Keycode commands[CONTROL_QUEUE_SIZE];
    size_t command_read;
    size_t command_count;
    pthread_t controller;
    bool controller_started;
    _Atomic bool done;
    bool failed;
    DumpCapture dump;
    Lfsr32 rng;
    uint32_t seed;
    int width;
    int height;
    int cell_size;
    unsigned glitter_density;
    int display_index;
    bool fullscreen;
    size_t workers;
    size_t initial_ants;
    size_t quantum;
    size_t min_service;
    uint32_t token_rate_divisor;
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
    WorkerPool *pool;
    size_t pool_slot;
} Universe;

/* Seed actual tape cells, so ants can read and overwrite the confetti.
 * Density adds one shard per 1000 cells per level; overlap is intentional. */
static void universe_glitter(Universe *u)
{
    Lfsr32 rng;
    rng_seed(&rng, u->seed ^ UINT32_C(0x676c6974));
    const size_t shards = (u->world.cells * u->glitter_density + 999u) / 1000u;
    for (size_t i = 0; i < shards; ++i) {
        const unsigned length = 3u + rng_uniform(&rng, 5);
        const unsigned width = 1u + rng_uniform(&rng, 3);
        const unsigned vertical = rng_uniform(&rng, 2);
        const unsigned x = rng_uniform(&rng, (uint32_t)u->world.width);
        const unsigned y = rng_uniform(&rng, (uint32_t)u->world.height);
        const uint8_t color = (uint8_t)(1u + rng_uniform(&rng, TURMITE_COLORS - 1u));
        for (unsigned a = 0; a < length; ++a) for (unsigned b = 0; b < width; ++b) {
            const unsigned px = (x + (vertical ? b : a)) % (unsigned)u->world.width;
            const unsigned py = (y + (vertical ? a : b)) % (unsigned)u->world.height;
            world_store_cell(&u->world, (size_t)py * u->world.width + px, color);
        }
    }
}

/* Reset the shared tape and ant population for a fresh universe while keeping
 * the process-level configuration (display, workers, scheduler tuning) intact. */
static void universe_seed(Universe *u)
{
    world_clear(&u->world);
    universe_glitter(u);
    ant_colony_reset(&u->colony);
    rng_seed(&u->rng, u->seed);
    render_palette_init(u->palette, u->palette_mode, u->seed);

    double now = mono_seconds();
    for (size_t i = 0; i < u->initial_ants; ++i) {
        Ant *ant = &u->colony.ants[i];
        ant_randomize(ant, &u->colony, &u->world, &u->rng,
                      rules_pick(rng_uniform(&u->rng, (uint32_t)rules_count())));
    }
    atomic_store_explicit(&u->colony.active_population, u->initial_ants, memory_order_release);
    u->started_at = now;
}

/* Called on main only, after its controller has stopped; suspension drains remaining leases. */
static void universe_destroy(Universe *u)
{
    worker_pool_suspend(u->pool, u->pool_slot);
    free(u->display_cells);
    u->display_cells = NULL;
    for (size_t i = 0; i < DISPLAY_FRAME_SLOTS; ++i) {
        free(u->frame_pixels[i]);
        u->frame_pixels[i] = NULL;
    }
    dump_capture_destroy(&u->dump);
    if (!u->headless) renderer_destroy(&u->renderer);
    if (u->scheduler_initialized) scheduler_destroy(&u->scheduler);
    u->scheduler_initialized = false;
    ant_colony_destroy(&u->colony);
    world_destroy(&u->world);
    if (u->view_lock_initialized) pthread_mutex_destroy(&u->view_lock);
    u->view_lock_initialized = false;
}

/* A zero-initialized Universe is required. All windows are initialized on main
 * before any controller starts, so partial startup can unwind synchronously. */
static int universe_init(Universe *u, uint32_t seed)
{
    u->seed = seed;
    if (u->cell_size < 1 || u->cell_size > 10) goto fail;
    u->ready_frame = u->displayed_frame = -1;
    if (pthread_mutex_init(&u->view_lock, NULL) != 0) goto fail;
    u->view_lock_initialized = true;
    if (world_init(&u->world, u->width / u->cell_size, u->height / u->cell_size) != 0) goto fail;
    if (ant_colony_init(&u->colony, &u->world) != 0) goto fail;
    universe_seed(u);
    if (scheduler_init(&u->scheduler, &u->colony, SCHED_WFQ, u->quantum) != 0) goto fail;
    u->scheduler_initialized = true;
    scheduler_set_min_service(&u->scheduler, u->min_service);
    scheduler_set_token_rate_divisor(&u->scheduler, u->token_rate_divisor);
    if (dump_capture_init(&u->dump, &u->world, u->seed, u->dump_pages,
                          u->dump_interval, u->started_at) != 0) goto fail;
    if (!u->headless) {
        if (renderer_init(&u->renderer, u->width, u->height, u->cell_size,
                          u->hud_visible, u->display_index, u->fullscreen) != 0) goto fail;
        u->display_cells = malloc(u->world.cells);
        if (!u->display_cells) goto fail;
        for (size_t i = 0; i < DISPLAY_FRAME_SLOTS; ++i) {
            u->frame_pixels[i] = malloc(u->world.cells * sizeof(uint32_t));
            if (!u->frame_pixels[i]) goto fail;
        }
        char title[80];
        snprintf(title, sizeof(title), "Turmite Universe — display %d", u->display_index);
        SDL_SetWindowTitle(u->renderer.window, title);
    }
    return 0;
fail:
    fprintf(stderr, "universe initialization failed (display %d)\n", u->display_index);
    universe_destroy(u);
    return -1;
}

/* Workers are independent consumers of scheduler leases. Partial startup
 * failure stops the scheduler and joins already-created threads before return. */
/* Parsing helpers reject zero for numeric controls where zero has no useful
 * interpretation; callers add any narrower semantic range checks. */
static bool parse_uint(const char *s, size_t *out)
{
    if (!s) return false;
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno || !end || *end || v == 0 || v > SIZE_MAX) return false;
    *out = (size_t)v;
    return true;
}

static bool parse_display(const char *s, int *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || !end || *end || v < 0 || v > INT_MAX) return false;
    *out = (int)v;
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

/* Every option accepted by getopt_long() below has a short form, so every
 * entry here documents both. Order matches the opts[]/optstring order so the
 * two can be diffed against each other by eye. */
typedef struct {
    char short_opt;
    const char *long_opt;
    const char *arg_name; /* NULL for options that take no argument */
    const char *description;
} OptionHelp;

static const OptionHelp option_help[] = {
    {'s', "seed",               "HEX",  "deterministic universe seed"},
    {'a', "ants",                "N",   "2-32"},
    {'q', "quantum",             "N",   "maximum instructions per dispatch (1..4096)"},
    {'b', "min-service",         "N",   "minimum normal dispatch batch (default 16)"},
    {'v', "token-rate-divisor",  "N",   "divide all ant token generation rates (default 1)"},
    {'w', "workers",             "N",   "shared ant threads across all universes (default 2)"},
    {'p', "display",             "N",   "select monitor N for single-window modes (default 0)"},
    {'W', "windowed",            NULL,  "one normal window on selected monitor (default)"},
    {'F', "fullscreen",          NULL,  "fullscreen on selected monitor"},
    {'A', "fullscreen-all",      NULL,  "fullscreen on every detected monitor"},
    {'u', "hud",                 NULL,  "show developer HUD (hidden by default)"},
    {'R', "random",              NULL,  "random HSV hues, dark background, S=80%, V=90%"},
    {'r', "randomish",           NULL,  "hues spaced 32 degrees in random 160-degree arc"},
    {'g', "glitter",            "N",   "startup confetti density (1..10; disabled by default)"},
    {'c', "cell-size",           "N",   "pixels per cell (1..10, default 1)"},
    {'x', "width",               "N",   "windowed/headless canvas width (default 1200)"},
    {'y', "height",              "N",   "windowed/headless canvas height (default 800)"},
    {'m', "minutes",             "N",   "universe lifetime (default 5)"},
    {'d', "dump-pages",          "N",   "retained debug pages: 1,3,7,...,1023 (default 127)"},
    {'i', "dump-interval",       "N",   "seconds between retained pages (default 1)"},
    {'D', "dump-dir",            "PATH","parent directory for debug dumps (default ./turmite-dumps)"},
    {'n', "no-hud",              NULL,  "hide developer HUD"},
    {'H', "headless",            NULL,  "run without SDL; type Q then Enter to debug-quit"},
    {'h', "help",                NULL,  "show this help"},
};

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    for (size_t i = 0; i < sizeof(option_help) / sizeof(option_help[0]); ++i) {
        const OptionHelp *o = &option_help[i];
        char left[40];
        if (o->arg_name)
            snprintf(left, sizeof(left), "-%c, --%s %s", o->short_opt, o->long_opt, o->arg_name);
        else
            snprintf(left, sizeof(left), "-%c, --%s", o->short_opt, o->long_opt);
        printf("  %-28s %s\n", left, o->description);
    }
}

static bool valid_population(size_t n)
{
    return n >= 2 && n <= 32;
}

static void choose_new_seed(Universe *u)
{
    u->seed = rng_entropy_seed();
}

static void universe_begin_debug_drain(Universe *u)
{
    if (u->debug_dump) return;
    u->debug_dump = true;
    u->paused = false;
    u->restart = false;
    scheduler_begin_drain(&u->scheduler);
    if (!u->display_cells) u->display_cells = malloc(u->world.cells);
    if (!u->display_cells || dump_begin_frames(&u->dump, u->dump_root) != 0) {
        u->failed = true;
        atomic_store_explicit(&u->quit, true, memory_order_release);
    }
}

/* Interactive controls deliberately modify scheduler policy/population rather
 * than touching worker threads directly. This keeps control-plane operations
 * serialized through the scheduler. */
static void handle_key(Universe *u, SDL_Keycode key)
{
    if (key == SDLK_ESCAPE) {
        atomic_store_explicit(&u->quit, true, memory_order_release);
        return;
    }

    if (key == SDLK_q) {
        universe_begin_debug_drain(u);
        return;
    }

    if (u->debug_dump) return; /* Do not pause/reseed/refill a draining universe. */

    if (key == SDLK_SPACE) {
        u->paused = !u->paused;
        scheduler_set_paused(&u->scheduler, u->paused);
        return;
    }

    if (key == SDLK_h) {
        u->hud_visible = !u->hud_visible;
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
            (void)scheduler_double_population(&u->scheduler, &u->world, &u->rng, mono_microseconds());
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

/* Headless input is deliberately non-blocking so the control thread can keep
 * servicing watchdog and dump timers while workers run independently. */
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
            universe_begin_debug_drain(u);
            return;
        }
    }
}

/* Main enqueues keys; the universe controller applies them in order. */
static bool universe_enqueue(Universe *u, SDL_Keycode key)
{
    pthread_mutex_lock(&u->view_lock);
    const bool room = u->command_count < CONTROL_QUEUE_SIZE;
    if (room) {
        u->commands[(u->command_read + u->command_count) % CONTROL_QUEUE_SIZE] = key;
        ++u->command_count;
    }
    pthread_mutex_unlock(&u->view_lock);
    return room;
}

static void universe_apply_commands(Universe *u)
{
    for (;;) {
        pthread_mutex_lock(&u->view_lock);
        if (!u->command_count) {
            pthread_mutex_unlock(&u->view_lock);
            return;
        }
        SDL_Keycode key = u->commands[u->command_read];
        u->command_read = (u->command_read + 1) % CONTROL_QUEUE_SIZE;
        --u->command_count;
        pthread_mutex_unlock(&u->view_lock);
        handle_key(u, key);
    }
}

/* Single producer: controller chooses the slot neither ready nor being
 * presented. Triple buffering lets it replace an unread frame without waiting
 * for a slow upload. It never touches a slot held by main's SDL presentation. */
static void universe_render(Universe *u, double age, bool paused)
{
    for (size_t i = 0; i < u->world.cells; ++i)
        u->display_cells[i] = world_load(&u->world, i);
    if (u->debug_dump && !u->failed &&
        dump_write_frame(&u->dump, u->display_cells, u->palette) != 0) {
        u->failed = true;
        atomic_store_explicit(&u->quit, true, memory_order_release);
    }
    if (u->headless) return;
    pthread_mutex_lock(&u->view_lock);
    int slot = 0;
    while (slot == u->ready_frame || slot == u->displayed_frame) ++slot;
    pthread_mutex_unlock(&u->view_lock);
    const RenderFrame frame = { (size_t)u->world.width, (size_t)u->world.height, u->display_cells };
    (void)render_argb(&frame, u->palette, u->frame_pixels[slot], u->world.cells);
    u->frame_hud[slot] = (RendererHud){
        .population = scheduler_active_population(&u->scheduler),
        .workers = u->workers,
        .quantum = scheduler_get_quantum(&u->scheduler),
        .min_service = scheduler_get_min_service(&u->scheduler),
        .seed = u->seed,
        .age = age,
        .collisions = atomic_load_explicit(&u->colony.collisions, memory_order_relaxed),
        .paused = paused
    };
    /* HUD visibility travels with the frame; renderer fields stay main-owned. */
    u->frame_hud[slot].visible = u->hud_visible;
    pthread_mutex_lock(&u->view_lock);
    u->ready_frame = slot;
    pthread_mutex_unlock(&u->view_lock);
}

/* Main consumes a prepared buffer. No world scan or conversion on this thread. */
static void universe_present(Universe *u)
{
    pthread_mutex_lock(&u->view_lock);
    int slot = u->ready_frame;
    if (slot >= 0) {
        u->displayed_frame = slot;
        u->ready_frame = -1;
    }
    pthread_mutex_unlock(&u->view_lock);
    if (slot < 0) return;
    u->renderer.hud_visible = u->frame_hud[slot].visible;
    renderer_present(&u->renderer, u->frame_pixels[slot], u->world.cells, &u->frame_hud[slot]);
}

/* Per-universe control loop. CPU frame preparation and dump capture run here,
 * never on SDL's main thread and never on an ant execution worker. */
static bool run_universe(Universe *u)
{
    bool timed_out = false;
    worker_pool_enable(u->pool, u->pool_slot, &u->scheduler, &u->world);

    dump_capture_reset(&u->dump, &u->world, u->seed, u->started_at);
    dump_capture_now(&u->dump, &u->world, &u->colony, &u->scheduler, &u->rng, 0.0, mono_seconds());

    /* Rendering is independently throttled to 30 Hz. Simulation workers are not
     * frame-locked and may execute any amount of work between observations. */
    double next_frame = mono_seconds();
    const double frame_period = 1.0 / 30.0;

    while (!atomic_load_explicit(&u->quit, memory_order_acquire) && !u->restart) {
        universe_apply_commands(u);
        if (u->headless) headless_poll_input(u);
        if (atomic_load_explicit(&u->quit, memory_order_acquire) || u->restart) break;

        double now = mono_seconds();
        double age = now - u->started_at;
        if (!u->debug_dump && age >= u->lifetime_minutes * 60.0) {
            timed_out = true;
            break;
        }

        dump_capture_maybe(&u->dump, &u->world, &u->colony, &u->scheduler, &u->rng, age, now);

        if (!u->headless || u->debug_dump) {
            universe_render(u, age, u->paused);
            if (u->debug_dump && scheduler_drain_complete(&u->scheduler)) {
                atomic_store_explicit(&u->quit, true, memory_order_release);
                break;
            }

            next_frame += frame_period;
            double sleep_for = next_frame - mono_seconds();
            if (sleep_for > 0.0) {
                struct timespec ts = { (time_t)sleep_for,
                    (long)((sleep_for - (time_t)sleep_for) * 1e9) };
                nanosleep(&ts, NULL);
            } else {
                next_frame = mono_seconds();
            }
        } else {
            /* Keep the control/dump thread cool while workers burn the universe. */
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000L };
            nanosleep(&ts, NULL);
        }
    }

    worker_pool_suspend(u->pool, u->pool_slot);

    if (u->debug_dump) {
        /* Always export the final stable frame after the last lease returns. */
        if (!u->failed) universe_render(u, mono_seconds() - u->started_at, true);
        double final_age = mono_seconds() - u->started_at;
        dump_capture_now(&u->dump, &u->world, &u->colony, &u->scheduler, &u->rng, final_age, mono_seconds());
        if (dump_write(&u->dump, &u->world, &u->colony, &u->scheduler,
                         u->workers, scheduler_get_quantum(&u->scheduler),
                         u->lifetime_minutes, u->dump_root, u->palette) != 0)
            u->failed = true;
    }

    if (timed_out) u->restart = true;

    if (!u->headless && !u->debug_dump) {
        universe_render(u, mono_seconds() - u->started_at, true);
        /* Give main a chance to show the final frame without stalling peers. */
        struct timespec ts = { .tv_nsec = 400000000L };
        nanosleep(&ts, NULL);
    }

    return !atomic_load_explicit(&u->quit, memory_order_acquire) && (u->restart || timed_out);
}

static void *universe_control(void *arg)
{
    Universe *u = arg;
    while (run_universe(u)) {
        choose_new_seed(u);
        u->restart = false;
        u->debug_dump = false;
        u->paused = false;
        /* Main may request close at any time: never clear its quit flag. */
        if (atomic_load_explicit(&u->quit, memory_order_acquire)) break;
        universe_seed(u);
        scheduler_destroy(&u->scheduler);
        u->scheduler_initialized = false;
        if (scheduler_init(&u->scheduler, &u->colony, SCHED_WFQ, u->quantum) != 0) {
            u->failed = true;
            break;
        }
        u->scheduler_initialized = true;
        scheduler_set_min_service(&u->scheduler, u->min_service);
        scheduler_set_token_rate_divisor(&u->scheduler, u->token_rate_divisor);
    }
    atomic_store_explicit(&u->done, true, memory_order_release);
    return NULL;
}

/* A key/close event is routed only to the universe owning that SDL window. */
static void route_event(Universe *universes, size_t count, const SDL_Event *event)
{
    if (event->type == SDL_QUIT) {
        for (size_t i = 0; i < count; ++i)
            atomic_store_explicit(&universes[i].quit, true, memory_order_release);
        return;
    }
    Uint32 id = 0;
    if (event->type == SDL_KEYDOWN && !event->key.repeat) id = event->key.windowID;
    else if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE)
        id = event->window.windowID;
    if (!id) return;
    for (size_t i = 0; i < count; ++i) {
        Universe *u = &universes[i];
        if (!u->renderer.window || SDL_GetWindowID(u->renderer.window) != id) continue;
        if (event->type == SDL_WINDOWEVENT || event->key.keysym.sym == SDLK_ESCAPE)
            atomic_store_explicit(&u->quit, true, memory_order_release);
        else if (!universe_enqueue(u, event->key.keysym.sym))
            fprintf(stderr, "display %d control queue full; key ignored\n", u->display_index);
        return;
    }
}

int main(int argc, char **argv)
{
    /* Parse into local configuration first. Universe is constructed only after
     * display-derived dimensions and all validation are known. */
    size_t ants = DEFAULT_ANTS;
    size_t quantum = DEFAULT_QUANTUM;
    size_t min_service = DEFAULT_MIN_SERVICE;
    size_t token_rate_divisor = DEFAULT_TOKEN_RATE_DIVISOR;
    size_t workers = DEFAULT_WORKERS;
    size_t cell_size = DEFAULT_CELL_SIZE;
    size_t width = DEFAULT_WIDTH;
    size_t height = DEFAULT_HEIGHT;
    int display_index = 0;
    bool all_displays = false;
    bool fullscreen = false;
    double minutes = DEFAULT_MINUTES;
    size_t dump_pages = DEFAULT_DUMP_PAGES;
    double dump_interval = DEFAULT_DUMP_INTERVAL;
    const char *dump_root = dump_default_output_root();
    bool hud = false;
    RenderPaletteMode palette_mode = RENDER_PALETTE_DEFAULT;
    uint32_t explicit_seed = 0;
    bool has_seed = false;
    bool headless = false;
    size_t glitter_density = GLITTER_DENSITY_DEFAULT;

    static const struct option opts[] = {
        {"seed", required_argument, NULL, 's'},
        {"ants", required_argument, NULL, 'a'},
        {"quantum", required_argument, NULL, 'q'},
        {"min-service", required_argument, NULL, 'b'},
        {"token-rate-divisor", required_argument, NULL, 'v'},
        {"workers", required_argument, NULL, 'w'},
        {"display", required_argument, NULL, 'p'},
        {"windowed", no_argument, NULL, 'W'},
        {"fullscreen", no_argument, NULL, 'F'},
        {"fullscreen-all", no_argument, NULL, 'A'},
        {"hud", no_argument, NULL, 'u'},
        {"random", no_argument, NULL, 'R'},
        {"randomish", no_argument, NULL, 'r'},
        {"glitter", required_argument, NULL, 'g'},
        {"cell-size", required_argument, NULL, 'c'},
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
        int c = getopt_long(argc, argv, "s:a:q:b:v:w:p:WFAuRrg:c:x:y:m:d:i:D:nHh", opts, NULL);
        if (c == -1) break;
        switch (c) {
            case 's': if (!parse_seed(optarg, &explicit_seed)) { fprintf(stderr, "bad --seed\n"); return 2; } has_seed = true; break;
            case 'a': if (!parse_uint(optarg, &ants) || !valid_population(ants)) { fprintf(stderr, "--ants must be 2-32\n"); return 2; } break;
            case 'q': if (!parse_uint(optarg, &quantum) || quantum < MIN_QUANTUM || quantum > MAX_QUANTUM) { fprintf(stderr, "bad --quantum\n"); return 2; } break;
            case 'b': if (!parse_uint(optarg, &min_service) || min_service < 1 || min_service > MAX_QUANTUM) { fprintf(stderr, "bad --min-service\n"); return 2; } break;
            case 'v': if (!parse_uint(optarg, &token_rate_divisor) || token_rate_divisor > UINT32_MAX) { fprintf(stderr, "bad --token-rate-divisor\n"); return 2; } break;
            case 'w': if (!parse_uint(optarg, &workers) || workers > 8) { fprintf(stderr, "--workers must be 1..8\n"); return 2; } break;
            case 'p': if (!parse_display(optarg, &display_index)) { fprintf(stderr, "bad --display\n"); return 2; } break;
            case 'W': fullscreen = false; all_displays = false; break;
            case 'F': fullscreen = true; all_displays = false; break;
            case 'A': fullscreen = true; all_displays = true; break;
            case 'u': hud = true; break;
            case 'R': palette_mode = RENDER_PALETTE_RANDOM; break;
            case 'r': palette_mode = RENDER_PALETTE_RANDOMISH; break;
            case 'g': if (!parse_uint(optarg, &glitter_density) || glitter_density > 10) { fprintf(stderr, "--glitter must be 1..10\n"); return 2; } break;
            case 'c': if (!parse_uint(optarg, &cell_size) || cell_size > 10) { fprintf(stderr, "--cell-size must be 1..10\n"); return 2; } break;
            case 'x': if (!parse_uint(optarg, &width)) { fprintf(stderr, "bad --width\n"); return 2; } break;
            case 'y': if (!parse_uint(optarg, &height)) { fprintf(stderr, "bad --height\n"); return 2; } break;
            case 'm': minutes = strtod(optarg, NULL); if (minutes <= 0.0) { fprintf(stderr, "bad --minutes\n"); return 2; } break;
            case 'd': if (!parse_uint(optarg, &dump_pages) || !dump_pages_value_valid(dump_pages)) { fprintf(stderr, "--dump-pages must be 1,3,7,...,1023\n"); return 2; } break;
            case 'i': dump_interval = strtod(optarg, NULL); if (dump_interval <= 0.0) { fprintf(stderr, "bad --dump-interval\n"); return 2; } break;
            case 'D': dump_root = optarg; break;
            case 'n': hud = false; break;
            case 'H': headless = true; break;
            case 'h': usage(argv[0]); return 0;
            default: usage(argv[0]); return 2;
        }
    }

    if (optind < argc) { fprintf(stderr, "unexpected argument: %s\n", argv[optind]); return 2; }
    const int displays = headless ? 1 : renderer_display_count();
    if (displays <= 0 || (!headless && !all_displays && display_index >= displays)) {
        fprintf(stderr, "no usable displays or invalid --display index\n");
        if (!headless) renderer_shutdown();
        return 2;
    }
    const size_t count = headless || !all_displays ? 1u : (size_t)displays;
    Universe *universes = NULL;
    if (count <= SIZE_MAX / sizeof(*universes) &&
        posix_memalign((void **)&universes, _Alignof(Universe), count * sizeof(*universes)) == 0)
        memset(universes, 0, count * sizeof(*universes));
    if (!universes) {
        if (!headless) renderer_shutdown();
        return 1;
    }
    size_t initialized = 0;
    int result = 0;
    const uint32_t base_seed = has_seed ? explicit_seed : rng_entropy_seed();
    for (size_t i = 0; i < count; ++i) {
        Universe *u = &universes[i];
        u->display_index = all_displays && !headless ? (int)i : display_index;
        size_t world_width = width, world_height = height;
        if (fullscreen && !headless) {
            int dw, dh;
            if (renderer_display_size(u->display_index, &dw, &dh) != 0) { result = 2; break; }
            world_width = (size_t)dw;
            world_height = (size_t)dh;
        }
        if (world_width < 160 || world_height < 120 || world_width > 65535 || world_height > 65535) {
            fprintf(stderr, "universe dimensions are invalid\n");
            result = 2;
            break;
        }
        u->cell_size = (int)cell_size;
        u->glitter_density = glitter_density;
        u->width = (int)world_width;
        u->height = (int)world_height;
        u->fullscreen = fullscreen;
        u->workers = workers;
        u->initial_ants = ants;
        u->quantum = quantum;
        u->min_service = min_service;
        u->token_rate_divisor = (uint32_t)token_rate_divisor;
        u->lifetime_minutes = minutes;
        u->dump_pages = dump_pages;
        u->dump_interval = dump_interval;
        u->dump_root = dump_root;
        u->headless = headless;
        u->hud_visible = hud;
        u->palette_mode = palette_mode;
        atomic_init(&u->quit, false);
        atomic_init(&u->done, false);
        /* Deterministic distinct startup seeds; a single window keeps --seed
         * exactly. Automatic restarts still use fresh host entropy. */
        uint32_t seed = base_seed ^ (UINT32_C(0x9e3779b9) * (uint32_t)i);
        if (!seed) seed = 1;
        if (universe_init(u, seed) != 0) {
            fprintf(stderr, "failed to initialize display %d: canvas %dx%d, cell size %d\n",
                    u->display_index, u->width, u->height, u->cell_size);
            result = 1;
            break;
        }
        ++initialized;
    }
    WorkerPool *pool = NULL;
    if (!result) {
        pool = worker_pool_create(count, workers);
        if (!pool) { fprintf(stderr, "shared worker pool initialization failed\n"); result = 1; }
        else for (size_t i = 0; i < count; ++i) {
            universes[i].pool = pool;
            universes[i].pool_slot = i;
        }
    }
    if (!result && headless) {
        (void)universe_control(&universes[0]);
        result = universes[0].failed ? 1 : 0;
    } else if (!result) {
        for (size_t i = 0; i < count; ++i) {
            if (pthread_create(&universes[i].controller, NULL, universe_control, &universes[i]) != 0) {
                fprintf(stderr, "controller thread creation failed\n");
                result = 1;
                break;
            }
            universes[i].controller_started = true;
        }
        size_t live = result ? 0 : count;
        while (live) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) route_event(universes, count, &event);
            live = 0;
            for (size_t i = 0; i < count; ++i) {
                Universe *u = &universes[i];
                if (!u->controller_started) continue;
                universe_present(u);
                if (atomic_load_explicit(&u->done, memory_order_acquire)) {
                    pthread_join(u->controller, NULL);
                    u->controller_started = false;
                    if (u->failed) result = 1;
                    /* Release this universe now; peers keep running. */
                    universe_destroy(u);
                } else ++live;
            }
            if (live) SDL_Delay(1);
        }
    }
    /* Also covers partial thread creation: request stop, join, then free. */
    for (size_t i = 0; i < initialized; ++i)
        atomic_store_explicit(&universes[i].quit, true, memory_order_release);
    for (size_t i = 0; i < initialized; ++i) {
        if (universes[i].controller_started) pthread_join(universes[i].controller, NULL);
        universe_destroy(&universes[i]);
    }
    worker_pool_destroy(pool);
    free(universes);
    if (!headless) renderer_shutdown();
    return result;
}
