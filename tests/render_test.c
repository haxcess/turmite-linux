#include "renderer.h"
#include "world.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Recover HSV from quantized RGB to check the user-visible constraints. */
static float palette_hue(uint32_t rgb, float *saturation, unsigned *value)
{
    float r = (float)((rgb >> 16) & 255), g = (float)((rgb >> 8) & 255);
    float b = (float)(rgb & 255);
    float hi = r > g ? r : g, lo = r < g ? r : g;
    if (b > hi) hi = b;
    if (b < lo) lo = b;
    float delta = hi - lo;
    assert(delta > 0);
    *saturation = delta / hi;
    *value = (unsigned)hi;
    float h = hi == r ? (g - b) / delta :
              hi == g ? 2.0f + (b - r) / delta : 4.0f + (r - g) / delta;
    h *= 60.0f;
    return h < 0 ? h + 360.0f : h;
}

static void test_palettes(void)
{
    uint32_t palette[TURMITE_COLORS], repeat[TURMITE_COLORS];
    render_palette_init(palette, RENDER_PALETTE_DEFAULT, 0);
    assert(memcmp(palette, RENDER_BASE_PALETTE, sizeof(palette)) == 0);
    for (int mode = RENDER_PALETTE_RANDOM; mode <= RENDER_PALETTE_RANDOMISH; ++mode) {
        bool sectors[6] = {false};
        for (uint32_t seed = 0; seed < 1024; ++seed) {
            render_palette_init(palette, (RenderPaletteMode)mode, seed);
            render_palette_init(repeat, (RenderPaletteMode)mode, seed);
            assert(memcmp(palette, repeat, sizeof(palette)) == 0);
            float hues[TURMITE_COLORS];
            for (size_t i = 0; i < TURMITE_COLORS; ++i) {
                float sat;
                unsigned value;
                assert((palette[i] & 0xff000000u) == 0);
                hues[i] = palette_hue(palette[i], &sat, &value);
                sectors[(unsigned)(hues[i] / 60.0f)] = true;
                assert(value == (i == 0 ? 51u : 230u));
                /* Background channels are capped far below every other
                 * color's brightest channel, regardless of selected hue. */
                if (i > 0) {
                    for (unsigned shift = 0; shift <= 16; shift += 8)
                        assert(((palette[0] >> shift) & 255u) < value / 4);
                }
                if (mode == RENDER_PALETTE_RANDOM) assert(sat > 0.795f && sat < 0.805f);
                else assert(sat >= 0.625f && sat <= 0.975f);
            }
            if (mode == RENDER_PALETTE_RANDOMISH) {
                /* Check spacing after RGB quantization, including hue wrap. */
                for (size_t i = 1; i < TURMITE_COLORS; ++i) {
                    float gap = hues[i] - hues[i - 1];
                    if (gap < 0) gap += 360;
                    assert(gap > 30.0f && gap < 34.0f);
                }
                float span = hues[TURMITE_COLORS - 1] - hues[0];
                if (span < 0) span += 360;
                assert(span > 158.0f && span < 162.0f);
            }
        }
        for (unsigned i = 0; i < 6; ++i) assert(sectors[i]);
        render_palette_init(palette, (RenderPaletteMode)mode, 12345);
        render_palette_init(repeat, (RenderPaletteMode)mode, 98765);
        assert(memcmp(palette, repeat, sizeof(palette)) != 0);
    }
}

int main(void)
{
    test_palettes();
    World world;
    assert(world_init(&world, 3, 2) == 0);
    uint8_t snapshot[6];
    for (size_t i = 0; i < world.cells; ++i) {
        world_store(&world, i, (uint8_t)i);
        snapshot[i] = world_load(&world, i);
    }
    const RenderFrame frame = {3, 2, snapshot};
    /* Conversion uses a fixed fixture, independent of the user's preview palette. */
    const uint32_t palette[TURMITE_COLORS] = {
        0x010203u, 0x102030u, 0x334455u, 0x667788u, 0x99aabbu, 0xccddeeu
    };
    const uint32_t expected[] = {
        0xff010203u, 0xff102030u, 0xff334455u,
        0xff667788u, 0xff99aabbu, 0xffccddeeu
    };
    uint32_t rgb[6];
    assert(render_argb(&frame, palette, rgb, 6));
    assert(memcmp(rgb, expected, sizeof(rgb)) == 0);

    /* A backend can use a completely different panel encoding without RGB.
     * These are test codes, not the protocol of a selected e-paper panel. */
    const uint8_t codes[6] = {7, 4, 9, 1, 12, 3};
    uint8_t encoded[6];
    assert(render_codes(&frame, codes, encoded, 6));
    assert(memcmp(encoded, codes, sizeof(encoded)) == 0);

    /* Captured storage is stable while the simulation moves on. */
    world_clear(&world);
    assert(render_argb(&frame, palette, rgb, 6));
    assert(memcmp(rgb, expected, sizeof(rgb)) == 0);
    for (size_t i = 0; i < world.cells; ++i)
        snapshot[i] = world_load(&world, i);
    assert(render_argb(&frame, palette, rgb, 6));
    for (size_t i = 0; i < 6; ++i) assert(rgb[i] == expected[0]);

    /* A custom palette belongs to rendering; the world stays unchanged. */
    const uint32_t custom[6] = {0x123456u, 0, 0, 0, 0, 0};
    assert(render_argb(&frame, custom, rgb, 6));
    for (size_t i = 0; i < 6; ++i) {
        assert(rgb[i] == 0xff123456u);
        assert(world_load(&world, i) == 0);
    }
    snapshot[0] = 255;
    assert(render_codes(&frame, codes, encoded, 6));
    assert(encoded[0] == codes[0]);
    assert(render_argb(&frame, custom, rgb, 6));
    assert(rgb[0] == 0xff123456u);
    assert(snapshot[0] == 255);

    /* Reject malformed frames/small buffers before writing any output. */
    memset(rgb, 0x5a, sizeof(rgb));
    memset(encoded, 0x5a, sizeof(encoded));
    assert(!render_argb(&frame, custom, rgb, 5));
    assert(!render_codes(&frame, codes, encoded, 5));
    const RenderFrame overflow = {SIZE_MAX, 2, snapshot};
    const RenderFrame empty = {0, 2, snapshot};
    assert(render_frame_cells(&overflow) == 0);
    assert(render_frame_cells(&empty) == 0);
    assert(!render_argb(&overflow, custom, rgb, 6));
    assert(!render_codes(&overflow, codes, encoded, 6));
    assert(!render_argb(NULL, custom, rgb, 6));
    assert(!render_codes(&frame, NULL, encoded, 6));
    assert(!render_argb(&frame, NULL, rgb, 6));
    assert(!render_codes(NULL, codes, encoded, 6));
    for (size_t i = 0; i < 6; ++i) {
        assert(rgb[i] == 0x5a5a5a5au);
        assert(encoded[i] == 0x5a);
    }
    world_destroy(&world);
    puts("render ok: six colors, panel mapping, snapshot isolation, reset, bounds");
    return 0;
}
