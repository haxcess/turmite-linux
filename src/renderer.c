#include "renderer.h"
#include "rng.h"
 
const uint32_t RENDER_BASE_PALETTE[TURMITE_COLORS] = { // From Solarized palette, https://ethanschoonover.com/solarized/
    0x073642u, /* black */
    0x586E75u, /* white */
    0xDC322Fu, /* red */
    0xB58900u, /* yellow */
    0x859900u, /* green */
    0x288BD2u  /* blue */
};

/* Hue in degrees, saturation/value in [0,1]; no libm dependency. */
static uint32_t hsv_rgb(float hue, float saturation, float value)
{
    float sector = hue / 60.0f;
    unsigned i = (unsigned)sector;
    float f = sector - (float)i;
    float p = value * (1.0f - saturation);
    float q = value * (1.0f - saturation * f);
    float t = value * (1.0f - saturation * (1.0f - f));
    float r, g, b;
    switch (i) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        default: r = value; g = p; b = q; break;
    }
    return ((uint32_t)(r * 255.0f + 0.5f) << 16) |
           ((uint32_t)(g * 255.0f + 0.5f) << 8) |
           (uint32_t)(b * 255.0f + 0.5f);
}

static float palette_unit(uint32_t *state)
{
    *state = lfsr32_advance(*state);
    /* 24 bits prevent float rounding up to 1. */
    return (float)(*state >> 8) / 16777216.0f;
}

void render_palette_init(uint32_t palette[TURMITE_COLORS],
                         RenderPaletteMode mode, uint32_t seed)
{
    const float saturation = 0.8f;
    const float value = 0.7f;
    const float background_value = 0.2f;
    uint32_t state = seed;
    /* Warm the private stream so small explicit seeds yield varied hues. */
    for (unsigned i = 0; i < 32; ++i) state = lfsr32_advance(state);
    float start = mode == RENDER_PALETTE_RANDOMISH ? palette_unit(&state) * 360.0f : 0.0f;
    for (size_t i = 0; i < TURMITE_COLORS; ++i) {
        if (mode == RENDER_PALETTE_DEFAULT) {
            palette[i] = RENDER_BASE_PALETTE[i];
            continue;
        }
        /* Include both ends of the arc: six hues are 32 degrees apart. */
        float hue = mode == RENDER_PALETTE_RANDOMISH
                  ? start + 160.0f * (float)i / (float)(TURMITE_COLORS - 1)
                  : palette_unit(&state) * 360.0f;
        if (hue >= 360.0f) hue -= 360.0f;
        float s = saturation;
        if (mode == RENDER_PALETTE_RANDOMISH)
            s *= 0.8f + 0.4f * palette_unit(&state);
        palette[i] = hsv_rgb(hue, s, i == 0 ? background_value : value);
    }
}

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
    uint32_t argb[TURMITE_COLORS];
    for (size_t i = 0; i < TURMITE_COLORS; ++i)
        argb[i] = 0xff000000u | (palette[i] & 0xffffffu);
    for (size_t i = 0; i < count; ++i)
        pixels[i] = argb[color_index(frame->colors[i])];
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
