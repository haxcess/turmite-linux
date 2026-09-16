#include "dump.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "rules.h"

static uint64_t fnv1a64(const uint8_t *data, size_t n)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < n; ++i) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint32_t count_changed(const uint8_t *a, const uint8_t *b, size_t n)
{
    if (!b) return 0;
    uint32_t changed = 0;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) ++changed;
    }
    return changed;
}

static size_t round_down_power_of_two_minus_one(size_t pages)
{
    /* Accepted values are 1,3,7,...,1023. Caller validates. */
    return pages;
}

bool dump_pages_value_valid(size_t pages)
{
    if (pages == 0 || pages > 1023) return false;
    return ((pages + 1) & pages) == 0;
}

const char *dump_default_output_root(void)
{
    return "turmite-dumps";
}

int dump_capture_init(DumpCapture *capture, const World *world, uint32_t seed,
                      size_t requested_pages, double interval_seconds, double now)
{
    if (!capture || !world || !world->data || !dump_pages_value_valid(requested_pages) || interval_seconds <= 0.0) {
        return -1;
    }
    memset(capture, 0, sizeof(*capture));
    const size_t pages = round_down_power_of_two_minus_one(requested_pages);
    capture->cells = world->cells;
    capture->ant_slots = TURMITE_MAX_ANTS;
    capture->page_capacity = pages;
    capture->interval_seconds = interval_seconds;
    capture->next_capture_at = now;
    capture->seed = seed;
    capture->world_width = world->width;
    capture->world_height = world->height;

    capture->world_pages = calloc(pages, capture->cells * sizeof(uint8_t));
    capture->page_meta = calloc(pages, sizeof(*capture->page_meta));
    capture->ant_meta = calloc(pages * capture->ant_slots, sizeof(*capture->ant_meta));
    if (!capture->world_pages || !capture->page_meta || !capture->ant_meta) {
        dump_capture_destroy(capture);
        return -1;
    }
    return 0;
}

void dump_capture_destroy(DumpCapture *capture)
{
    if (!capture) return;
    free(capture->world_pages);
    free(capture->page_meta);
    free(capture->ant_meta);
    memset(capture, 0, sizeof(*capture));
}

void dump_capture_reset(DumpCapture *capture, const World *world, uint32_t seed, double now)
{
    if (!capture || !world) return;
    capture->page_count = 0;
    capture->next_page = 0;
    capture->next_capture_at = now;
    capture->seed = seed;
    capture->world_width = world->width;
    capture->world_height = world->height;
}

static uint64_t colony_instruction_total(const AntColony *colony)
{
    uint64_t total = 0;
    for (size_t i = 0; i < TURMITE_MAX_ANTS; ++i) {
        total += ant_instruction_count(colony, i);
    }
    return total;
}

static void capture_one(DumpCapture *capture, const World *world, const AntColony *colony,
                        const Scheduler *scheduler, const Lfsr32 *rng, double age, double now)
{
    const size_t slot = capture->next_page;
    uint8_t *dst = capture->world_pages + slot * capture->cells;
    for (size_t i = 0; i < capture->cells; ++i) {
        dst[i] = world_load(world, i);
    }

    const size_t previous_slot = (capture->page_count == 0)
        ? SIZE_MAX
        : ((slot + capture->page_capacity - 1) % capture->page_capacity);
    const uint8_t *previous = (capture->page_count == 0)
        ? NULL
        : capture->world_pages + previous_slot * capture->cells;

    DumpPageMeta *meta = &capture->page_meta[slot];
    meta->age = age;
    meta->collisions = atomic_load_explicit(&colony->collisions, memory_order_relaxed);
    meta->dispatches = scheduler_get_dispatches(scheduler);
    meta->total_instructions = colony_instruction_total(colony);
    meta->world_hash = fnv1a64(dst, capture->cells);
    meta->changed_cells = count_changed(dst, previous, capture->cells);
    meta->rng_state = rng ? atomic_load_explicit(&rng->state, memory_order_relaxed) : 0;
    meta->active_population = (uint32_t)atomic_load_explicit(&colony->active_population, memory_order_relaxed);
    meta->quantum = (uint32_t)scheduler_get_quantum((Scheduler *)scheduler);
    meta->min_service = (uint32_t)scheduler_get_min_service((Scheduler *)scheduler);

    DumpAntMeta *ants = capture->ant_meta + slot * capture->ant_slots;
    pthread_mutex_lock((pthread_mutex_t *)&scheduler->lock);
    for (size_t i = 0; i < capture->ant_slots; ++i) {
        const Ant *ant = &colony->ants[i];
        DumpAntMeta *out = &ants[i];
        const uint32_t flags = atomic_load_explicit(&ant->flags, memory_order_relaxed);
        out->id = (uint32_t)i;
        out->enabled = (flags & ANT_F_ENABLED) != 0;
        out->clobbered = (flags & ANT_F_CLOBBERED) != 0;
        out->expired = (flags & ANT_F_EXPIRED) != 0;
        out->draining = (flags & ANT_F_DRAINING) != 0;
        out->x = ant_position_x(colony, i);
        out->y = ant_position_y(colony, i);
        out->heading = atomic_load_explicit(&ant->heading, memory_order_relaxed);
        out->state = atomic_load_explicit(&ant->state, memory_order_relaxed);
        out->rule_index = ant_rule_index(ant);
        out->tokens = ant_tokens(ant);
        out->token_rate = ant_token_rate(ant);
        out->token_capacity = ant_token_capacity(ant);
        out->weight = atomic_load_explicit(&ant->weight, memory_order_relaxed);
        out->fair_credit = scheduler->fair_credit[i];
        out->instructions = ant_instruction_count(colony, i);
        out->mutations = ant_mutation_count(colony, i);
    }
    pthread_mutex_unlock((pthread_mutex_t *)&scheduler->lock);

    capture->next_page = (slot + 1) % capture->page_capacity;
    if (capture->page_count < capture->page_capacity) ++capture->page_count;
    capture->next_capture_at = now + capture->interval_seconds;
}

