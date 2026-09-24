/* Fixed work: compare interpreter changes without wall-clock token supply. */
#include "ant.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double seconds(void)
{
    struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv)
{
    unsigned rounds = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 10000;
    size_t count = argc > 2 ? (size_t)strtoul(argv[2], NULL, 0) : rules_count();
    if (!rounds || !count || count > rules_count()) return 2;
    World w; AntColony c; Lfsr32 rng;
    if (world_init(&w, 300, 200) || ant_colony_init(&c, &w)) return 1;
    uint64_t total = 0, hash = UINT64_C(14695981039346656037);
    double start = seconds();
    for (size_t r = 0; r < count; ++r) {
        ant_colony_reset(&c); world_clear(&w); rng_seed(&rng, 12345);
        Ant *a = &c.ants[0]; ant_randomize(a, &c, &w, &rng, rules_get(r));
        for (unsigned i = 0; i < rounds; ++i) {
            atomic_store_explicit(&a->tokens_fp, 256 * TOKEN_FP_ONE, memory_order_relaxed);
            total += ant_execute_quantum(a, &c, &w, 256);
        }
        for (size_t i = 0; i < w.cells; ++i) hash = (hash ^ world_load(&w, i)) * UINT64_C(1099511628211);
        hash = (hash ^ ant_packed_position(&c, 0)) * UINT64_C(1099511628211);
        hash = (hash ^ atomic_load(&a->heading)) * UINT64_C(1099511628211);
        hash = (hash ^ atomic_load(&a->state)) * UINT64_C(1099511628211);
    }
    double elapsed = seconds() - start;
    printf("instructions=%llu hash=%016llx cpu_seconds=%.6f ns_per_instruction=%.3f\n",
           (unsigned long long)total, (unsigned long long)hash, elapsed, elapsed * 1e9 / total);
    ant_colony_destroy(&c); world_destroy(&w);
}
