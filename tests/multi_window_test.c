/* Exercise real host orchestration with two windows on the dummy display.
 * Including main keeps its internal ownership/queue API private to the host. */
#define main turmite_application_main
#include "../src/main.c"
#undef main

#include <assert.h>

static void init_test_universe(Universe *u, int width, int height, uint32_t seed)
{
    memset(u, 0, sizeof(*u));
    u->width = width;
    u->height = height;
    u->workers = 2;
    u->initial_ants = 8;
    u->quantum = 32;
    u->min_service = 16;
    u->token_rate_divisor = 1;
    u->lifetime_minutes = 5;
    u->dump_pages = 1;
    u->dump_interval = 1;
    u->hud_visible = false;
    atomic_init(&u->quit, false);
    atomic_init(&u->done, false);
    u->cell_size = 1;
    assert(universe_init(u, seed) == 0);
}

static void pump(Universe *u, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (u[i].renderer.window) universe_present(&u[i]);
    SDL_PumpEvents();
    SDL_Delay(2);
}

static RendererHud observed(const Universe *u)
{
    assert(u->displayed_frame >= 0);
    return u->frame_hud[u->displayed_frame];
}

static uint64_t instructions(const Universe *u)
{
    uint64_t total = 0;
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i)
        total += ant_instruction_count(&u->colony, i);
    return total;
}

static void key(Universe *all, size_t index, SDL_Keycode code)
{
    SDL_Event event = {0};
    event.type = SDL_KEYDOWN;
    event.key.repeat = 0;
    event.key.windowID = SDL_GetWindowID(all[index].renderer.window);
    event.key.keysym.sym = code;
    route_event(all, 2, &event);
}

/* Produce frames faster than consumption, verifying no in-use buffer overwrite.
 * This producer changes only its own tape and calls the same application path. */
static void *fast_frames(void *arg)
{
    Universe *u = arg;
    for (size_t frame = 1; frame <= 500; ++frame) {
        uint8_t color = (uint8_t)(frame % TURMITE_COLORS);
        for (size_t cell = 0; cell < u->world.cells; ++cell) world_store(&u->world, cell, color);
        universe_render(u, (double)frame, false);
    }
    atomic_store_explicit(&u->done, true, memory_order_release);
    return NULL;
}

int main(void)
{
    assert(renderer_display_count() >= 1);
    Universe u[2];
    init_test_universe(&u[0], 160, 120, 0x12345678);
    init_test_universe(&u[1], 192, 128, 0x87654321);
    assert(u[0].world.data != u[1].world.data);
    assert(u[0].colony.occupancy != u[1].colony.occupancy);
    assert(SDL_GetWindowID(u[0].renderer.window) != SDL_GetWindowID(u[1].renderer.window));

    /* A producer may outrun the UI. Every consumed slot must still contain
     * one complete generation, and retained frames must remain immutable. */
    pthread_t producer;
    assert(pthread_create(&producer, NULL, fast_frames, &u[0]) == 0);
    double deadline = mono_seconds() + 5;
    do {
        pump(u, 1);
        if (u[0].displayed_frame >= 0) {
            const int slot = u[0].displayed_frame;
            const uint8_t color = (uint8_t)((size_t)observed(&u[0]).age % TURMITE_COLORS);
            const uint32_t expected = 0xff000000u | RENDER_BASE_PALETTE[color];
            SDL_Delay(1);
            for (size_t cell = 0; cell < u[0].world.cells; ++cell)
                assert(u[0].frame_pixels[slot][cell] == expected);
        }
        assert(mono_seconds() < deadline);
    } while (!atomic_load_explicit(&u[0].done, memory_order_acquire));
    pthread_join(producer, NULL);
    atomic_store(&u[0].done, false);
    universe_seed(&u[0]);
    for (size_t i = 0; i < 2; ++i) {
        assert(pthread_create(&u[i].controller, NULL, universe_control, &u[i]) == 0);
        u[i].controller_started = true;
    }
    deadline = mono_seconds() + 5;
    while (u[1].displayed_frame < 0 || observed(&u[0]).age >= 500) {
        pump(u, 2);
        assert(mono_seconds() < deadline);
    }
    key(u, 0, SDLK_SPACE);
    deadline = mono_seconds() + 5;
    while (!observed(&u[0]).paused) { pump(u, 2); assert(mono_seconds() < deadline); }
    assert(!observed(&u[1]).paused);
    uint64_t before = instructions(&u[1]);
    for (int i = 0; i < 50; ++i) pump(u, 2);
    assert(instructions(&u[1]) > before);

    key(u, 0, SDLK_h);
    deadline = mono_seconds() + 5;
    while (!u[0].renderer.hud_visible) { pump(u, 2); assert(mono_seconds() < deadline); }
    assert(!u[1].renderer.hud_visible);
    const uint32_t seed0 = observed(&u[0]).seed, seed1 = observed(&u[1]).seed;
    key(u, 0, SDLK_r);
    deadline = mono_seconds() + 5;
    while (observed(&u[0]).seed == seed0) { pump(u, 2); assert(mono_seconds() < deadline); }
    assert(observed(&u[1]).seed == seed1);
    assert(!observed(&u[0]).paused);

    SDL_Event close = {0};
    close.type = SDL_WINDOWEVENT;
    close.window.event = SDL_WINDOWEVENT_CLOSE;
    close.window.windowID = SDL_GetWindowID(u[0].renderer.window);
    route_event(u, 2, &close);
    assert(atomic_load(&u[0].quit));
    assert(!atomic_load(&u[1].quit));
    deadline = mono_seconds() + 5;
    while (!atomic_load_explicit(&u[0].done, memory_order_acquire)) {
        pump(u, 2); assert(mono_seconds() < deadline);
    }
    pthread_join(u[0].controller, NULL);
    u[0].controller_started = false;
    universe_destroy(&u[0]);
    assert(SDL_WasInit(SDL_INIT_VIDEO));
    before = instructions(&u[1]);
    for (int i = 0; i < 50; ++i) pump(u, 2);
    assert(instructions(&u[1]) > before);
    assert(!atomic_load(&u[1].done));

    uint32_t *readback = malloc(u[1].world.cells * sizeof(*readback));
    assert(readback);
    assert(SDL_RenderReadPixels(u[1].renderer.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                readback, u[1].width * (int)sizeof(*readback)) == 0);
    assert(memcmp(readback, u[1].frame_pixels[u[1].displayed_frame],
                  u[1].world.cells * sizeof(*readback)) == 0);
    free(readback);
    SDL_Event quit = { .type = SDL_QUIT };
    route_event(u, 2, &quit);
    pthread_join(u[1].controller, NULL);
    universe_destroy(&u[1]);
    /* Idempotent cleanup covers the application's final sweep. */
    universe_destroy(&u[0]);
    renderer_shutdown();
    puts("multi-window ok: independent worlds/controls, frame ownership, restart, close, peer survival");
    return 0;
}
