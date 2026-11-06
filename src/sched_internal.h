#ifndef SCHED_INTERNAL_H
#define SCHED_INTERNAL_H

#include <signal.h>
#include <stddef.h>
#include <ucontext.h>

#include "tcb.h"

/* Full definition of the opaque TCB from tcb.h. Private to the library;
 * only tcb.c (and later timer.c) may include this header. */
struct TCB {
    int id;
    saf_state_t state;
    ucontext_t ctx;
    void (*fn)(void *);
    void *arg;
    char *map;
    size_t map_len;
    unsigned vg_id;
};

// Guarded stack representation with a low-end guard page
typedef struct {
    char *map;
    size_t map_len;
    void *sp;
    size_t sp_len;
    unsigned vg_id;
} guarded_stack_t;

int alloc_guarded_stack(guarded_stack_t *gs);
void free_mapped_stack(char *map, size_t map_len, unsigned vg_id);

/* S3 CHANGE: current and sched_ctx are _Thread_local so each pthread worker
 * has its own independent running-thread pointer and scheduler hub context. */

/* current is non-NULL exactly while some thread is RUNNING (I10).
 * sched_ctx is the hub every thread switches through (I1); it exists
 * only for the duration of one scheduler_run() call. */
extern _Thread_local TCB *current;
extern _Thread_local ucontext_t sched_ctx;

/* g_switches, g_preemptions, g_deferred are per-worker counters.
 * _Thread_local so workers don't race on writes.
 * saf_get_stats() will aggregate them across workers in S3. */

/* g_switches is written only from sched_loop, never from signal context,
 * so a plain long is fine. g_preemptions/g_deferred are written only
 * inside the signal handler (on_tick -> sched_on_tick), so they are
 * volatile sig_atomic_t (I6). */
extern _Thread_local long g_switches;
extern _Thread_local volatile sig_atomic_t g_preemptions;
extern _Thread_local volatile sig_atomic_t g_deferred;

/* Called by timer.c's on_tick after it counts the tick (I6, I7): forces
 * a preemption of the RUNNING thread, or defers it if preempt_count > 0. */
void sched_on_tick(void);

#ifndef NDEBUG
/* Debug-only instrumentation used by tests/test_coop.c. */
extern long g_tcb_destroyed;
saf_state_t saf_debug_current_state(void);
#endif

#endif
