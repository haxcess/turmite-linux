#ifndef TURMITE_RENDERER_H
#define TURMITE_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "ant.h"
#include "scheduler.h"
#include "world.h"

/* SDL is an observer of the universe, not part of its execution model. The
 * renderer converts the persistent Linux ink plane into a texture and overlays
 * optional development metadata without feeding state back into the ants. */
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
    int width;
    int height;
    int cell_size;
    int display_index;
    bool fullscreen;
    bool hud_visible;
} Renderer;

/* Query the desktop pixel dimensions of one SDL display. This also performs
 * the minimal SDL video initialization needed before the world is allocated. */
int renderer_display_size(int display_index, int *width, int *height);

int renderer_init(Renderer *renderer, int width, int height, int cell_size,
                  bool hud_visible, int display_index, bool fullscreen);
void renderer_destroy(Renderer *renderer);
void renderer_render(Renderer *renderer, World *world, const AntColony *colony,
                     const Scheduler *scheduler, size_t workers, uint32_t seed, double universe_age,
                     uint64_t total_collisions, bool paused);
void renderer_handle_resize(Renderer *renderer, int width, int height);

#endif
