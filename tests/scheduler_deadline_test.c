#include "../src/scheduler.c"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    World w; AntColony c; Scheduler s; Lfsr32 rng;
    assert(world_init(&w, 16, 16) == 0 && ant_colony_init(&c, &w) == 0);
    rng_seed(&rng, 123); Ant *a = &c.ants[0];
    ant_randomize(a, &c, &w, &rng, rules_get(0));
    assert(scheduler_init(&s, &c, SCHED_WFQ, 256) == 0);
    atomic_store(&a->token_rate, 100); atomic_store(&a->token_capacity_fp, 256 * TOKEN_FP_ONE);
    atomic_store(&a->tokens_fp, 0); s.last_token_us[0] = 1000000;
    scheduler_set_min_service(&s, 100);
    assert(candidate_locked(&s, 1000000, NULL) == NULL);
    assert(s.next_wake_us == 2000000);
    assert(candidate_locked(&s, 1999999, NULL) == NULL);
    /* Q16 truncation can defer an exact boundary by one microsecond. */
    assert(s.next_wake_us >= 2000000 && s.next_wake_us <= 2000001);
    assert(candidate_locked(&s, s.next_wake_us, NULL) == a);
    /* Long sleeps at slow rates must accrue the full elapsed interval. */
    atomic_store(&a->tokens_fp, 0); s.last_token_us[0] = 1000000;
    scheduler_set_token_rate_divisor(&s, 100);
    assert(candidate_locked(&s, 1000000, NULL) == NULL);
    assert(s.next_wake_us == 101000000);
    assert(candidate_locked(&s, 101000000, NULL) == a);
    assert(atomic_load(&a->tokens_fp) == 100 * TOKEN_FP_ONE);
    /* Extreme scale/elapsed products saturate instead of wrapping. */
    atomic_store(&a->tokens_fp, 0); atomic_store(&a->token_rate, UINT32_MAX);
    scheduler_set_token_rate_scale(&s, UINT32_MAX); s.last_token_us[0] = 1;
    accrue_tokens(&s, 0, a, UINT64_C(100000000000));
    assert(atomic_load(&a->tokens_fp) == atomic_load(&a->token_capacity_fp));
    /* Disabled/zero-rate and paused universes sleep until an explicit event. */
    atomic_store(&a->tokens_fp, 0); atomic_store(&a->token_rate, 0);
    assert(candidate_locked(&s, UINT64_C(100000000001), NULL) == NULL);
    assert(s.next_wake_us == UINT64_MAX);
    scheduler_set_paused(&s, true);
    assert(candidate_locked(&s, UINT64_C(100000000002), NULL) == NULL);
    assert(s.next_wake_us == UINT64_MAX);
    scheduler_set_paused(&s, false);
    atomic_fetch_or(&a->flags, ANT_F_CLOBBERED);
    assert(candidate_locked(&s, UINT64_C(100000000003), NULL) == NULL);
    assert(s.next_wake_us == UINT64_C(100000000003) + COLLISION_PAUSE_US);
    scheduler_destroy(&s); ant_colony_destroy(&c); world_destroy(&w);
    puts("scheduler deadlines ok: batching, long sleeps, overflow, zero rate, pause, collision recovery");
}
