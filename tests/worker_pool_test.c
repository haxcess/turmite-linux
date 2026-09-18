#include "worker_pool.h"
#include "rules.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static World worlds[3];
static AntColony colonies[3];
static Scheduler schedulers[3];
static pthread_mutex_t observation = PTHREAD_MUTEX_INITIALIZER;
static pthread_t seen[2];
static size_t seen_count, active, peak;
static bool leased[3][TURMITE_MAX_ANTS];
static int fail_after = -1;
int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *v)
{
    if (fail_after == 0) return EAGAIN;
    if (fail_after > 0) --fail_after;
    return __real_pthread_create(t, a, f, v);
}
static void delay(long ns) { struct timespec t = {0, ns}; nanosleep(&t, NULL); }
size_t __real_ant_execute_quantum(Ant *, AntColony *, World *, size_t);
size_t __wrap_ant_execute_quantum(Ant *a, AntColony *c, World *w, size_t n)
{
    size_t u = (size_t)(c - colonies), i = (size_t)(a - c->ants);
    assert(u < 3 && i < TURMITE_MAX_ANTS);
    pthread_mutex_lock(&observation);
    assert(!leased[u][i]); leased[u][i] = true;
    size_t j;
    for (j = 0; j < seen_count; ++j) if (pthread_equal(seen[j], pthread_self())) break;
    if (j == seen_count) { assert(seen_count < 2); seen[seen_count++] = pthread_self(); }
    ++active; if (active > peak) peak = active;
    assert(active <= 2);
    pthread_mutex_unlock(&observation);
    delay(1000000L);
    size_t executed = __real_ant_execute_quantum(a, c, w, n);
    pthread_mutex_lock(&observation);
    leased[u][i] = false; --active;
    pthread_mutex_unlock(&observation);
    return executed;
}
static void initialize(size_t u)
{
    assert(world_init(&worlds[u], 300, 200) == 0);
    assert(ant_colony_init(&colonies[u], &worlds[u]) == 0);
    Lfsr32 rng; rng_seed(&rng, (uint32_t)(12345 + u));
    size_t count = u ? 7 : 6;
    for (size_t i = 0; i < count; ++i)
        ant_randomize(&colonies[u].ants[i], &colonies[u], &worlds[u], &rng, rules_get(0));
    atomic_store(&colonies[u].active_population, count);
    assert(scheduler_init(&schedulers[u], &colonies[u], SCHED_WFQ, 20) == 0);
    scheduler_set_min_service(&schedulers[u], 300);
}
static void destroy(size_t u)
{
    scheduler_destroy(&schedulers[u]); ant_colony_destroy(&colonies[u]); world_destroy(&worlds[u]);
}
static void await_progress(size_t u, uint64_t before)
{
    for (unsigned i = 0; i < 2000; ++i) {
        if (scheduler_get_dispatches(&schedulers[u]) > before + 5) return;
        delay(1000000L);
    }
    assert(!"universe stopped progressing");
}
int main(void)
{
    alarm(20);
    fail_after = 1;
    assert(worker_pool_create(3, 2) == NULL); /* First worker must be joined. */
    fail_after = -1;
    for (size_t u = 0; u < 3; ++u) initialize(u);
    WorkerPool *p = worker_pool_create(3, 2); assert(p);
    for (size_t u = 0; u < 3; ++u) worker_pool_enable(p, u, &schedulers[u], &worlds[u]);
    for (size_t u = 0; u < 3; ++u) await_progress(u, 0);
    scheduler_set_paused(&schedulers[0], true);
    uint64_t paused = scheduler_get_dispatches(&schedulers[0]);
    await_progress(1, scheduler_get_dispatches(&schedulers[1]));
    await_progress(2, scheduler_get_dispatches(&schedulers[2]));
    assert(scheduler_get_dispatches(&schedulers[0]) == paused);
    scheduler_set_paused(&schedulers[0], false);
    await_progress(0, paused);
    for (unsigned pass = 0; pass < 20; ++pass) {
        worker_pool_suspend(p, 0);
        destroy(0);
        await_progress(1, scheduler_get_dispatches(&schedulers[1]));
        initialize(0);
        worker_pool_enable(p, 0, &schedulers[0], &worlds[0]);
        await_progress(0, 0);
    }
    for (size_t u = 0; u < 3; ++u) worker_pool_suspend(p, u);
    worker_pool_destroy(p);
    assert(seen_count == 2 && peak == 2 && active == 0);
    for (size_t u = 0; u < 3; ++u) destroy(u);
    puts("shared pool ok: 20 ants, 3 universes, 2 workers; pause, reset, lease ownership, creation failure");
    return 0;
}
