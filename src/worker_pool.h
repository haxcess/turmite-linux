#ifndef TURMITE_WORKER_POOL_H
#define TURMITE_WORKER_POOL_H
#include "scheduler.h"

typedef struct WorkerPool WorkerPool;
WorkerPool *worker_pool_create(size_t members, size_t workers);
/* Registration and suspension are serialized per slot by its controller.
 * Register each scheduler in one slot only. Objects remain caller-owned. */
void worker_pool_enable(WorkerPool *pool, size_t slot, Scheduler *scheduler, World *world);
/* Removes from dispatch and waits for outstanding leases; then safe to reset/free. */
void worker_pool_suspend(WorkerPool *pool, size_t slot);
/* Call after all universe controllers have stopped. */
void worker_pool_destroy(WorkerPool *pool);
#endif
