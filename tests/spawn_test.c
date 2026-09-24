#include "scheduler.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill(unsigned width, unsigned height, unsigned count)
{
    World w; AntColony c; Scheduler s; Lfsr32 rng;
    assert(world_init(&w, (int)width, (int)height)==0 && ant_colony_init(&c,&w)==0);
    assert(scheduler_init(&s,&c,SCHED_WFQ,256)==0); rng_seed(&rng,123);
    for (unsigned i=0;i<count;++i) {
        assert(scheduler_spawn_ant(&s,&w,&rng,1000+i)==1);
        assert(scheduler_active_population(&s)==i+1);
        Ant *a=&c.ants[i];
        assert(a->rule==rules_get(ant_rule_index(a)));
        assert(atomic_load(&a->tokens_fp)==atomic_load(&a->token_capacity_fp));
        assert(atomic_load(&a->token_rate)>=50000 && atomic_load(&a->token_rate)<=2000000);
        assert(atomic_load(&a->weight)>=1 && atomic_load(&a->weight)<=16);
        assert(atomic_load(&a->state)<a->rule->states && atomic_load(&a->heading)<4);
        assert(atomic_load(&a->color_offset)<TURMITE_COLORS);
        assert(ant_instruction_count(&c,i)==0 && ant_mutation_count(&c,i)==0);
        for (unsigned j=0;j<i;++j) assert(ant_packed_position(&c,j)!=ant_packed_position(&c,i));
        /* A leased, mutated predecessor must neither block nor seed the spawn. */
        ant_mutate_in_place(a,&c);
        atomic_store(&a->token_rate,1); atomic_store(&a->weight,123);
        atomic_fetch_or(&a->flags,ANT_F_LEASED);
    }
    assert(scheduler_spawn_ant(&s,&w,&rng,2000)==0);
    assert(scheduler_active_population(&s)==count);
    if (count<TURMITE_MAX_ANTS) assert(!(atomic_load(&c.ants[count].flags)&ANT_F_ENABLED));
    /* Reuse a retired slot, resetting its statistics and scheduling state. */
    ant_release_occupancy(&c.ants[0],&c);
    atomic_store(&c.ants[0].flags,ANT_F_EXPIRED);
    atomic_fetch_and(&c.enabled_mask,~UINT32_C(1)); atomic_fetch_sub(&c.active_population,1);
    atomic_store(&c.stats[0].instructions,999);atomic_store(&c.stats[0].mutations,999);
    s.fair_credit[0]=999;s.recovery_at_us[0]=999;
    assert(scheduler_spawn_ant(&s,&w,&rng,3000)==1);
    assert(ant_instruction_count(&c,0)==0 && ant_mutation_count(&c,0)==0);
    assert(s.fair_credit[0]==0 && s.recovery_at_us[0]==0 && s.last_token_us[0]==3000);
    scheduler_begin_drain(&s);assert(scheduler_spawn_ant(&s,&w,&rng,4000)==0);
    scheduler_stop(&s);assert(scheduler_spawn_ant(&s,&w,&rng,4000)==0);
    scheduler_destroy(&s);ant_colony_destroy(&c);world_destroy(&w);
}
int main(void)
{
    fill(64,64,32);fill(1,1,1);fill(17,1,17);
    puts("spawn ok: one library ant, independent phenotype, active leases, slot/world capacity, recycled metadata, drain/stop");
}
