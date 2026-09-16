#include "rng.h"

#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

void rng_seed(Lfsr32 *rng, uint32_t seed)
{
    if (seed == 0) seed = 0x1u;
    atomic_store_explicit(&rng->state, seed, memory_order_relaxed);
}

uint32_t rng_next(Lfsr32 *rng)
{
    uint32_t old = atomic_load_explicit(&rng->state, memory_order_relaxed);
    for (;;) {
        uint32_t lsb = old & 1u;
        uint32_t next = old >> 1;
        if (lsb) next ^= 0x80200003u;
        if (atomic_compare_exchange_weak_explicit(
                &rng->state, &old, next,
                memory_order_relaxed, memory_order_relaxed)) {
            return next;
        }
    }
}

uint32_t rng_uniform(Lfsr32 *rng, uint32_t upper_exclusive)
{
    if (upper_exclusive == 0) return 0;
    return rng_next(rng) % upper_exclusive;
}

float rng_unit(Lfsr32 *rng)
{
    return (float)(rng_next(rng) / 4294967295.0);
}

uint32_t rng_entropy_seed(void)
{
    uint32_t seed = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t n = read(fd, &seed, sizeof(seed));
        close(fd);
        if (n == (ssize_t)sizeof(seed) && seed != 0) return seed;
    }

    uint64_t t = (uint64_t)time(NULL);
    uint64_t c = (uint64_t)clock();
    pid_t p = getpid();
    seed = (uint32_t)(t ^ (t >> 32) ^ c ^ ((uint64_t)p << 16));
    return seed ? seed : 0x1u;
}
