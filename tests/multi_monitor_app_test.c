/* Test the actual CLI/main loop with two simulated displays on SDL dummy video.
 * The wrappers adapt window placement to the one dummy display; this validates
 * discovery/routing/lifetime, not a real compositor or physical monitors. */
#define main turmite_application_main
#include "../src/main.c"
#undef main
#include <assert.h>

static pthread_t main_thread;
static SDL_Window *windows[2];
static Uint32 ids[2];
static unsigned presentations[2];
static bool closed[2];
static int created, requested_display, expected_windows;
static bool close_sent[2];
static bool expect_windowed, expect_hud;
static int expected_cell_size;

void __real_renderer_present(Renderer *, const uint32_t *, size_t, const RendererHud *);
void __wrap_renderer_present(Renderer *renderer, const uint32_t *pixels, size_t cells, const RendererHud *hud)
{
    assert(renderer->cell_size == expected_cell_size);
    assert(cells == (size_t)(renderer->width / expected_cell_size) *
                    (size_t)(renderer->height / expected_cell_size));
    int texture_width, texture_height;
    assert(SDL_QueryTexture(renderer->texture, NULL, NULL, &texture_width, &texture_height) == 0);
    assert(texture_width == renderer->width / expected_cell_size);
    assert(texture_height == renderer->height / expected_cell_size);
    assert(renderer->hud_visible == expect_hud);
    assert(hud->visible == expect_hud);
    __real_renderer_present(renderer, pixels, cells, hud);
}

int __wrap_SDL_GetNumVideoDisplays(void) { return 2; }
int __wrap_SDL_GetDisplayBounds(int display, SDL_Rect *rect)
{
    assert(pthread_equal(pthread_self(), main_thread));
    if (display < 0 || display >= 2) return -1;
    *rect = (SDL_Rect){display * 160, 0, display ? 192 : 160, display ? 128 : 120};
    return 0;
}

