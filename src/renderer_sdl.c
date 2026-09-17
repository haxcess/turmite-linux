#include "renderer_sdl.h"

#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SDL transport consumes fixed-palette snapshots prepared by the host. */

/* Compact 5x7 font keeps the HUD dependency-free. Each row is five bits, MSB
 * on the left, and is expanded into filled SDL rectangles when drawn. */
typedef struct { char c; uint8_t rows[7]; } Glyph;

#define G(C,a,b,c,d,e,f,g) { C, {a,b,c,d,e,f,g} }
static const Glyph FONT[] = {
    G(' ',0,0,0,0,0,0,0),
    G(':',0,4,0,0,4,0,0),
    G('.',0,0,0,0,0,4,0),
    G('-',0,0,0,31,0,0,0),
    G('/',1,2,4,8,16,0,0),
    G('0',14,17,19,21,25,17,14),
    G('1',4,12,4,4,4,4,14),
    G('2',14,17,1,2,4,8,31),
    G('3',30,1,1,14,1,1,30),
    G('4',2,6,10,18,31,2,2),
    G('5',31,16,16,30,1,1,30),
    G('6',14,16,16,30,17,17,14),
    G('7',31,1,2,4,8,8,8),
    G('8',14,17,17,14,17,17,14),
    G('9',14,17,17,15,1,1,14),
    G('A',14,17,17,31,17,17,17),
    G('B',30,17,17,30,17,17,30),
    G('C',14,17,16,16,16,17,14),
    G('D',30,17,17,17,17,17,30),
    G('E',31,16,16,30,16,16,31),
    G('F',31,16,16,30,16,16,16),
    G('G',14,17,16,23,17,17,14),
    G('H',17,17,17,31,17,17,17),
    G('I',14,4,4,4,4,4,14),
    G('J',7,2,2,2,18,18,12),
    G('K',17,18,20,24,20,18,17),
    G('L',16,16,16,16,16,16,31),
    G('M',17,27,21,21,17,17,17),
    G('N',17,25,21,19,17,17,17),
    G('O',14,17,17,17,17,17,14),
    G('P',30,17,17,30,16,16,16),
    G('Q',14,17,17,17,21,18,13),
    G('R',30,17,17,30,20,18,17),
    G('S',15,16,16,14,1,1,30),
    G('T',31,4,4,4,4,4,4),
    G('U',17,17,17,17,17,17,14),
    G('V',17,17,17,17,17,10,4),
    G('W',17,17,17,21,21,27,17),
    G('X',17,17,10,4,10,17,17),
    G('Y',17,17,10,4,4,4,4),
    G('Z',31,1,2,4,8,16,31)
};
#undef G

static const Glyph *glyph(char c)
{
    for (size_t i = 0; i < sizeof(FONT)/sizeof(FONT[0]); ++i) {
        if (FONT[i].c == c) return &FONT[i];
    }
    return &FONT[0];
}

static void draw_text(SDL_Renderer *r, int x, int y, const char *text, int scale)
{
    SDL_SetRenderDrawColor(r, 235, 235, 240, 235);
    int cursor = x;
    for (const char *p = text; *p; ++p) {
        const Glyph *g = glyph(*p);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (g->rows[row] & (1u << (4 - col))) {
                    SDL_Rect px = { cursor + col * scale, y + row * scale, scale, scale };
                    SDL_RenderFillRect(r, &px);
                }
            }
        }
        cursor += 6 * scale;
    }
}

static int ensure_sdl_video(void)
{
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) return 0;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

int renderer_display_size(int display_index, int *width, int *height)
{
    if (!width || !height || display_index < 0) return -1;
    if (ensure_sdl_video() != 0) return -1;

    const int count = SDL_GetNumVideoDisplays();
    if (display_index >= count) {
        fprintf(stderr, "display %d does not exist (SDL reports %d display%s)\n",
                display_index, count, count == 1 ? "" : "s");
        return -1;
    }

    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(display_index, &bounds) != 0) {
        fprintf(stderr, "SDL_GetDisplayBounds(%d) failed: %s\n",
                display_index, SDL_GetError());
        return -1;
    }

    *width = bounds.w;
    *height = bounds.h;
    return 0;
}

