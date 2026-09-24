/* Deliberately straightforward, independent single-ant reference interpreter. */
#include "ant.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned x, y, heading, state; bool halted; } Reference;
static void step(Reference *a, uint8_t *tape, unsigned width, unsigned height,
                 const TurmiteRule *r, unsigned offset)
{
    unsigned cell = a->y * width + a->x, color = tape[cell];
    unsigned local = (color + TURMITE_COLORS - offset) % TURMITE_COLORS;
    RuleAction action = {(uint8_t)color, TURN_F, (uint8_t)a->state, false};
    if (a->state < r->states && r->colors) {
        if (local >= r->colors) local = r->colors - 1;
        action = r->table[a->state][local];
        tape[cell] = (action.write_color + offset) % TURMITE_COLORS;
    }
    a->state = action.next_state;
    switch (action.turn) {
        case TURN_R: a->heading = (a->heading + 1) % 4; break;
        case TURN_L: a->heading = (a->heading + 3) % 4; break;
        case TURN_B: a->heading = (a->heading + 2) % 4; break;
        case TURN_N: a->heading = 0; break;
        case TURN_E: a->heading = 1; break;
        case TURN_S: a->heading = 2; break;
        case TURN_W: a->heading = 3; break;
        default: break;
    }
    if (action.halt) { a->halted = true; return; }
    if (action.turn == TURN_H) return;
    switch (a->heading) {
        case 0: a->y = (a->y + height - 1) % height; break;
        case 1: a->x = (a->x + 1) % width; break;
        case 2: a->y = (a->y + 1) % height; break;
        case 3: a->x = (a->x + width - 1) % width; break;
    }
}
static void compare(const TurmiteRule *rule, unsigned width, unsigned height,
                    unsigned offset, unsigned heading, unsigned state)
{
    World w; AntColony c; Lfsr32 rng;
    assert(world_init(&w, (int)width, (int)height) == 0);
    assert(ant_colony_init(&c, &w) == 0); rng_seed(&rng, 123);
    Ant *a = &c.ants[0]; ant_randomize(a, &c, &w, &rng, rule);
    ant_release_occupancy(a, &c);
    atomic_store(&c.positions[0], 0); atomic_store(&c.occupancy[0], 1);
    atomic_store(&a->heading, heading); atomic_store(&a->state, state);
    atomic_store(&a->color_offset, offset);
    uint8_t *tape = calloc(w.cells, 1); assert(tape);
    for (size_t i = 0; i < w.cells; ++i) { tape[i] = i % TURMITE_COLORS; world_store(&w, i, tape[i]); }
    Reference r = {0, 0, heading, state, false};
    for (unsigned i = 0; i < 128; ++i) {
        /* Test both instruction boundaries and multi-instruction publication. */
        unsigned grant = 1 + i % 17, executed = 0;
        while (executed < grant && !r.halted) { step(&r, tape, width, height, rule, offset); ++executed; }
        atomic_store(&a->tokens_fp, grant * TOKEN_FP_ONE + 17);
        assert(ant_execute_quantum(a, &c, &w, grant) == executed);
        assert(atomic_load(&a->tokens_fp) == (grant - executed) * TOKEN_FP_ONE + 17);
        assert(ant_packed_position(&c, 0) == (r.x | (r.y << 16)));
        assert(atomic_load(&a->heading) == r.heading && atomic_load(&a->state) == r.state);
        assert(!!(atomic_load(&a->flags) & ANT_F_HALTED) == r.halted);
        for (size_t j = 0; j < w.cells; ++j) {
            assert(world_load(&w, j) == tape[j]);
            assert(atomic_load(&c.occupancy[j]) == (j == r.y * width + r.x));
        }
        if (r.halted) break;
    }
    free(tape); ant_colony_destroy(&c); world_destroy(&w);
}
int main(void)
{
    for (size_t i = 0; i < rules_count(); ++i)
        for (unsigned o = 0; o < TURMITE_COLORS; ++o)
            for (unsigned h = 0; h < 4; ++h)
                compare(rules_get(i), 13, 11, o, h, 0);
    for (unsigned turn = TURN_F; turn <= TURN_W; ++turn)
        for (unsigned halt = 0; halt < 2; ++halt)
            for (unsigned h = 0; h < 4; ++h) {
                TurmiteRule r = {.states=1, .colors=1, .table={{{0, (uint8_t)turn, 0, halt}}}};
                compare(&r, 1, 1, 0, h, 0);
                compare(&r, 1, 7, 5, h, 0);
                compare(&r, 7, 1, 3, h, 0);
            }
    uint32_t rng = 1234;
    for (unsigned i = 0; i < 500; ++i) {
        TurmiteRule r; rules_generate(&r, &rng);
        compare(&r, 7, 5, i % 6, i % 4, i % r.states);
        rules_mutate(&r, &rng);
        compare(&r, 7, 5, i % 6, i % 4, i % r.states);
    }
    TurmiteRule invalid = {.states=1, .colors=0};
    compare(&invalid, 7, 5, 0, 1, 0);
    invalid.colors = 1;
    compare(&invalid, 7, 5, 0, 1, 4);
    puts("execution reference ok: catalogue, generated/mutated rules, colors, turns, HALT, tiny worlds, fallback, token accounting");
}
