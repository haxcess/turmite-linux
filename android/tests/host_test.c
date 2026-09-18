#include "engine.h"
#include "renderer.h"
#include <stdio.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void)
{
    uint32_t pixels[64 * 48];
    CHECK(!android_engine_create(0, 48, 1, 8, 1));
    CHECK(!android_engine_create(961, 48, 1, 8, 1));
    CHECK(!android_engine_create(64, 48, 1, 1, 1));
    CHECK(!android_engine_create(64, 48, 1, 9, 1));
    CHECK(!android_engine_create(64, 48, 1, 2, 0));
    CHECK(!android_engine_create(64, 48, 1, 2, 101));
    CHECK(!android_engine_frame(NULL, pixels, 64 * 48));
    android_engine_destroy(NULL);
    AndroidEngine *survivor = android_engine_create(64, 48, 123, 8, 1);
    CHECK(survivor);
    for (unsigned cycle = 0; cycle < 30; ++cycle) {
        int ants = 2 + (int)(cycle % 7);
        int divisor = cycle % 2 ? 100 : 1;
        AndroidEngine *engine = android_engine_create(64, 48, cycle + 1, ants, divisor);
        CHECK(engine);
        CHECK(android_engine_population(engine) == (size_t)ants);
        CHECK(android_engine_divisor(engine) == (uint32_t)divisor);
        CHECK(!android_engine_frame(engine, pixels, 1));
        CHECK(!android_engine_frame(engine, NULL, 64 * 48));
        for (unsigned attempt = 0; attempt < 200 && android_engine_instructions(engine) == 0; ++attempt) {
            struct timespec delay = { 0, 1000000 };
            nanosleep(&delay, NULL);
        }
        CHECK(android_engine_instructions(engine) > 0);
        for (unsigned frame = 0; frame < 8; ++frame) {
            CHECK(android_engine_frame(engine, pixels, 64 * 48));
            for (size_t i = 0; i < 64 * 48; ++i) {
                bool valid = false;
                for (size_t color = 0; color < TURMITE_COLORS; ++color)
                    if (pixels[i] == (UINT32_C(0xff000000) | RENDER_BASE_PALETTE[color])) valid = true;
                CHECK(valid);
            }
        }
        android_engine_destroy(engine);
    }
    // Destroying other instances must leave this independent worker pool usable.
    CHECK(android_engine_frame(survivor, pixels, 64 * 48));
    CHECK(android_engine_instructions(survivor) > 0);
    android_engine_destroy(survivor);
    puts("Android native host: repeated create/render/destroy, worker progress, palette, bounds OK");
    return 0;
}