void dump_capture_maybe(DumpCapture *capture, const World *world, const AntColony *colony,
                        const Scheduler *scheduler, const Lfsr32 *rng, double age, double now)
{
    if (!capture || now < capture->next_capture_at) return;
    /* Catch up without trying to manufacture historical pages. One capture per observed loop. */
    capture_one(capture, world, colony, scheduler, rng, age, now);
}

void dump_capture_now(DumpCapture *capture, const World *world, const AntColony *colony,
                      const Scheduler *scheduler, const Lfsr32 *rng, double age, double now)
{
    if (!capture) return;
    capture_one(capture, world, colony, scheduler, rng, age, now);
}

static int make_dir_if_missing(const char *path)
{
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    fprintf(stderr, "dump: mkdir %s failed: %s\n", path, strerror(errno));
    return -1;
}

static void write_heading(FILE *fp, const char *key, const char *value)
{
    fprintf(fp, "%s=%s\n", key, value);
}

static void make_timestamp(char *buf, size_t size)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, size, "%Y%m%d-%H%M%S", &tm);
}

static size_t chronological_slot(const DumpCapture *capture, size_t ordinal)
{
    /* Oldest first. */
    size_t first = (capture->page_count == capture->page_capacity) ? capture->next_page : 0;
    return (first + ordinal) % capture->page_capacity;
}

