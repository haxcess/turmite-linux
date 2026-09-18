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

/* One Galois LFSR step (taps 0x80200003), shared by every stream in the
 * codebase: the atomic Lfsr32 below wraps it in a CAS loop, and any private
 * non-atomic uint32_t stream (per-ant RNG, rule mutation) calls it directly.
 * Keeping the tap polynomial in exactly one place means it can only need
 * fixing in one place. Zero is the LFSR's absorbing state, so it is remapped
 * to 1 on the way in rather than requiring every caller to pre-seed away
 * from zero. */
static inline uint32_t lfsr32_advance(uint32_t state)
{
    if (state == 0) state = 1u;
    const uint32_t lsb = state & 1u;
    state >>= 1;
    if (lsb) state ^= 0x80200003u;
    return state;
}

void rng_seed(Lfsr32 *rng, uint32_t seed);
uint32_t rng_next(Lfsr32 *rng);
uint32_t rng_uniform(Lfsr32 *rng, uint32_t upper_exclusive);
float rng_unit(Lfsr32 *rng);
uint32_t rng_entropy_seed(void);

#endif
