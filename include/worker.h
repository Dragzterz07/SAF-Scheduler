#ifndef WORKER_H
#define WORKER_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Worker thread abstraction for M:N scheduling.
 *
 * Manages underlying POSIX kernel threads (pthreads), CPU core pinning,
 * and per-worker scheduler stack setup.
 */

typedef struct worker {
  int id;
  int core_id;
  pthread_t thread;
  void (*sched_loop_fn)(void);
  int preempt_enabled;
  long tick_us;
} worker_t;

// Thread-local ID of the current worker (0 for Worker 0, 1 for Worker 1, etc.)
extern _Thread_local int current_worker_id;

// Pin calling thread to a given physical/logical CPU core ID.
void worker_pin_to_core(int core_id);

// Returns the number of online CPU cores available.
int worker_get_online_cores(void);

// Reads configured worker count from SAF_WORKERS, capped at online CPU cores, or defaults to 1.
int worker_get_count(void);

// Run scheduler hub loop on the calling thread (used for Worker 0).
int worker_run_local(int worker_id, int core_id, void (*sched_loop_fn)(void),
                     int preempt_enabled, long tick_us);

// Spawn a background worker pthread (Workers 1 .. M-1).
int worker_spawn(worker_t *w, int worker_id, int core_id,
                 void (*sched_loop_fn)(void), int preempt_enabled,
                 long tick_us);

// Wait for a background worker pthread to terminate.
int worker_join(worker_t *w);

#ifdef __cplusplus
}
#endif

#endif
