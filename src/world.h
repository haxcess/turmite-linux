#ifndef TURMITE_WORLD_H
#define TURMITE_WORLD_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define TURMITE_COLORS 6

/* Linux presentation colors. These are deliberately separate from the six
 * logical tape values: the simulation reads/writes only data[], while ink[]
 * remembers the RGB that was physically "dropped" on a cell at write time. */
extern const uint32_t TURMITE_DISPLAY_BASE_PALETTE[TURMITE_COLORS];

typedef struct {
    int width;
    int height;
    size_t cells;

    /* Logical paper consumed by turmite rules. */
    _Atomic uint8_t *data;

    /* Linux-only visual paper. Each write snapshots the current RGB for that
     * logical color; a rendered cell does not change again until overwritten. */
    _Atomic uint32_t *ink;
    _Atomic uint32_t ink_palette[TURMITE_COLORS];
} World;

int world_init(World *world, int width, int height);
void world_destroy(World *world);
void world_clear(World *world);
uint8_t world_load(const World *world, size_t index);
void world_store(World *world, size_t index, uint8_t value);
uint32_t world_ink_load(const World *world, size_t index);
void world_set_ink_palette(World *world, const uint32_t palette[TURMITE_COLORS]);
size_t world_index(const World *world, int x, int y);

/* Hot-path write used by the ant interpreter. The logical six-color write and
 * the visible ink deposit are intentionally independent relaxed stores; they
 * are not a transaction, matching the universe's existing race semantics. */
static inline void world_store_inked(World *world, size_t index, uint8_t value)
{
    const uint8_t logical = (uint8_t)(value % TURMITE_COLORS);
    atomic_store_explicit(&world->data[index], logical, memory_order_relaxed);
    const uint32_t rgb = atomic_load_explicit(&world->ink_palette[logical], memory_order_relaxed);
    atomic_store_explicit(&world->ink[index], rgb, memory_order_relaxed);
}

#endif
