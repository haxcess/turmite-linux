#ifndef TURMITE_RENDERER_H
#define TURMITE_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "ant.h"
#include "scheduler.h"
#include "world.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
    int width;
    int height;
    int cell_size;
    bool hud_visible;
} Renderer;

int renderer_init(Renderer *renderer, int width, int height, int cell_size, bool hud_visible);
void renderer_destroy(Renderer *renderer);
void renderer_render(Renderer *renderer, const World *world, const AntColony *colony,
                     const Scheduler *scheduler, size_t workers, uint32_t seed, double universe_age,
                     uint64_t total_collisions, bool paused);
void renderer_handle_resize(Renderer *renderer, int width, int height);

#endif
