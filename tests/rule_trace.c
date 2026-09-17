#include "ant.h"
#include "world.h"
#include "rules.h"
#include <assert.h>
#include <stdio.h>

/* Reference traces from the C interpreter for every catalogue state/heading. */
int main(void)
{
    puts("globalThis.TURMITE_TRACES = [");
    bool first = true;
    for (size_t i=0; i<rules_count(); ++i) {
        const TurmiteRule *rule=rules_get(i);
        assert(rule->states >= 1 && rule->states <= TURMITE_MAX_STATES);
        assert(rule->colors >= 1 && rule->colors <= TURMITE_COLORS);
        for (unsigned s=0; s<rule->states; ++s) for(unsigned c=0;c<rule->colors;++c) {
            const RuleAction *a=&rule->table[s][c];
            assert(a->write_color < rule->colors && a->next_state < rule->states);
            assert(a->turn >= TURN_F && a->turn <= TURN_W);
        }
        for (unsigned state=0; state<rule->states; ++state) for(unsigned dir=0;dir<4;++dir) {
            World w; AntColony colony; Lfsr32 rng;
            assert(world_init(&w,64,48)==0);
            assert(ant_colony_init(&colony,&w)==0);
            rng_seed(&rng,123);
            Ant *ant=&colony.ants[0];
            ant_randomize(ant,&colony,&w,&rng,rule);
            ant_release_occupancy(ant,&colony);
            atomic_store(&colony.positions[0],(24u<<16)|32u);
            atomic_store(&colony.occupancy[24*64+32],1);
            atomic_store(&ant->state,state); atomic_store(&ant->heading,dir);
            unsigned executed=0;
            while (executed<10000) {
                atomic_store(&ant->tokens_fp,500u<<TOKEN_FP_SHIFT);
                size_t n=ant_execute_quantum(ant,&colony,&w,500);
                executed+=(unsigned)n;
                if (atomic_load(&ant->flags)&ANT_F_CLOBBERED) break;
                assert(n>0);
            }
            uint32_t hash=2166136261u; unsigned nonzero=0;
            for(size_t j=0;j<w.cells;++j) { uint8_t c=world_load(&w,j); hash=(hash^c)*16777619u; nonzero+=c!=0; }
            uint32_t pos=ant_packed_position(&colony,0);
            printf("%s[%zu,%u,%u,%u,%u,%u,%u,%u,%u,%u]",first?"":",\n",i,state,dir,pos&65535,pos>>16,
                   atomic_load(&ant->state),atomic_load(&ant->heading),hash,nonzero,executed);
            first=false; ant_colony_destroy(&colony); world_destroy(&w);
        }
    }
    puts("\n];");
}
