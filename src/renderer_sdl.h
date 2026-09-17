#ifndef TURMITE_RENDERER_SDL_H
#define TURMITE_RENDERER_SDL_H

#include <SDL2/SDL.h>
#include "renderer.h"

/* Linux-only transport and developer HUD. No access to simulation objects. */
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
    size_t cells;
    int width;
    int height;
    int cell_size;
    int display_index;
    bool fullscreen;
    bool hud_visible;
} Renderer;

typedef struct {
    size_t population;
    size_t workers;
    size_t quantum;
    size_t min_service;
    uint32_t seed;
    double age;
    uint64_t collisions;
    bool paused;
} RendererHud;

int renderer_display_size(int display_index, int *width, int *height);
int renderer_init(Renderer *renderer, int width, int height, int cell_size,
                  bool hud_visible, int display_index, bool fullscreen);
void renderer_destroy(Renderer *renderer);
/* Synchronous: the caller may reuse frame storage after this returns. */
void renderer_render(Renderer *renderer, const RenderFrame *frame,
                     const RendererHud *hud);

#endif
