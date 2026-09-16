#ifndef TURMITE_WORLD_H
#define TURMITE_WORLD_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define TURMITE_COLORS 6

typedef struct {
    int width;
    int height;
    size_t cells;
    _Atomic uint8_t *data;
} World;

int world_init(World *world, int width, int height);
void world_destroy(World *world);
void world_clear(World *world);
uint8_t world_load(const World *world, size_t index);
void world_store(World *world, size_t index, uint8_t value);
size_t world_index(const World *world, int x, int y);

#endif
