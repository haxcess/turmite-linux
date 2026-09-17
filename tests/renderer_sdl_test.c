#include "renderer_sdl.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    Renderer renderer;
    assert(renderer_init(&renderer, 3, 2, 1, false, 0, false) == 0);
    uint8_t colors[6] = {0, 1, 2, 3, 4, 5};
    const RenderFrame frame = {3, 2, colors};
    RendererHud hud = {0};
    renderer_render(&renderer, &frame, &hud);
    uint32_t pixels[6];
    assert(SDL_RenderReadPixels(renderer.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                pixels, 3 * (int)sizeof(*pixels)) == 0);
    for (size_t i = 0; i < 6; ++i)
        assert(pixels[i] == (0xff000000u | RENDER_BASE_PALETTE[i]));

    /* Age cannot recolor fixed-palette output. A cleared/restarted universe
     * replaces the entire frame without retaining old presentation state. */
    hud.age = 12345.0;
    for (size_t i = 0; i < 6; ++i) colors[i] = 0;
    renderer_render(&renderer, &frame, &hud);
    assert(SDL_RenderReadPixels(renderer.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                pixels, 3 * (int)sizeof(*pixels)) == 0);
    for (size_t i = 0; i < 6; ++i) assert(pixels[i] == (0xff000000u | RENDER_BASE_PALETTE[0]));
    renderer_destroy(&renderer);
    renderer_shutdown();
    puts("SDL render ok: fixed-palette readback and cleared frame");
    return 0;
}
