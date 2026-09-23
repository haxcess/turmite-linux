#include "ant.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned bits(unsigned n) { unsigned count = 0; for (; n; n &= n-1) ++count; return count; }
static unsigned changed_bits(const TurmiteRule *a, const TurmiteRule *b)
{
    assert(a->states == b->states && a->colors == b->colors);
    unsigned changed = 0;
    for (unsigned s = 0; s < a->states; ++s) for (unsigned c = 0; c < a->colors; ++c) {
        const RuleAction *x = &a->table[s][c], *y = &b->table[s][c];
        assert(y->write_color < b->colors && y->next_state < b->states && y->turn <= TURN_W);
        changed += bits(x->write_color ^ y->write_color) + bits((unsigned)x->turn ^ (unsigned)y->turn);
        changed += bits(x->next_state ^ y->next_state) + (x->halt != y->halt);
    }
    return changed;
}

static void crossing(unsigned axes, WallMode walls, unsigned heading, int width, int height)
{
    World w; AntColony colony; Lfsr32 rng;
    assert(world_init(&w, width, height) == 0);
    assert(ant_colony_init(&colony, &w) == 0);
    w.boundary_axes = axes; w.walls = walls;
    TurmiteRule rule = {.states = 1, .colors = 1, .table = {{{0, TURN_F, 0, false}}}};
    rng_seed(&rng, 123);
    Ant *ant = &colony.ants[0];
    ant_randomize(ant, &colony, &w, &rng, &rule);
    atomic_store(&ant->color_offset, 0);
    ant_release_occupancy(ant, &colony);
    unsigned x = heading == 1 ? (unsigned)width-1 : 0;
    unsigned y = heading == 2 ? (unsigned)height-1 : 0;
    atomic_store(&colony.positions[0], x | (y << 16));
    atomic_store(&colony.occupancy[y * width + x], 1);
    atomic_store(&ant->heading, heading);
    atomic_store(&ant->tokens_fp, TOKEN_FP_ONE);
    assert(ant_execute_quantum(ant, &colony, &w, 1) == 1);
    bool bounded = axes & ((heading & 1) ? WORLD_BOUND_X : WORLD_BOUND_Y);
    bool bounce = bounded && walls == WALL_BOUNCY;
    unsigned ex = x, ey = y;
    if (heading == 0) ey = bounce ? (height > 1 ? 1u : 0u) : (unsigned)height-1;
    if (heading == 1) ex = bounce ? (width > 1 ? (unsigned)width-2 : 0u) : 0;
    if (heading == 2) ey = bounce ? (height > 1 ? (unsigned)height-2 : 0u) : 0;
    if (heading == 3) ex = bounce ? (width > 1 ? 1u : 0u) : (unsigned)width-1;
    assert(ant_position_x(&colony, 0) == ex && ant_position_y(&colony, 0) == ey);
    assert(atomic_load(&ant->heading) == (bounce ? (heading + 2) % 4 : heading));
    assert(ant_occupant_at(&colony, ex, ey) == 1);
    bool radioactive = bounded && walls == WALL_RADIOACTIVE;
    assert(changed_bits(&rule, ant->rule) == (unsigned)radioactive);
    assert(ant_mutation_count(&colony, 0) == (unsigned)radioactive);
    if (radioactive) assert(ant->rule == &colony.runtime_rules[0]);
    uint8_t pixels[20] = {0};
    world_overlay_boundaries(&w, pixels);
    for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col) {
        bool line = walls != WALL_TRANSPARENT &&
            (((axes & WORLD_BOUND_X) && (col == 0 || col == width-1)) ||
             ((axes & WORLD_BOUND_Y) && (row == 0 || row == height-1)));
        assert(pixels[row*width+col] == (line ? (walls == WALL_BOUNCY ? 1 : 3) : 0));
        assert(world_load(&w, (size_t)row*width+col) == 0);
    }
    ant_colony_destroy(&colony); world_destroy(&w);
}

