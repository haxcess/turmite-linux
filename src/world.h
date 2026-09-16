#ifndef TURMITE_WORLD_H
#define TURMITE_WORLD_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define TURMITE_COLORS 6

/* The Linux world carries two parallel planes:
 *   data[] is the six-color logical tape read by the automata;
 *   ink[] is persistent RGB presentation state used only by SDL.
 * Keeping them separate lets the visual ink drift over time without changing
 * the state machine seen by the ants. */
extern const uint32_t TURMITE_DISPLAY_BASE_PALETTE[TURMITE_COLORS];

typedef struct {
    int width;
    int height;
    size_t cells;

    /* Shared tape: each byte is one logical color index. */
    _Atomic uint8_t *data;

    /* Linux presentation plane. Each write snapshots the current RGB assigned
     * to that logical color, so old paper does not recolor retroactively. */
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

/* Hot-path write. Logical color and RGB ink are deliberately two relaxed
 * stores rather than a transaction, preserving the universe's last-writer-wins
 * concurrency while giving the renderer persistent historical color. */
static inline void world_store_inked(World *world, size_t index, uint8_t value)
{
    const uint8_t logical = (uint8_t)(value % TURMITE_COLORS);
    atomic_store_explicit(&world->data[index], logical, memory_order_relaxed);
    const uint32_t rgb = atomic_load_explicit(&world->ink_palette[logical], memory_order_relaxed);
    atomic_store_explicit(&world->ink[index], rgb, memory_order_relaxed);
}

#endif
