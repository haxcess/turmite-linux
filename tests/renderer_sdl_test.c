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
    /* SDL scales the small texture to the entire unchanged output rectangle. */
    for (int extra = 0; extra <= 1; ++extra) {
        int width = 6 + extra, height = 4 + extra;
        assert(renderer_init(&renderer, width, height, 2, false, 0, false) == 0);
        for (size_t i = 0; i < 6; ++i) colors[i] = (uint8_t)i;
        renderer_render(&renderer, &frame, &hud);
        uint32_t scaled[35];
        assert(SDL_RenderReadPixels(renderer.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                    scaled, width * (int)sizeof(*scaled)) == 0);
        assert(scaled[0] == (0xff000000u | RENDER_BASE_PALETTE[0]));
        assert(scaled[width-1] == (0xff000000u | RENDER_BASE_PALETTE[2]));
        assert(scaled[(height-1)*width] == (0xff000000u | RENDER_BASE_PALETTE[3]));
        assert(scaled[height*width-1] == (0xff000000u | RENDER_BASE_PALETTE[5]));
        if (!extra) for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
            assert(scaled[y*width+x] == (0xff000000u | RENDER_BASE_PALETTE[(y/2)*3+x/2]));
        renderer_destroy(&renderer);
    }
    assert(renderer_init(&renderer, 1, 1, 2, false, 0, false) == -1);
    renderer_shutdown();
    puts("SDL render ok: fixed-palette readback and cleared frame");
    return 0;
}