static void instruction_boundaries(void)
{
    World w; AntColony colony; Lfsr32 rng;
    assert(world_init(&w, 5, 4) == 0);
    assert(ant_colony_init(&colony, &w) == 0);
    world_set_boundaries(&w, WORLD_BOX, WALL_RADIOACTIVE, 1);
    TurmiteRule rule = {.states = 1, .colors = 1, .table = {{{0, TURN_F, 0, false}}}};
    for (unsigned kind = 0; kind < 3; ++kind) {
        ant_colony_reset(&colony);
        rng_seed(&rng, 123);
        Ant *ant = &colony.ants[0];
        rule.table[0][0].turn = kind == 0 ? TURN_H : TURN_F;
        rule.table[0][0].halt = kind == 1;
        ant_randomize(ant, &colony, &w, &rng, &rule);
        atomic_store(&ant->color_offset, 0);
        ant_release_occupancy(ant, &colony);
        atomic_store(&colony.positions[0], 0);
        atomic_store(&colony.occupancy[0], 1);
        atomic_store(&ant->heading, 3);
        atomic_store(&ant->tokens_fp, 2 * TOKEN_FP_ONE);
        if (kind < 2) {
            assert(ant_execute_quantum(ant, &colony, &w, 1) == 1);
            assert(ant_packed_position(&colony, 0) == 0);
            assert(ant_mutation_count(&colony, 0) == 0);
        } else {
            /* Select a radiation draw that toggles HALT. The instruction
             * already underway wraps; the next one sees the mutated table. */
            uint32_t seed;
            for (seed = 1; seed < 1000; ++seed) {
                uint32_t state = seed; TurmiteRule candidate = rule;
                rules_flip_bit(&candidate, &state);
                if (candidate.table[0][0].halt) break;
            }
            assert(seed < 1000);
            ant->rng_state = seed;
            assert(ant_execute_quantum(ant, &colony, &w, 2) == 2);
            assert(ant_position_x(&colony, 0) == 4);
            assert(atomic_load(&ant->flags) & ANT_F_HALTED);
            assert(ant_mutation_count(&colony, 0) == 1);
            assert(!rule.table[0][0].halt);
        }
    }
    ant_colony_destroy(&colony); world_destroy(&w);
}

int main(void)
{
    instruction_boundaries();
    for (unsigned axes = 0; axes < 4; ++axes)
        for (WallMode walls = WALL_TRANSPARENT; walls <= WALL_RADIOACTIVE; ++walls)
            for (unsigned h = 0; h < 4; ++h) {
                crossing(axes, walls, h, 5, 4);
                crossing(axes, walls, h, 1, 1);
            }
    World w = {0}; unsigned seen = 0;
    for (uint32_t seed = 1; seed < 100; ++seed) {
        world_set_boundaries(&w, WORLD_TUBE, WALL_BOUNCY, seed);
        unsigned axes = w.boundary_axes;
        assert(axes == WORLD_BOUND_X || axes == WORLD_BOUND_Y); seen |= axes;
        world_set_boundaries(&w, WORLD_TUBE, WALL_BOUNCY, seed);
        assert(w.boundary_axes == axes);
    }
    assert(seen == 3);
    world_set_boundaries(&w, WORLD_TOROID, WALL_RADIOACTIVE, 1); assert(w.boundary_axes == 0);
    world_set_boundaries(&w, WORLD_BOX, WALL_BOUNCY, 1); assert(w.boundary_axes == 3);
    uint32_t rng = 123456;
    for (unsigned i = 0; i < 4096; ++i) {
        TurmiteRule rule; rules_generate(&rule, &rng);
        TurmiteRule before = rule;
        rules_flip_bit(&rule, &rng);
        assert(changed_bits(&before, &rule) == 1);
    }
    puts("boundaries ok: all edges/modes, occupancy, tiny worlds, overlays, seeded tube, single-bit mutations");
}
