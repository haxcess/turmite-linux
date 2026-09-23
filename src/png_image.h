#ifndef TURMITE_PNG_IMAGE_H
#define TURMITE_PNG_IMAGE_H
#include "renderer.h"

/* Debug-only disk export: 4-bit indexed PNG, universe RGB palette, no alpha.
 * One image pixel per logical cell. Caller supplies a stable snapshot. */
int png_image_write(const char *path, const RenderFrame *frame,
                    const uint32_t palette[TURMITE_COLORS]);
#endif
