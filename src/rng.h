#ifndef TURMITE_RNG_H
#define TURMITE_RNG_H

#include <stdint.h>
#include <stdatomic.h>

/* Small deterministic PRNG used for universe setup and reproducibility. The
 * atomic state lets setup/control paths share one generator safely; ants use
 * their own non-atomic LFSR state while executing. */
typedef struct {
    _Atomic uint32_t state;
} Lfsr32;

void rng_seed(Lfsr32 *rng, uint32_t seed);
uint32_t rng_next(Lfsr32 *rng);
uint32_t rng_uniform(Lfsr32 *rng, uint32_t upper_exclusive);
float rng_unit(Lfsr32 *rng);
uint32_t rng_entropy_seed(void);

#endif