int dump_write(const DumpCapture *capture, const World *world, const AntColony *colony,
              const Scheduler *scheduler, size_t workers, size_t quantum,
              double lifetime_minutes, const char *output_root)
{
    if (!capture || !world || !colony || !scheduler || capture->page_count == 0) {
        fprintf(stderr, "dump: no pages captured\n");
        return -1;
    }

    const char *root = output_root && *output_root ? output_root : dump_default_output_root();
    if (make_dir_if_missing(root) != 0) return -1;

    char stamp[32];
    make_timestamp(stamp, sizeof(stamp));
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/tape-%08" PRIX32 "-%s", root, capture->seed, stamp);
    if (make_dir_if_missing(dir) != 0) return -1;

    char pages_dir[560];
    snprintf(pages_dir, sizeof(pages_dir), "%s/pages", dir);
    if (make_dir_if_missing(pages_dir) != 0) return -1;

    char path[640];
    snprintf(path, sizeof(path), "%s/manifest.txt", dir);
    FILE *manifest = fopen(path, "w");
    if (!manifest) {
        fprintf(stderr, "dump: cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    fprintf(manifest, "TURMITE UNIVERSE DEBUG DUMP\n");
    write_heading(manifest, "format", "v1");
    fprintf(manifest, "seed=0x%08" PRIX32 "\n", capture->seed);
    fprintf(manifest, "world_width=%d\n", capture->world_width);
    fprintf(manifest, "world_height=%d\n", capture->world_height);
    fprintf(manifest, "world_cells=%zu\n", capture->cells);
    fprintf(manifest, "colors=%d\n", TURMITE_COLORS);
    fprintf(manifest, "cell_format=uint8 color index, row-major, top-to-bottom\n");
    fprintf(manifest, "workers=%zu\n", workers);
    fprintf(manifest, "scheduler=WFQ\n");
    fprintf(manifest, "quantum=%zu\n", quantum);
    fprintf(manifest, "min_service=%zu\n", scheduler_get_min_service(scheduler));
    fprintf(manifest, "lifetime_minutes=%.6f\n", lifetime_minutes);
    fprintf(manifest, "capture_interval_seconds=%.6f\n", capture->interval_seconds);
    fprintf(manifest, "requested_pages=%zu\n", capture->page_capacity);
    fprintf(manifest, "captured_pages=%zu\n", capture->page_count);
    fprintf(manifest, "\nPAGE TABLE\n");
    fprintf(manifest, "page\tage_seconds\tworld_hash_hex\tchanged_cells\tcollisions\tdispatches\tinstructions\trng_state_hex\tactive_population\tquantum\tmin_service\n");

    for (size_t ordinal = 0; ordinal < capture->page_count; ++ordinal) {
        size_t slot = chronological_slot(capture, ordinal);
        const DumpPageMeta *meta = &capture->page_meta[slot];
        fprintf(manifest, "%04zu\t%.6f\t%016" PRIX64 "\t%" PRIu32 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%08" PRIX32 "\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\n",
                ordinal, meta->age, meta->world_hash, meta->changed_cells,
                meta->collisions, meta->dispatches, meta->total_instructions,
                meta->rng_state, meta->active_population, meta->quantum, meta->min_service);
    }

    fprintf(manifest, "\nANT SNAPSHOTS\n");
    fprintf(manifest, "page\tant\tenabled\tclobbered\texpired\tdraining\tx\ty\theading\tstate\trule_index\trule_id\trule_name\ttokens\ttoken_rate\ttoken_capacity\tweight\tfair_credit\tinstructions\tmutations\n");
    for (size_t ordinal = 0; ordinal < capture->page_count; ++ordinal) {
        size_t slot = chronological_slot(capture, ordinal);
        const DumpAntMeta *ants = capture->ant_meta + slot * capture->ant_slots;
        for (size_t i = 0; i < capture->ant_slots; ++i) {
            const DumpAntMeta *a = &ants[i];
            const TurmiteRule *rule = rules_get(a->rule_index);
            fprintf(manifest, "%04zu\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%u\t%u\t%u\t%s\t%s\t%.9g\t%.9g\t%.9g\t%u\t%.9g\t%" PRIu64 "\t%" PRIu64 "\n",
                    ordinal, a->id, a->enabled, a->clobbered, a->expired, a->draining,
                    a->x, a->y, a->heading, a->state, a->rule_index,
                    rule ? rule->id : "?", rule ? rule->name : "?",
                    a->tokens, a->token_rate, a->token_capacity, a->weight, a->fair_credit,
                    a->instructions, a->mutations);
        }
    }

    fclose(manifest);

    for (size_t ordinal = 0; ordinal < capture->page_count; ++ordinal) {
        size_t slot = chronological_slot(capture, ordinal);
        snprintf(path, sizeof(path), "%s/page-%04zu.bin", pages_dir, ordinal);
        FILE *page = fopen(path, "wb");
        if (!page) {
            fprintf(stderr, "dump: cannot create %s: %s\n", path, strerror(errno));
            return -1;
        }
        const uint8_t *src = capture->world_pages + slot * capture->cells;
        size_t written = fwrite(src, 1, capture->cells, page);
        fclose(page);
        if (written != capture->cells) {
            fprintf(stderr, "dump: short write for %s\n", path);
            return -1;
        }
    }

    char readme_path[640];
    snprintf(readme_path, sizeof(readme_path), "%s/README.txt", dir);
    FILE *readme = fopen(readme_path, "w");
    if (readme) {
        fprintf(readme,
                "This directory is a headless debug capture of Turmite Universe.\n\n"
                "pages/page-NNNN.bin is exactly %zu bytes: one uint8 color index per world cell,\n"
                "row-major from top-left to bottom-right. Use manifest.txt for dimensions and metadata.\n"
                "The page table is chronological; page 0000 is the oldest captured page in the ring.\n"
                "world_hash_hex is FNV-1a over the raw page bytes; changed_cells compares each page\n"
                "with the immediately preceding captured page.\n",
                capture->cells);
        fclose(readme);
    }

    printf("debug dump written: %s (%zu pages)\n", dir, capture->page_count);
    return 0;
}
