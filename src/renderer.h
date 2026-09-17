#ifndef TURMITE_RENDERER_H
#define TURMITE_RENDERER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "colors.h"

/* Caller-owned snapshot: one logical index (0..5) per cell, row-major.
 * No SDL, RTOS, World, scheduler, allocation, or transport dependency.
 * colors must contain width*height bytes and stay unchanged while consumed.
 * Async/DMA backends must retain the snapshot or copy it before returning
 * control to a producer that will reuse the storage.
 */
typedef struct {
    size_t width;
    size_t height;
    const uint8_t *colors;
} RenderFrame;

/* Fixed Linux preview palette. Panel drivers supply their own color mapping. */
extern const uint32_t RENDER_BASE_PALETTE[TURMITE_COLORS];

/* Validate dimensions/storage and return the cell count, or zero on error. */
size_t render_frame_cells(const RenderFrame *frame);

/* Allocation-free conversion into caller-owned buffers. Capacity is in cells.
 * Invalid indices map to logical zero. Invalid frame/pointers/capacity return
 * false without changing output. Input, palette and output must not overlap.
 */
bool render_argb(const RenderFrame *frame,
                 const uint32_t palette[TURMITE_COLORS],
                 uint32_t *pixels, size_t capacity);

/* Map indices to display-specific byte codes. Packing, commands, refresh
 * scheduling, and DMA/cache ownership belong to the eventual panel backend.
 * Input, mapping and output must not overlap.
 */
bool render_codes(const RenderFrame *frame,
                  const uint8_t codes[TURMITE_COLORS],
                  uint8_t *pixels, size_t capacity);

#endif
