#include "world.h"

#include <stdlib.h>

const uint32_t TURMITE_DISPLAY_BASE_PALETTE[TURMITE_COLORS] = {
    0x09090Du, /* near-black */
    0xF2F2F2u, /* white */
    0xE84A5Fu, /* red */
    0xF5D547u, /* yellow */
    0x58D68Du, /* green */
    0x4EA5D9u  /* blue */
};

int world_init(World *world, int width, int height)
{
    if (!world || width <= 0 || height <= 0 || width > 65535 || height > 65535) return -1;
    world->width = width;
    world->height = height;
    world->cells = (size_t)width * (size_t)height;
    world->data = calloc(world->cells, sizeof(*world->data));
    world->ink = calloc(world->cells, sizeof(*world->ink));
    if (!world->data || !world->ink) {
        free(world->data);
        free(world->ink);
        world->data = NULL;
        world->ink = NULL;
        world->cells = 0;
        return -1;
    }

    for (size_t i = 0; i < TURMITE_COLORS; ++i)
        atomic_init(&world->ink_palette[i], TURMITE_DISPLAY_BASE_PALETTE[i]);

    world_clear(world);
    return 0;
}

void world_destroy(World *world)
{
    if (!world) return;
    free(world->data);
    free(world->ink);
    world->data = NULL;
    world->ink = NULL;
    world->cells = 0;
    world->width = world->height = 0;
}

void world_clear(World *world)
{
    if (!world || !world->data || !world->ink) return;
    const uint32_t background = atomic_load_explicit(&world->ink_palette[0], memory_order_relaxed);
    for (size_t i = 0; i < world->cells; ++i) {
        atomic_store_explicit(&world->data[i], 0u, memory_order_relaxed);
        atomic_store_explicit(&world->ink[i], background, memory_order_relaxed);
    }
}

uint8_t world_load(const World *world, size_t index)
{
    if (!world || index >= world->cells) return 0;
    return atomic_load_explicit(&world->data[index], memory_order_relaxed);
}

void world_store(World *world, size_t index, uint8_t value)
{
    if (!world || index >= world->cells) return;
    world_store_inked(world, index, value);
}

uint32_t world_ink_load(const World *world, size_t index)
{
    if (!world || !world->ink || index >= world->cells) return TURMITE_DISPLAY_BASE_PALETTE[0];
    return atomic_load_explicit(&world->ink[index], memory_order_relaxed);
}

void world_set_ink_palette(World *world, const uint32_t palette[TURMITE_COLORS])
{
    if (!world || !palette) return;
    for (size_t i = 0; i < TURMITE_COLORS; ++i)
        atomic_store_explicit(&world->ink_palette[i], palette[i] & 0x00ffffffu, memory_order_relaxed);
}

size_t world_index(const World *world, int x, int y)
{
    int xx = x % world->width;
    int yy = y % world->height;
    if (xx < 0) xx += world->width;
    if (yy < 0) yy += world->height;
    return (size_t)yy * (size_t)world->width + (size_t)xx;
}
