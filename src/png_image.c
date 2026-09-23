#include "png_image.h"

#include <png.h>
#include <stdio.h>
#include <time.h>

int png_image_write(const char *path, const RenderFrame *frame,
                    const uint32_t palette[TURMITE_COLORS])
{
    if (!path || !palette || !frame || !frame->colors || !frame->width || !frame->height ||
        frame->width > PNG_UINT_31_MAX || frame->height > PNG_UINT_31_MAX ||
        frame->width > SIZE_MAX / frame->height) return -1;
    for (size_t i = 0; i < frame->width * frame->height; ++i)
        if (frame->colors[i] >= TURMITE_COLORS) return -1;
    FILE *file = fopen(path, "wb");
    if (!file) { perror("PNG output"); return -1; }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info) {
        if (png) png_destroy_write_struct(&png, NULL);
        fclose(file);
        return -1;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(file);
        return -1;
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, (png_uint_32)frame->width, (png_uint_32)frame->height,
                 4, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_color colors[TURMITE_COLORS];
    for (size_t i = 0; i < TURMITE_COLORS; ++i) {
        colors[i] = (png_color){(palette[i] >> 16) & 255u,
                               (palette[i] >> 8) & 255u, palette[i] & 255u};
    }
    png_set_PLTE(png, info, colors, TURMITE_COLORS);
    /* Match the example's sRGB intent, gamma/chromaticity, background and DPI. */
    png_set_sRGB_gAMA_and_cHRM(png, info, PNG_sRGB_INTENT_RELATIVE);
    png_set_pHYs(png, info, 11811, 11811, PNG_RESOLUTION_METER);
    png_color_16 background = {0};
    png_set_bKGD(png, info, &background);
    time_t now = time(NULL);
    struct tm utc;
    if (gmtime_r(&now, &utc)) {
        png_time timestamp;
        png_convert_from_struct_tm(&timestamp, &utc);
        png_set_tIME(png, info, &timestamp);
    }
    png_set_compression_level(png, 9);
    png_write_info(png, info);
    /* Input remains one index per byte; libpng packs two pixels per byte. */
    png_set_packing(png);
    for (size_t y = 0; y < frame->height; ++y)
        png_write_row(png, frame->colors + y * frame->width);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    if (fclose(file) != 0) { perror("PNG close"); return -1; }
    return 0;
}
