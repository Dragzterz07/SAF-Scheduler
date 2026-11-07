#include "worker.h"
#include "preempt.h"
#include "sched_internal.h"
#include "timer.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

_Thread_local int current_worker_id = 0;

void worker_pin_to_core(int core_id) {
  if (core_id < 0) {
    return;
  }
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

int worker_get_online_cores(void) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return (n > 0) ? (int)n : 1;
}

int worker_get_count(void) {
  int cores = worker_get_online_cores();
  const char *env = getenv("SAF_WORKERS");
  if (env != NULL) {
    int w = atoi(env);
    if (w > 0) {
      return (w > cores) ? cores : w;
    }
  }
  return 1;
}

int worker_run_local(int worker_id, int core_id, void (*sched_loop_fn)(void),
                     int preempt_enabled, long tick_us) {
  current_worker_id = worker_id;
  worker_pin_to_core(core_id);

  sig_block_timer();

  guarded_stack_t sched_stack;
  int rc = alloc_guarded_stack(&sched_stack);
  if (rc != 0) {
    sig_unblock_timer();
    return rc;
  }

  ucontext_t worker_main_ctx;
  if (getcontext(&sched_ctx) == -1) {
    int err = errno;
    free_mapped_stack(sched_stack.map, sched_stack.map_len, sched_stack.vg_id);
    sig_unblock_timer();
    return -err;
  }

  sched_ctx.uc_stack.ss_sp = sched_stack.sp;
  sched_ctx.uc_stack.ss_size = sched_stack.sp_len;
  sched_ctx.uc_stack.ss_flags = 0;
  sched_ctx.uc_link = &worker_main_ctx;
  makecontext(&sched_ctx, sched_loop_fn, 0);

  if (preempt_enabled) {
    rc = timer_arm(tick_us);
    if (rc != 0) {
      free_mapped_stack(sched_stack.map, sched_stack.map_len,
                        sched_stack.vg_id);
      sig_unblock_timer();
      return rc;
    }
  }

  swapcontext(&worker_main_ctx, &sched_ctx);

  if (preempt_enabled) {
    timer_disarm();
  }

  free_mapped_stack(sched_stack.map, sched_stack.map_len, sched_stack.vg_id);
  sig_unblock_timer();
  return 0;
}

static void *worker_thread_trampoline(void *arg) {
  worker_t *w = (worker_t *)arg;
  worker_run_local(w->id, w->core_id, w->sched_loop_fn, w->preempt_enabled,
                   w->tick_us);
  return NULL;
}

int worker_spawn(worker_t *w, int worker_id, int core_id,
                 void (*sched_loop_fn)(void), int preempt_enabled,
                 long tick_us) {
  w->id = worker_id;
  w->core_id = core_id;
  w->sched_loop_fn = sched_loop_fn;
  w->preempt_enabled = preempt_enabled;
  w->tick_us = tick_us;

  return pthread_create(&w->thread, NULL, worker_thread_trampoline, w);
}

int worker_join(worker_t *w) { return pthread_join(w->thread, NULL); }
