/* GRAVEYARD — retired code, kept verbatim for reference and possible
 * reinstatement. Not part of the build: not in the Makefile's SRC list, and
 * not compiled by any test target. If this gets a real caller again, move
 * its prototype back into world.h (it sat right after world_store()) and its
 * body back into world.c (it sat right after world_store()'s definition),
 * then delete it from here.
 *
 * Retired: 2026-09, src/ consistency audit. Zero callers anywhere in src/ or
 * tests/. Its own doc comment explained why: ant.c's ant_execute_quantum()
 * deliberately reimplements toroidal wrap with cheap +-1 branches instead of
 * calling this general modulo-based version, since ant movement is always a
 * single cell per step. That divergence is intentional and still stands —
 * this function just never ended up with any other caller either.
 */

#include "../world.h"

/* Coordinates wrap toroidally; the executor uses equivalent branch wrapping. */
size_t world_index(const World *world, int x, int y)
{
    int xx = x % world->width;
    int yy = y % world->height;
    if (xx < 0) xx += world->width;
    if (yy < 0) yy += world->height;
    return (size_t)yy * (size_t)world->width + (size_t)xx;
}
