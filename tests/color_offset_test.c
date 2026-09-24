#include "ant.h"
#include <assert.h>
#include <stdio.h>

static void place(AntColony *c, unsigned x, unsigned y)
{
    ant_release_occupancy(&c->ants[0], c);
    atomic_store(&c->positions[0], x | (y << 16));
    atomic_store(&c->occupancy[y*c->occupancy_width+x], 1);
}

static void mappings(void)
{
    World w; AntColony c; Lfsr32 rng;
    assert(world_init(&w, 9, 7) == 0 && ant_colony_init(&c, &w) == 0);
    for (unsigned n = 1; n <= TURMITE_COLORS; ++n) {
        TurmiteRule rule = {.states = 2, .colors = (uint8_t)n};
        for (unsigned color = 0; color < n; ++color)
            rule.table[0][color] = (RuleAction){(color+1)%n, TURN_R, 1, false};
        ant_colony_reset(&c); rng_seed(&rng, 123);
        Ant *ant = &c.ants[0]; ant_randomize(ant, &c, &w, &rng, &rule);
        for (unsigned offset = 0; offset < TURMITE_COLORS; ++offset) {
            atomic_store(&ant->color_offset, offset);
            for (unsigned color = 0; color < TURMITE_COLORS; ++color) {
                place(&c, 4, 3);
                atomic_store(&ant->state, 0); atomic_store(&ant->heading, 0);
                atomic_store(&ant->tokens_fp, TOKEN_FP_ONE);
                world_store(&w, 31, color);
                assert(ant_execute_quantum(ant, &c, &w, 1) == 1);
                unsigned local = (color + TURMITE_COLORS - offset) % TURMITE_COLORS;
                if (local >= n) local = n - 1;
                assert(world_load(&w, 31) == (offset+(local+1)%n)%TURMITE_COLORS);
                assert(atomic_load(&ant->state) == 1);
                assert(atomic_load(&ant->heading) == 1);
            }
        }
    }
    ant_colony_destroy(&c); world_destroy(&w);
}

static void langton_translation(void)
{
    const TurmiteRule rule = {.states=1, .colors=2,
        .table={{{1, TURN_R, 0, false}, {0, TURN_L, 0, false}}}};
    for (unsigned offset=0; offset<TURMITE_COLORS; ++offset) {
        World worlds[2]; AntColony colonies[2]; Lfsr32 rng;
        for (unsigned i=0; i<2; ++i) {
            assert(world_init(&worlds[i], 31, 29)==0);
            assert(ant_colony_init(&colonies[i], &worlds[i])==0);
            rng_seed(&rng, 123);
            ant_randomize(&colonies[i].ants[0], &colonies[i], &worlds[i], &rng, &rule);
            atomic_store(&colonies[i].ants[0].color_offset, i ? offset : 0);
            if (i) for (size_t cell=0; cell<worlds[i].cells; ++cell) world_store(&worlds[i],cell,offset);
        }
        for (unsigned step=0; step<1000; ++step) {
            for (unsigned i=0; i<2; ++i) {
                atomic_store(&colonies[i].ants[0].tokens_fp, TOKEN_FP_ONE);
                assert(ant_execute_quantum(&colonies[i].ants[0], &colonies[i], &worlds[i], 1)==1);
            }
            assert(ant_packed_position(&colonies[0],0)==ant_packed_position(&colonies[1],0));
            assert(atomic_load(&colonies[0].ants[0].heading)==atomic_load(&colonies[1].ants[0].heading));
        }
        for (size_t cell=0; cell<worlds[0].cells; ++cell)
            assert(world_load(&worlds[1],cell)==(world_load(&worlds[0],cell)+offset)%TURMITE_COLORS);
        for (unsigned i=0; i<2; ++i) { ant_colony_destroy(&colonies[i]); world_destroy(&worlds[i]); }
    }
}

static void lifecycle(void)
{
    World w; AntColony c; Lfsr32 rng;
    assert(world_init(&w,20,20)==0 && ant_colony_init(&c,&w)==0);
    unsigned seen=0, reborn=0;
    for (unsigned seed=1; seed<128; ++seed) {
        ant_colony_reset(&c); rng_seed(&rng,seed);
        Ant *a=&c.ants[0]; ant_randomize(a,&c,&w,&rng,rules_get(0));
        unsigned offset=atomic_load(&a->color_offset);
        assert(offset<TURMITE_COLORS); seen |= 1u<<offset;
        assert(ant_spawn_random(&c.ants[1],&c,&w,&rng));
        assert(atomic_load(&c.ants[1].color_offset)<TURMITE_COLORS);
        ant_mutate_in_place(a,&c);
        assert(atomic_load(&a->color_offset)==offset);
        ant_rebirth_random(a,&c);
        assert(atomic_load(&a->color_offset)<TURMITE_COLORS);
        reborn |= 1u<<atomic_load(&a->color_offset);
        ant_colony_reset(&c); rng_seed(&rng,seed);
        ant_randomize(a,&c,&w,&rng,rules_get(0));
        assert(atomic_load(&a->color_offset)==offset);
    }
    assert(seen==63 && reborn==63);
    ant_colony_destroy(&c); world_destroy(&w);
}

int main(void)
{
    mappings(); langton_translation(); lifecycle();
    puts("color offsets ok: all sizes/offsets/colors, fallback, translated Langton traces, spawn/mutate/rebirth");
}
