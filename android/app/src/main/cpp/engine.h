#ifndef TURMITE_ANDROID_ENGINE_H
#define TURMITE_ANDROID_ENGINE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct AndroidEngine AndroidEngine;
// Single caller owns create/frame/destroy; internal ant workers run concurrently.
AndroidEngine *android_engine_create(int width, int height, uint32_t seed, int ants, int divisor);
bool android_engine_frame(AndroidEngine *engine, uint32_t *argb, size_t capacity);
size_t android_engine_population(const AndroidEngine *engine);
uint32_t android_engine_divisor(const AndroidEngine *engine);
uint64_t android_engine_instructions(const AndroidEngine *engine);
void android_engine_destroy(AndroidEngine *engine);
#endif
