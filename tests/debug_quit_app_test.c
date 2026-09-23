#define main turmite_application_main
#include "../src/main.c"
#undef main
#include "png_image.h"
#include <assert.h>

static size_t exports, renders;
static uint64_t last_hash;
static uint64_t hash_frame(const RenderFrame *f)
{
    uint64_t h=14695981039346656037ull;
    for(size_t i=0;i<f->width*f->height;++i) h=(h^f->colors[i])*1099511628211ull;
    return h;
}
int __real_png_image_write(const char *,const RenderFrame *,const uint32_t *);
int __wrap_png_image_write(const char *path,const RenderFrame *f,const uint32_t *palette)
{
    if(strstr(path,"/frames/")) { ++exports; last_hash=hash_frame(f); }
    return __real_png_image_write(path,f,palette);
}
bool __real_render_argb(const RenderFrame *,const uint32_t *,uint32_t *,size_t);
bool __wrap_render_argb(const RenderFrame *f,const uint32_t *palette,uint32_t *pixels,size_t capacity)
{
    assert(exports==renders+1 && hash_frame(f)==last_hash);
    ++renders;
    return __real_render_argb(f,palette,pixels,capacity);
}
size_t __real_ant_execute_quantum(Ant *,AntColony *,World *,size_t);
size_t __wrap_ant_execute_quantum(Ant *a,AntColony *c,World *w,size_t q)
{
    struct timespec delay={.tv_nsec=3000000}; nanosleep(&delay,NULL);
    return __real_ant_execute_quantum(a,c,w,q);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    Universe u={0};
    u.width=160; u.height=120; u.cell_size=2; u.workers=1; u.initial_ants=2;
    u.quantum=1; u.min_service=1000; u.token_rate_divisor=1;
    u.lifetime_minutes=0.00001; /* A lifetime expiry must not interrupt Q. */
    u.dump_pages=3; u.dump_interval=0.02; u.dump_root=argv[1];
    atomic_init(&u.quit,false); atomic_init(&u.done,false);
    assert(universe_init(&u,123)==0);
    const TurmiteRule rule={.states=1,.colors=1,.table={{{0,TURN_H,0,false}}}};
    for(unsigned i=0;i<2;++i) {
        u.colony.ants[i].rule=&rule;
        atomic_store(&u.colony.ants[i].state,0);
        atomic_store(&u.colony.ants[i].tokens_fp,40*TOKEN_FP_ONE+TOKEN_FP_ONE/2);
    }
    scheduler_set_paused(&u.scheduler,true); u.paused=true;
    u.pool=worker_pool_create(1,1); assert(u.pool);
    assert(universe_enqueue(&u,SDLK_q));
    assert(universe_enqueue(&u,SDLK_q)); /* Repeated Q does not reset exports. */
    assert(universe_enqueue(&u,SDLK_SPACE));
    assert(universe_enqueue(&u,SDLK_r));
    assert(universe_enqueue(&u,SDLK_EQUALS));
    assert(!run_universe(&u));
    assert(!u.failed && atomic_load(&u.quit));
    assert(scheduler_drain_complete(&u.scheduler));
    assert(exports==renders && exports>=3 && exports==u.dump.png_frames);
    assert(ant_instruction_count(&u.colony,0)+ant_instruction_count(&u.colony,1)==80);
    assert(u.dump.page_count==3);
    WorkerPool *pool=u.pool;
    universe_destroy(&u); worker_pool_destroy(pool); renderer_shutdown();
    printf("SDL debug quit ok: %zu rendered frames, %zu PNGs, exact finite budget, paused/repeated Q, restart suppressed\n",renders,exports);
}
