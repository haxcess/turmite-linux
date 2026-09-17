#include "world.h"

#include <stdlib.h>

int world_init(World *world, int width, int height)
{
    if (!world || width <= 0 || height <= 0 || width > 65535 || height > 65535) return -1;
    *world = (World){ .width = width, .height = height,
                      .cells = (size_t)width * (size_t)height };
    world->data = calloc(world->cells, sizeof(*world->data));
    if (!world->data) {
        *world = (World){0};
        return -1;
    }
    for (size_t i = 0; i < world->cells; ++i)
        atomic_init(&world->data[i], 0);
    return 0;
}

void world_destroy(World *world)
{
    if (!world) return;
    free(world->data);
    *world = (World){0};
}

void world_clear(World *world)
{
    if (!world || !world->data) return;
    for (size_t i = 0; i < world->cells; ++i)
        atomic_store_explicit(&world->data[i], 0u, memory_order_relaxed);
}

uint8_t world_load(const World *world, size_t index)
{
    if (!world || index >= world->cells) return 0;
    return atomic_load_explicit(&world->data[index], memory_order_relaxed);
}

void world_store(World *world, size_t index, uint8_t value)
{
    if (!world || index >= world->cells) return;
    world_store_cell(world, index, value);
}

/* Coordinates wrap toroidally; the executor uses equivalent branch wrapping. */
size_t world_index(const World *world, int x, int y)
{
    int xx = x % world->width;
    int yy = y % world->height;
    if (xx < 0) xx += world->width;
    if (yy < 0) yy += world->height;
    return (size_t)yy * (size_t)world->width + (size_t)xx;
}
