#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t PALETTE[TURMITE_COLORS] = {
    0x09090Du, /* near-black */
    0xF2F2F2u, /* white */
    0xE84A5Fu, /* red */
    0xF5D547u, /* yellow */
    0x58D68Du, /* green */
    0x4EA5D9u  /* blue */
};

/* Compact 5x7 font for the development HUD. Each row is 5 bits, MSB on the left. */
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

int renderer_init(Renderer *renderer, int width, int height, int cell_size, bool hud_visible)
{
    memset(renderer, 0, sizeof(*renderer));
    renderer->width = width;
    renderer->height = height;
    renderer->cell_size = cell_size;
    renderer->hud_visible = hud_visible;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    renderer->window = SDL_CreateWindow(
        "Turmite Universe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, SDL_WINDOW_SHOWN);
    if (!renderer->window) return -1;

    renderer->renderer = SDL_CreateRenderer(renderer->window, -1,
        SDL_RENDERER_ACCELERATED);
    if (!renderer->renderer) return -1;

    SDL_SetRenderDrawBlendMode(renderer->renderer, SDL_BLENDMODE_BLEND);
    renderer->pixels = malloc((size_t)(width / cell_size) * (size_t)(height / cell_size) * sizeof(uint32_t));
    if (!renderer->pixels) return -1;

    renderer->texture = SDL_CreateTexture(renderer->renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, width / cell_size, height / cell_size);
    if (!renderer->texture) return -1;
    SDL_SetTextureScaleMode(renderer->texture, SDL_ScaleModeNearest);
    return 0;
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

void renderer_render(Renderer *renderer, const World *world, const AntColony *colony,
                     const Scheduler *scheduler, size_t workers, uint32_t seed, double universe_age,
                     uint64_t total_collisions, bool paused)
{
    const int n = world->width * world->height;
    for (int i = 0; i < n; ++i) {
        uint8_t c = world_load(world, (size_t)i);
        uint32_t rgb = PALETTE[c % TURMITE_COLORS];
        renderer->pixels[i] = 0xFF000000u | rgb;
    }

    SDL_UpdateTexture(renderer->texture, NULL, renderer->pixels, world->width * (int)sizeof(uint32_t));
    SDL_RenderClear(renderer->renderer);
    SDL_Rect dst = {0, 0, renderer->width, renderer->height};
    SDL_RenderCopy(renderer->renderer, renderer->texture, NULL, &dst);

    if (renderer->hud_visible) {
        SDL_Rect panel = { 12, 12, 310, 124 };
        SDL_SetRenderDrawColor(renderer->renderer, 0, 0, 0, 150);
        SDL_RenderFillRect(renderer->renderer, &panel);

        char line[128];
        snprintf(line, sizeof(line), "ANTS %zu / 32", atomic_load_explicit(&colony->active_population, memory_order_relaxed));
        draw_text(renderer->renderer, 24, 22, line, 2);
        snprintf(line, sizeof(line), "WORKERS %zu  Q %zu  MIN %zu", workers,
                 scheduler_get_quantum((Scheduler *)scheduler),
                 scheduler_get_min_service((Scheduler *)scheduler));
        draw_text(renderer->renderer, 24, 42, line, 2);
        snprintf(line, sizeof(line), "WFQ  AGE %05.1F", universe_age);
        draw_text(renderer->renderer, 24, 62, line, 2);
        snprintf(line, sizeof(line), "SEED %08X", seed);
        draw_text(renderer->renderer, 24, 82, line, 2);
        snprintf(line, sizeof(line), "COLL %llu", (unsigned long long)total_collisions);
        draw_text(renderer->renderer, 24, 102, line, 2);
        draw_text(renderer->renderer, 24, 122, paused ? "PAUSED" : "RUNNING", 2);
    }

    SDL_RenderPresent(renderer->renderer);
}

void renderer_handle_resize(Renderer *renderer, int width, int height)
{
    (void)renderer;
    (void)width;
    (void)height;
}
