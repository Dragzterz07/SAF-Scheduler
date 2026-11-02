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

/* current is non-NULL exactly while some thread is RUNNING (I10).
 * sched_ctx is the hub every thread switches through (I1); it exists
 * only for the duration of one scheduler_run() call. */
extern TCB *current;
extern ucontext_t sched_ctx;

/* g_switches is written only from sched_loop, never from signal context,
 * so a plain long is fine. g_preemptions/g_deferred are written only
 * inside the signal handler (on_tick -> sched_on_tick), so they are
 * volatile sig_atomic_t (I6). */
extern long g_switches;
extern volatile sig_atomic_t g_preemptions;
extern volatile sig_atomic_t g_deferred;

/* Called by timer.c's on_tick after it counts the tick (I6, I7): forces
 * a preemption of the RUNNING thread, or defers it if preempt_count > 0. */
void sched_on_tick(void);

#ifndef NDEBUG
/* Debug-only instrumentation used by tests/test_coop.c. */
extern long g_tcb_destroyed;
saf_state_t saf_debug_current_state(void);
#endif

#endif
