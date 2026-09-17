/* Use the real recovery routine with an injected clock, without sleeping. */
#include "../src/scheduler.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned changes(const TurmiteRule *a, const TurmiteRule *b)
{
    unsigned n = 0;
    for (unsigned s=0;s<TURMITE_MAX_STATES;++s) for(unsigned c=0;c<TURMITE_COLORS;++c) {
        const RuleAction *x=&a->table[s][c], *y=&b->table[s][c];
        n += x->write_color != y->write_color;
        n += x->turn != y->turn;
        n += x->next_state != y->next_state;
        n += x->halt != y->halt;
    }
    return n;
}

static void valid(const TurmiteRule *r)
{
    assert(r->states>=1 && r->states<=TURMITE_MAX_STATES);
    assert(r->colors>=1 && r->colors<=TURMITE_COLORS);
    for(unsigned s=0;s<r->states;++s) for(unsigned c=0;c<r->colors;++c) {
        assert(r->table[s][c].write_color<r->colors);
        assert(r->table[s][c].next_state<r->states);
        assert(r->table[s][c].turn>=TURN_F && r->table[s][c].turn<=TURN_W);
    }
}

static void generation(void)
{
    uint32_t rng=0x12345678;
    unsigned states[4]={0}, colors[6]={0}, halt_changes=0;
    for(unsigned i=0;i<30000;++i) {
        TurmiteRule r; rules_generate(&r,&rng); valid(&r);
        states[r.states-1]++;colors[r.colors-1]++;
        const TurmiteRule old=r;rules_mutate(&r,&rng);valid(&r);
        assert(r.states==old.states && r.colors==old.colors && changes(&r,&old)==1);
        for(unsigned s=0;s<r.states;++s) for(unsigned c=0;c<r.colors;++c)
            halt_changes+=r.table[s][c].halt!=old.table[s][c].halt;
    }
    for(unsigned i=1;i<4;++i)assert(states[i-1]>states[i] && states[i]>0);
    for(unsigned i=1;i<6;++i)assert(colors[i-1]>colors[i] && colors[i]>0);
    assert(halt_changes>0);
    printf("complexity samples: states=%u,%u,%u,%u colors=%u,%u,%u,%u,%u,%u\n",
           states[0],states[1],states[2],states[3],colors[0],colors[1],colors[2],colors[3],colors[4],colors[5]);
    for(size_t i=0;i<rules_count();++i) {
        const TurmiteRule before=*rules_get(i);
        TurmiteRule variant=before;rules_mutate(&variant,&rng);
        assert(changes(&before,&variant)==1 && changes(&before,rules_get(i))==0);
        assert(rules_index_of(rules_get(i))==i);
        for(size_t j=i+1;j<rules_count();++j)assert(strcmp(rules_get(i)->id,rules_get(j)->id)!=0);
    }
}

static const TurmiteRule walker={"test", "Test",2,2,{
    {{1,TURN_F,1,false},{0,TURN_F,1,false}},
    {{1,TURN_F,0,false},{0,TURN_F,0,false}}
}};
static void place(AntColony *colony, Ant *ant, unsigned x, unsigned y)
{
    ant_release_occupancy(ant,colony);
    size_t i=(size_t)(ant-colony->ants);
    atomic_store(&colony->positions[i],(y<<16)|x);
    atomic_store(&colony->occupancy[y*colony->occupancy_width+x],i+1);
    atomic_store(&ant->heading,1);
    atomic_store(&ant->state,0);
}

