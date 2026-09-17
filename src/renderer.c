#include "renderer.h"

const uint32_t RENDER_BASE_PALETTE[TURMITE_COLORS] = { // From Solarized palette, https://ethanschoonover.com/solarized/
    0x073642u, /* black */
    0x586E75u, /* white */
    0xDC322Fu, /* red */
    0xB58900u, /* yellow */
    0x859900u, /* green */
    0x288BD2u  /* blue */
};

size_t render_frame_cells(const RenderFrame *frame)
{
    if (!frame || !frame->colors || !frame->width || !frame->height ||
        frame->width > SIZE_MAX / frame->height)
        return 0;
    return frame->width * frame->height;
}

static uint8_t color_index(uint8_t color)
{
    return color < TURMITE_COLORS ? color : 0;
}

bool render_argb(const RenderFrame *frame,
                 const uint32_t palette[TURMITE_COLORS],
                 uint32_t *pixels, size_t capacity)
{
    const size_t count = render_frame_cells(frame);
    if (!count || !palette || !pixels || capacity < count ||
        count > SIZE_MAX / sizeof(*pixels)) return false;
    for (size_t i = 0; i < count; ++i)
        pixels[i] = 0xff000000u | (palette[color_index(frame->colors[i])] & 0xffffffu);
    return true;
}

bool render_codes(const RenderFrame *frame,
                  const uint8_t codes[TURMITE_COLORS],
                  uint8_t *pixels, size_t capacity)
{
    const size_t count = render_frame_cells(frame);
    if (!count || !codes || !pixels || capacity < count) return false;
    for (size_t i = 0; i < count; ++i)
        pixels[i] = codes[color_index(frame->colors[i])];
    return true;
}
