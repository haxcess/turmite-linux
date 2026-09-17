#include "renderer.h"
#include "world.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
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