static void collisions(void)
{
    World world;AntColony colony;Scheduler scheduler;Lfsr32 rng;
    assert(world_init(&world,16,16)==0);assert(ant_colony_init(&colony,&world)==0);
    rng_seed(&rng,123);
    for(unsigned i=0;i<2;++i) ant_randomize(&colony.ants[i],&colony,&world,&rng,&walker);
    Ant *loser=&colony.ants[0],*winner=&colony.ants[1];
    place(&colony,loser,2,2);place(&colony,winner,3,2);
    atomic_store(&loser->tokens_fp,100*TOKEN_FP_ONE);atomic_store(&winner->tokens_fp,500*TOKEN_FP_ONE);
    atomic_fetch_or(&loser->flags,ANT_F_LEASED | ANT_F_DRAINING);
    atomic_store(&loser->token_rate,0);
    assert(scheduler_init(&scheduler,&colony,SCHED_WFQ,790)==0);
    assert(ant_execute_quantum(loser,&colony,&world,100)==1);
    assert(atomic_load(&loser->flags)&ANT_F_CLOBBERED);
    assert(ant_position_x(&colony,0)==3 && ant_occupant_at(&colony,3,2)==2);
    assert(atomic_load(&colony.collisions)==1);
    assert(ant_execute_quantum(loser,&colony,&world,100)==0);
    recover_ant(&scheduler,loser,1000);assert(scheduler.recovery_at_us[0]==0); /* still leased */
    atomic_fetch_and(&loser->flags,~(uint32_t)ANT_F_LEASED);
    const uint32_t pos=ant_packed_position(&colony,0),tokens=atomic_load(&loser->tokens_fp);
    const unsigned heading=atomic_load(&loser->heading), state=atomic_load(&loser->state);
    const unsigned capacity=atomic_load(&loser->token_capacity_fp),weight=atomic_load(&loser->weight);
    scheduler.fair_credit[0]=12.5;const uint64_t last=scheduler.last_token_us[0];
    TurmiteRule before=*loser->rule;
    recover_ant(&scheduler,loser,1000);
    recover_ant(&scheduler,loser,1000+COLLISION_PAUSE_US-1);
    assert(changes(&before,loser->rule)==0);
    recover_ant(&scheduler,loser,1000+COLLISION_PAUSE_US);
    assert(changes(&before,loser->rule)==1 && ant_mutation_count(&colony,0)==1);
    assert(loser->rule==&colony.runtime_rules[0]);
    assert(atomic_load(&loser->flags)&ANT_F_WAITING);assert(!runnable(loser));
    assert(atomic_load(&loser->flags)&ANT_F_DRAINING);
    assert(ant_packed_position(&colony,0)==pos && atomic_load(&loser->heading)==heading && atomic_load(&loser->state)==state);
    assert(atomic_load(&loser->tokens_fp)==tokens && atomic_load(&loser->token_rate)==0);
    assert(atomic_load(&loser->token_capacity_fp)==capacity && atomic_load(&loser->weight)==weight);
    assert(scheduler.fair_credit[0]==12.5 && scheduler.last_token_us[0]==last);
    before=*loser->rule;
    recover_ant(&scheduler,loser,200000);assert(changes(&before,loser->rule)==0);
    ant_release_occupancy(winner,&colony);
    recover_ant(&scheduler,loser,210000);
    assert(runnable(loser) && ant_occupant_at(&colony,3,2)==1);
    assert(ant_execute_quantum(loser,&colony,&world,1)==1);
    scheduler_destroy(&scheduler);

    /* Winning challenger clobbers a resident; resident cannot execute a grant. */
    ant_colony_reset(&colony);world_clear(&world);
    for(unsigned i=0;i<2;++i)ant_randomize(&colony.ants[i],&colony,&world,&rng,&walker);
    place(&colony,&colony.ants[0],2,2);place(&colony,&colony.ants[1],3,2);
    atomic_store(&colony.ants[0].tokens_fp,1000*TOKEN_FP_ONE);
    atomic_store(&colony.ants[1].tokens_fp,10*TOKEN_FP_ONE);
    assert(ant_execute_quantum(&colony.ants[0],&colony,&world,1)==1);
    assert(atomic_load(&colony.ants[1].flags)&ANT_F_CLOBBERED);
    assert(ant_execute_quantum(&colony.ants[1],&colony,&world,100)==0);
    assert(ant_occupant_at(&colony,3,2)==1);
    ant_colony_destroy(&colony);world_destroy(&world);
}

static void halts_and_clones(void)
{
    World w;AntColony c;Scheduler scheduler;Lfsr32 rng;
    assert(world_init(&w,16,16)==0);assert(ant_colony_init(&c,&w)==0);rng_seed(&rng,123);
    TurmiteRule halt=walker;halt.table[0][0].halt=true;halt.table[0][0].turn=TURN_R;
    Ant *a=&c.ants[0];ant_randomize(a,&c,&w,&rng,&halt);place(&c,a,8,8);
    atomic_store(&a->tokens_fp,100*TOKEN_FP_ONE);atomic_store(&a->token_rate,0);
    atomic_fetch_or(&a->flags,ANT_F_DRAINING);
    assert(ant_execute_quantum(a,&c,&w,50)==1);
    assert((atomic_load(&a->flags)&(ANT_F_HALTED|ANT_F_CLOBBERED))==ANT_F_HALTED);
    assert(ant_position_x(&c,0)==8 && ant_position_y(&c,0)==8 && world_load(&w,8*16+8)==1);
    assert(atomic_load(&a->heading)==2 && ant_execute_quantum(a,&c,&w,50)==0);
    assert(scheduler_init(&scheduler,&c,SCHED_WFQ,790)==0);
    recover_ant(&scheduler,a,1000);
    valid(a->rule);assert(a->rule==&c.runtime_rules[0] && ant_rule_index(a)==RULE_INDEX_RUNTIME);
    assert(runnable(a) && !(atomic_load(&a->flags)&ANT_F_DRAINING));
    assert(atomic_load(&a->tokens_fp)==atomic_load(&a->token_capacity_fp));
    assert(atomic_load(&a->token_rate)>=50000 && atomic_load(&a->token_rate)<=2000000);
    assert(atomic_load(&a->state)<a->rule->states && atomic_load(&a->heading)<4);
    assert(ant_packed_position(&c,0)==((8u<<16)|8u));
    assert(ant_mutation_count(&c,0)==0 && atomic_load(&c.collisions)==0);
    /* Deep clone: future edits of either table cannot affect its sibling. */
    ant_clone(&c.ants[1],&c,a,&w,&rng);
    assert(c.ants[1].rule!=a->rule && changes(c.ants[1].rule,a->rule)==0);
    TurmiteRule snapshot=*c.ants[1].rule;
    ant_mutate_in_place(a,&c);
    assert(changes(&snapshot,c.ants[1].rule)==0 && changes(a->rule,c.ants[1].rule)==1);
    ant_mutate_in_place(&c.ants[1],&c);valid(c.ants[1].rule);
    scheduler_destroy(&scheduler);ant_colony_reset(&c);
    ant_randomize(a,&c,&w,&rng,rules_get(0));
    assert(a->rule==rules_get(0) && ant_rule_index(a)==0); /* reset returns to catalogue */
    ant_colony_destroy(&c);world_destroy(&w);
}

int main(void)
{
    generation();collisions();halts_and_clones();
    puts("mutation ok: single edits, biased complexity, collision stop/pause/resume, HALT rebirth, clone isolation, catalogue reset");
}
