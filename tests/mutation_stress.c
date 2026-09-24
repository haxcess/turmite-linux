#include "ant.h"
#include "scheduler.h"
#include "dump.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

typedef struct {World *world;AntColony *colony;Scheduler *scheduler;} Worker;
static uint64_t now_us(void)
{
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;
}
static void *run(void *arg)
{
    Worker *w=arg;
    for(;;) {
        size_t grant=0;
        Ant *a=scheduler_acquire(w->scheduler,now_us(),&grant);
        if(!a)break;
        size_t n=ant_execute_quantum(a,w->colony,w->world,grant);
        scheduler_release(w->scheduler,a,n,now_us());
    }
    return NULL;
}
int main(void)
{
    World world;AntColony colony;Scheduler scheduler;Lfsr32 rng;DumpCapture capture;
    assert(world_init(&world,24,24)==0);assert(ant_colony_init(&colony,&world)==0);
    rng_seed(&rng,0x12345678);
    for(unsigned i=0;i<16;++i)ant_randomize(&colony.ants[i],&colony,&world,&rng,rules_get(i));
    atomic_store(&colony.active_population,16);
    assert(scheduler_init(&scheduler,&colony,SCHED_WFQ,790)==0);
    scheduler.collision_mutation = true;
    scheduler_set_min_service(&scheduler,1000);scheduler_set_token_rate_divisor(&scheduler,3);
    assert(dump_capture_init(&capture,&world,123,3,0.05,0)==0);
    pthread_t threads[8];Worker worker={&world,&colony,&scheduler};
    for(unsigned i=0;i<8;++i)assert(pthread_create(&threads[i],NULL,run,&worker)==0);
    for(unsigned i=0;i<20;++i) {
        struct timespec delay={.tv_nsec=50000000L};nanosleep(&delay,NULL);
        if(i%4==0) (void)scheduler_spawn_ant(&scheduler,&world,&rng,now_us());
        if(i%4==2) (void)scheduler_begin_culling(&scheduler,8);
        dump_capture_now(&capture,&world,&colony,&scheduler,&rng,i*0.05,i*0.05);
    }
    scheduler_stop(&scheduler);
    for(unsigned i=0;i<8;++i)pthread_join(threads[i],NULL);
    bool seen[TURMITE_MAX_ANTS]={false};
    for(size_t cell=0;cell<world.cells;++cell) {
        uint8_t owner=atomic_load(&colony.occupancy[cell]);if(!owner)continue;
        assert(owner<=TURMITE_MAX_ANTS && !seen[owner-1]);seen[owner-1]=true;
        uint32_t pos=ant_packed_position(&colony,owner-1);
        assert((pos>>16)*(unsigned)world.width+(pos&65535u)==cell);
    }
    uint64_t mutations=0;
    for(size_t i=0;i<TURMITE_MAX_ANTS;++i) {
        const Ant *a=&colony.ants[i];mutations+=ant_mutation_count(&colony,i);
        if(!a->rule)continue;
        if(ant_rule_index(a)==RULE_INDEX_RUNTIME)assert(a->rule==&colony.runtime_rules[i]);
        else assert(a->rule==rules_get(ant_rule_index(a)));
        assert(atomic_load(&a->state)<a->rule->states);
    }
    assert(mutations>0 && atomic_load(&colony.collisions)>0);
    printf("mutation stress ok: 8 workers, spawning/halving/capture, %llu collisions, %llu retained mutations\n",
           (unsigned long long)atomic_load(&colony.collisions),(unsigned long long)mutations);
    dump_capture_destroy(&capture);scheduler_destroy(&scheduler);ant_colony_destroy(&colony);world_destroy(&world);
}
