#ifndef TURMITE_DUMP_H
#define TURMITE_DUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ant.h"
#include "scheduler.h"
#include "world.h"

typedef struct {
    double age;
    uint64_t collisions;
    uint64_t dispatches;
    uint64_t total_instructions;
    uint64_t world_hash;
    uint32_t changed_cells;
    uint32_t rng_state;
    uint32_t active_population;
    uint32_t quantum;
    uint32_t min_service;
} DumpPageMeta;

typedef struct {
    uint32_t id;
    uint8_t enabled;
    uint8_t clobbered;
    uint8_t expired;
    uint8_t draining;
    int32_t x;
    int32_t y;
    uint8_t heading;
    uint8_t state;
    uint16_t rule_index;
    double tokens;
    double token_rate;
    double token_capacity;
    uint32_t weight;
    double fair_credit;
    uint64_t instructions;
    uint64_t mutations;
} DumpAntMeta;

typedef struct {
    uint8_t *world_pages;
    DumpPageMeta *page_meta;
    DumpAntMeta *ant_meta;
    size_t page_capacity;
    size_t page_count;
    size_t next_page;
    size_t cells;
    size_t ant_slots;
    double interval_seconds;
    double next_capture_at;
    uint32_t seed;
    int world_width;
    int world_height;
} DumpCapture;

int dump_capture_init(DumpCapture *capture, const World *world, uint32_t seed,
                      size_t requested_pages, double interval_seconds, double now);
void dump_capture_destroy(DumpCapture *capture);
void dump_capture_reset(DumpCapture *capture, const World *world, uint32_t seed, double now);
void dump_capture_maybe(DumpCapture *capture, const World *world, const AntColony *colony,
                        const Scheduler *scheduler, const Lfsr32 *rng, double age, double now);
void dump_capture_now(DumpCapture *capture, const World *world, const AntColony *colony,
                      const Scheduler *scheduler, const Lfsr32 *rng, double age, double now);
int dump_write(const DumpCapture *capture, const World *world, const AntColony *colony,
              const Scheduler *scheduler, size_t workers, size_t quantum,
              double lifetime_minutes, const char *output_root);

bool dump_pages_value_valid(size_t pages);
const char *dump_default_output_root(void);

#endif