SDL_Window *__real_SDL_CreateWindow(const char *, int, int, int, int, Uint32);
SDL_Window *__wrap_SDL_CreateWindow(const char *title, int x, int y, int width, int height, Uint32 flags)
{
    assert(pthread_equal(pthread_self(), main_thread));
    assert(created < expected_windows);
    const int display = requested_display >= 0 ? requested_display : created;
    assert(width == (expect_windowed || display == 0 ? 160 : 192));
    assert(height == (expect_windowed || display == 0 ? 120 : 128));
    assert((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ==
           (expect_windowed ? 0u : SDL_WINDOW_FULLSCREEN_DESKTOP));
    assert(!SDL_GetHintBoolean(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, SDL_TRUE));
    /* Checking only the low display bits misses UNDEFINED placement, which
     * permits the Wayland compositor to choose a different fullscreen output. */
    assert(x == (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    assert(y == (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_Window *window = __real_SDL_CreateWindow(title, 0, 0, width, height,
                                               flags & ~SDL_WINDOW_FULLSCREEN_DESKTOP);
    assert(window);
    windows[created] = window;
    ids[created++] = SDL_GetWindowID(window);
    return window;
}

void __real_SDL_RenderPresent(SDL_Renderer *);
void __wrap_SDL_RenderPresent(SDL_Renderer *renderer)
{
    assert(pthread_equal(pthread_self(), main_thread));
    for (int i = 0; i < created; ++i)
        if (!closed[i] && SDL_GetRenderer(windows[i]) == renderer) ++presentations[i];
    __real_SDL_RenderPresent(renderer);
}

void __real_SDL_DestroyWindow(SDL_Window *);
void __wrap_SDL_DestroyWindow(SDL_Window *window)
{
    assert(pthread_equal(pthread_self(), main_thread));
    for (int i = 0; i < created; ++i)
        if (window && windows[i] == window) closed[i] = true;
    __real_SDL_DestroyWindow(window);
}

int __real_SDL_PollEvent(SDL_Event *);
int __wrap_SDL_PollEvent(SDL_Event *event)
{
    assert(pthread_equal(pthread_self(), main_thread));
    if (__real_SDL_PollEvent(event)) return 1;
    if (!created) return 0;
    for (int i = 0; i < created; ++i) if (!presentations[i]) return 0;
    int target = -1;
    if (!close_sent[0]) target = 0;
    else if (created == 2 && closed[0] && !close_sent[1]) target = 1;
    if (target < 0) return 0;
    /* Avoid repeatedly synthesizing close while the controller is stopping. */
    close_sent[target] = true;
    *event = (SDL_Event){0};
    event->type = SDL_WINDOWEVENT;
    event->window.event = SDL_WINDOWEVENT_CLOSE;
    event->window.windowID = ids[target];
    return 1;
}

static void run_case(bool windowed, int only, bool hud, const char *const *options)
{
    memset(windows, 0, sizeof(windows));
    memset(presentations, 0, sizeof(presentations));
    memset(closed, 0, sizeof(closed));
    created = 0;
    memset(close_sent, 0, sizeof(close_sent));
    requested_display = only;
    expect_windowed = windowed;
    expect_hud = hud;
    expected_windows = only < 0 ? 2 : 1;
    char *args[20] = {"turmite", "--dump-pages", "1", "--width", "160", "--height", "120"};
    int argc = 7;
    expected_cell_size = DEFAULT_CELL_SIZE;
    for (size_t i = 0; options[i]; ++i)
        if (!strcmp(options[i], "-c") || !strcmp(options[i], "--cell-size"))
            expected_cell_size = atoi(options[i + 1]);
    for (size_t i = 0; options[i]; ++i) args[argc++] = (char *)options[i];
    args[argc] = NULL;
    optind = 0;
    assert(turmite_application_main(argc, args) == 0);
    assert(created == expected_windows);
    for (int i = 0; i < created; ++i) assert(closed[i] && presentations[i]);
    assert(SDL_WasInit(SDL_INIT_VIDEO) == 0);
}

int main(void)
{
    main_thread = pthread_self();
    /* Prevent a broken application loop from hanging automated validation. */
    alarm(20);
    run_case(true, 0, false, (const char *[]){NULL});
    run_case(true, 0, true, (const char *[]){"--hud", NULL});
    run_case(true, 0, false, (const char *[]){"--collision-mutation", NULL});
    run_case(false, -1, false, (const char *[]){"-A", "-M", NULL});
    run_case(false, 0, false, (const char *[]){"--fullscreen", NULL});
    run_case(false, 1, true, (const char *[]){"-F", "-p1", "-u", NULL});
    run_case(false, -1, false, (const char *[]){"--fullscreen-all", NULL});
    run_case(false, -1, false, (const char *[]){"-p1", "-A", NULL});
    run_case(false, -1, false, (const char *[]){"-A", "-p1", NULL});
    run_case(true, 1, false, (const char *[]){"-A", "-p1", "-W", NULL});
    run_case(false, 1, false, (const char *[]){"-p1", "-A", "-F", NULL});
    for (int size = 1; size <= 10; ++size) {
        char number[4];
        snprintf(number, sizeof(number), "%d", size);
        run_case(true, 0, false, (const char *[]){"--cell-size", number, NULL});
    }
    run_case(false, -1, false, (const char *[]){"-A", "-c", "3", NULL});
    run_case(false, 1, false, (const char *[]){"-F", "-p1", "-c", "10", NULL});
    const char *bad_sizes[] = {"0", "11", "-1", "1.5", "abc"};
    for (size_t i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); ++i) {
        char *args[] = {"turmite", "--cell-size", (char *)bad_sizes[i], NULL};
        optind = 0;
        assert(turmite_application_main(3, args) == 2);
        assert(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    }
    /* Headless capture and world storage must use the same reduced dimensions. */
    Universe headless = {0};
    headless.width = 161; headless.height = 121; headless.cell_size = 10;
    headless.headless = true; headless.workers = 1; headless.initial_ants = 2;
    headless.quantum = 32; headless.min_service = 16; headless.token_rate_divisor = 1;
    headless.dump_pages = 1; headless.dump_interval = 1;
    assert(universe_init(&headless, 123) == 0);
    assert(headless.world.width == 16 && headless.world.height == 12);
    assert(headless.colony.occupancy_cells == 192 && headless.dump.cells == 192);
    universe_destroy(&headless);
    alarm(0);
    puts("multi-monitor app ok: defaults, HUD flags, fullscreen modes, option precedence, main-thread SDL, shutdown");
    return 0;
}
