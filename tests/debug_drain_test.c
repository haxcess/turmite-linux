/* Include scheduler internals to drive collision recovery with a virtual clock. */
#include "../src/scheduler.c"
#include <assert.h>
#include <stdio.h>

static TurmiteRule hold = {.states=1, .colors=1, .table={{{0, TURN_H, 0, false}}}};
static void setup(World *w, AntColony *c, Scheduler *s, unsigned n)
{
    Lfsr32 rng;
    assert(world_init(w, 32, 24)==0 && ant_colony_init(c,w)==0);
    rng_seed(&rng, 123);
    for (unsigned i=0;i<n;++i) ant_randomize(&c->ants[i],c,w,&rng,&hold);
    atomic_store(&c->active_population,n);
    assert(scheduler_init(s,c,SCHED_WFQ,4)==0);
}
static void destroy(World *w, AntColony *c, Scheduler *s)
{
    scheduler_destroy(s); ant_colony_destroy(c); world_destroy(w);
}
static void finish(World *w, AntColony *c, Scheduler *s, uint64_t now)
{
    Scheduler *members[]={s};
    for (unsigned step=0; !scheduler_drain_complete(s); ++step) {
        assert(step<1000);
        size_t grant=0;
        Ant *a=scheduler_acquire_group(members,1,now,NULL,&grant);
        if (a) {
            uint32_t before=atomic_load(&a->tokens_fp);
            size_t executed=ant_execute_quantum(a,c,w,grant);
            scheduler_release(s,a,executed,now);
            assert(atomic_load(&a->tokens_fp)==before-executed*TOKEN_FP_ONE);
        }
        now+=COLLISION_PAUSE_US+1;
    }
    assert(atomic_load(&c->active_population)==0);
    for(size_t i=0;i<c->occupancy_cells;++i) assert(atomic_load(&c->occupancy[i])==0);
}
int main(void)
{
    World w; AntColony c; Scheduler s;
    setup(&w,&c,&s,3);
    uint64_t now=monotonic_us();
    for (unsigned i=0;i<3;++i) s.last_token_us[i]=now;
    atomic_store(&c.ants[0].tokens_fp,5*TOKEN_FP_ONE+TOKEN_FP_ONE/2);
    atomic_store(&c.ants[1].tokens_fp,TOKEN_FP_ONE/2);
    atomic_store(&c.ants[2].tokens_fp,17*TOKEN_FP_ONE);
    Scheduler *members[]={&s}; size_t grant=0;
    Ant *leased=scheduler_acquire_group(members,1,now,NULL,&grant);
    assert(leased && grant==4);
    scheduler_set_min_service(&s,1000);
    scheduler_set_paused(&s,true);
    uint32_t before=atomic_load(&leased->tokens_fp);
    scheduler_begin_drain(&s); scheduler_begin_drain(&s);
    assert(!atomic_load(&s.paused) && !scheduler_drain_complete(&s));
    scheduler_set_paused(&s,true); assert(!atomic_load(&s.paused));
    Lfsr32 rng; rng_seed(&rng,456);
    assert(scheduler_double_population(&s,&w,&rng,now)==0);
    for(unsigned i=0;i<3;++i) assert(atomic_load(&c.ants[i].token_rate)==0);
    size_t executed=ant_execute_quantum(leased,&c,&w,grant);
    scheduler_release(&s,leased,executed,now+10000000);
    assert(atomic_load(&leased->tokens_fp)==before-executed*TOKEN_FP_ONE);
    finish(&w,&c,&s,now+10000000);
    uint64_t total=0; for(unsigned i=0;i<3;++i) total+=ant_instruction_count(&c,i);
    assert(total==22);
    destroy(&w,&c,&s);

    /* HALT recovery must not refill; collision delays must not look drained. */
    setup(&w,&c,&s,2); now=monotonic_us();
    atomic_store(&c.ants[0].tokens_fp,9*TOKEN_FP_ONE);
    atomic_fetch_or(&c.ants[0].flags,ANT_F_HALTED);
    atomic_store(&c.ants[1].tokens_fp,3*TOKEN_FP_ONE);
    atomic_fetch_or(&c.ants[1].flags,ANT_F_CLOBBERED);
    scheduler_begin_drain(&s);
    recover_ant(&s,&c.ants[0],now);
    assert(atomic_load(&c.ants[0].tokens_fp)==9*TOKEN_FP_ONE);
    assert(atomic_load(&c.ants[0].token_rate)==0);
    assert(atomic_load(&c.ants[0].flags)&ANT_F_DRAINING);
    /* Keep subsequent execution deterministic; rebirth itself was tested. */
    c.ants[0].rule=&hold; atomic_store(&c.ants[0].state,0);
    recover_ant(&s,&c.ants[1],now);
    assert(!scheduler_drain_complete(&s));
    finish(&w,&c,&s,now+COLLISION_PAUSE_US+1);
    assert(ant_instruction_count(&c,0)+ant_instruction_count(&c,1)==12);
    destroy(&w,&c,&s);

    /* A zero-balance in-flight lease still blocks completion until release. */
    setup(&w,&c,&s,1); now=monotonic_us(); s.last_token_us[0]=now;
    atomic_store(&c.ants[0].tokens_fp,TOKEN_FP_ONE);
    leased=scheduler_acquire_group(members,1,now,NULL,&grant); assert(leased);
    scheduler_begin_drain(&s);
    executed=ant_execute_quantum(leased,&c,&w,grant);
    assert(!scheduler_drain_complete(&s));
    scheduler_release(&s,leased,executed,now);
    assert(scheduler_drain_complete(&s));
    destroy(&w,&c,&s);
    puts("debug drain ok: finite budgets, partial batches, paused/in-flight, no cloning/refill, HALT/collision recovery");
}