int renderer_init(Renderer *renderer, int width, int height, int cell_size,
                  bool hud_visible, int display_index, bool fullscreen)
{
    if (!renderer) return -1;
    memset(renderer, 0, sizeof(*renderer));
    if (width <= 0 || height <= 0 || cell_size <= 0 ||
        width % cell_size || height % cell_size ||
        width / cell_size > INT_MAX / (int)sizeof(uint32_t)) return -1;
    const size_t cols = (size_t)(width / cell_size);
    const size_t rows = (size_t)(height / cell_size);
    if (cols > SIZE_MAX / rows || cols * rows > SIZE_MAX / sizeof(uint32_t)) return -1;
    renderer->cells = cols * rows;
    renderer->width = width;
    renderer->height = height;
    renderer->cell_size = cell_size;
    renderer->display_index = display_index;
    renderer->fullscreen = fullscreen;
    renderer->hud_visible = hud_visible;

    if (ensure_sdl_video() != 0) return -1;

    /* SDL display indices choose the monitor. Desktop fullscreen preserves the
     * monitor's native video mode; windowed mode is centered on the same display. */
    const int x = fullscreen ? SDL_WINDOWPOS_UNDEFINED_DISPLAY(display_index)
                             : SDL_WINDOWPOS_CENTERED_DISPLAY(display_index);
    const int y = fullscreen ? SDL_WINDOWPOS_UNDEFINED_DISPLAY(display_index)
                             : SDL_WINDOWPOS_CENTERED_DISPLAY(display_index);
    const Uint32 flags = SDL_WINDOW_SHOWN |
                         (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0u);

    renderer->window = SDL_CreateWindow("Turmite Universe", x, y, width, height, flags);
    if (!renderer->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        goto fail;
    }

    renderer->renderer = SDL_CreateRenderer(renderer->window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer->renderer)
        renderer->renderer = SDL_CreateRenderer(renderer->window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        goto fail;
    }

    SDL_SetRenderDrawBlendMode(renderer->renderer, SDL_BLENDMODE_BLEND);
    renderer->pixels = malloc(renderer->cells * sizeof(*renderer->pixels));
    if (!renderer->pixels) goto fail;

    /* renderer->pixels stores packed 0xAARRGGBB words. ARGB8888 matches that
     * integer layout; using RGBA8888 made the 0xFF alpha byte appear as red. */
    renderer->texture = SDL_CreateTexture(renderer->renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, width / cell_size, height / cell_size);
    if (!renderer->texture) goto fail;
    SDL_SetTextureScaleMode(renderer->texture, SDL_ScaleModeNearest);
    return 0;

fail:
    renderer_destroy(renderer);
    return -1;
}

void renderer_destroy(Renderer *renderer)
{
    if (!renderer) return;
    free(renderer->pixels);
    SDL_DestroyTexture(renderer->texture);
    SDL_DestroyRenderer(renderer->renderer);
    SDL_DestroyWindow(renderer->window);
    SDL_Quit();
    memset(renderer, 0, sizeof(*renderer));
}

void renderer_render(Renderer *renderer, const RenderFrame *frame,
                     const RendererHud *hud)
{
    if (!renderer || !renderer->texture || !hud || !frame ||
        frame->width != (size_t)(renderer->width / renderer->cell_size) ||
        frame->height != (size_t)(renderer->height / renderer->cell_size)) return;
    if (!render_argb(frame, RENDER_BASE_PALETTE, renderer->pixels, renderer->cells)) return;

    SDL_UpdateTexture(renderer->texture, NULL, renderer->pixels,
                      (int)(frame->width * sizeof(*renderer->pixels)));
    SDL_RenderClear(renderer->renderer);
    SDL_Rect dst = {0, 0, renderer->width, renderer->height};
    SDL_RenderCopy(renderer->renderer, renderer->texture, NULL, &dst);

    if (renderer->hud_visible) {
        SDL_Rect panel = { 12, 12, 310, 124 };
        SDL_SetRenderDrawColor(renderer->renderer, 0, 0, 0, 150);
        SDL_RenderFillRect(renderer->renderer, &panel);

        char line[128];
        snprintf(line, sizeof(line), "ANTS %zu / 32", hud->population);
        draw_text(renderer->renderer, 24, 22, line, 2);
        snprintf(line, sizeof(line), "WORKERS %zu  Q %zu  MIN %zu", hud->workers, hud->quantum, hud->min_service);
        draw_text(renderer->renderer, 24, 42, line, 2);
        snprintf(line, sizeof(line), "WFQ  AGE %05.1F", hud->age);
        draw_text(renderer->renderer, 24, 62, line, 2);
        snprintf(line, sizeof(line), "SEED %08X", hud->seed);
        draw_text(renderer->renderer, 24, 82, line, 2);
        snprintf(line, sizeof(line), "COLL %llu", (unsigned long long)hud->collisions);
        draw_text(renderer->renderer, 24, 102, line, 2);
        draw_text(renderer->renderer, 24, 122, hud->paused ? "PAUSED" : "RUNNING", 2);
    }

    SDL_RenderPresent(renderer->renderer);
}

