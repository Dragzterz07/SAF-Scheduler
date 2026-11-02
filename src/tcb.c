#include "tcb.h"
#include "sched_internal.h"
#include "runqueue.h"
#include "preempt.h"
#include "preempt_internal.h"
#include "timer.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__has_include)
#  if __has_include(<valgrind/valgrind.h>)
#    include <valgrind/valgrind.h>
#    define HAS_VALGRIND 1
#  endif
#endif

#define STACK_SIZE ((size_t)64 * 1024)
#define SAF_MAX_THREADS 1024
#define DEFAULT_TICK_US 10000L

// S3 CHANGE: _Thread_local gives each pthread worker its own private copy.
_Thread_local TCB *current = NULL;
_Thread_local ucontext_t sched_ctx;
_Thread_local long g_switches = 0;
static size_t g_max_queue_len = 0;
_Thread_local volatile sig_atomic_t g_preemptions = 0;
_Thread_local volatile sig_atomic_t g_deferred = 0;

#ifndef NDEBUG
long g_tcb_destroyed = 0;
#endif

static ucontext_t main_ctx;
static int next_id = 1;
static int rq_initialized = 0;

typedef struct {
    char *map;
    size_t map_len;
    void *sp;
    size_t sp_len;
    unsigned vg_id;
} guarded_stack_t;

static int alloc_guarded_stack(guarded_stack_t *gs) {
    errno = 0;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) {
        return -(errno != 0 ? errno : EINVAL);
    }

    size_t map_len = STACK_SIZE + (size_t)page;
    char *map = mmap(NULL, map_len, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (map == MAP_FAILED) {
        return -errno;
    }
    if (mprotect(map, (size_t)page, PROT_NONE) == -1) {
        int err = errno;
        munmap(map, map_len);
        return -err;
    }

    unsigned vg_id = 0;
#ifdef HAS_VALGRIND
    vg_id = VALGRIND_STACK_REGISTER(map + page, map + map_len);
#endif

    gs->map = map;
    gs->map_len = map_len;
    gs->sp = map + page;
    gs->sp_len = STACK_SIZE;
    gs->vg_id = vg_id;
    return 0;
}

static void free_mapped_stack(char *map, size_t map_len, unsigned vg_id) {
#ifdef HAS_VALGRIND
    if (vg_id != 0) {
        VALGRIND_STACK_DEREGISTER(vg_id);
    }
#else
    (void)vg_id;
#endif
    munmap(map, map_len);
}

static void tcb_destroy(TCB *t) {
    free_mapped_stack(t->map, t->map_len, t->vg_id);
    free(t);
#ifndef NDEBUG
    g_tcb_destroyed++;
#endif
}

static void tramp(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    sigprocmask(SIG_UNBLOCK, &set, NULL);

    current->fn(current->arg);
    thread_exit();
}

/* Runs on sched_ctx's own stack, with SIGVTALRM blocked (I2). The only
 * context switches anywhere are thread -> sched_ctx and sched_ctx ->
 * thread (I1); re-enqueueing happens only here, after the switch back
 * has completed (I4); only this loop frees a thread, after switching
 * off it (I5). preempt_pending is cleared every time a thread is
 * switched in, so a deferred preemption from one thread's turn can't
 * leak into the next thread's turn. */
static void sched_loop(void) {
    TCB *t;
    while ((t = rq_dequeue()) != NULL) {
        assert(current == NULL); /* I10: at most one RUNNING thread */

        current = t;
        t->state = SAF_RUNNING;
        preempt_pending = 0;
        g_switches++;

        swapcontext(&sched_ctx, &t->ctx);

        current = NULL;
        if (t->state == SAF_READY) {
            rq_enqueue(t);
        } else if (t->state == SAF_FINISHED) {
            tcb_destroy(t);
        }
    }
}

/* Called from timer.c's on_tick, i.e. from the signal handler (I6): only
 * reads current/state, checks preempt_count, sets preempt_pending,
 * increments counters, and calls swapcontext. No stdio, malloc, locks,
 * or exit. Calling swapcontext from a signal handler is not strictly
 * async-signal-safe per POSIX, but is the standard, validated technique
 * for user-level preemption on Linux/glibc (R2). */
void sched_on_tick(void) {
    if (current == NULL || current->state != SAF_RUNNING) {
        return; /* between threads, on the scheduler's own stack */
    }
    if (preempt_count > 0) {
        preempt_pending = 1;
        g_deferred++;
        return;
    }
    g_preemptions++;
    current->state = SAF_READY;
    swapcontext(&current->ctx, &sched_ctx); /* resumes here when rescheduled */
}

int thread_create(void (*fn)(void *), void *arg) {
    if (fn == NULL) {
        return -EINVAL;
    }

    if (!rq_initialized) {
        int rc = rq_init(SAF_MAX_THREADS);
        if (rc != 0) {
            return rc;
        }
        rq_initialized = 1;
    }

    TCB *t = calloc(1, sizeof(TCB));
    if (t == NULL) {
        return -ENOMEM;
    }

    guarded_stack_t gs;
    int rc = alloc_guarded_stack(&gs);
    if (rc != 0) {
        free(t);
        return rc;
    }

    if (getcontext(&t->ctx) == -1) {
        int err = errno;
        free_mapped_stack(gs.map, gs.map_len, gs.vg_id);
        free(t);
        return -err;
    }

    t->ctx.uc_stack.ss_sp = gs.sp;
    t->ctx.uc_stack.ss_size = gs.sp_len;
    t->ctx.uc_stack.ss_flags = 0;
    t->ctx.uc_link = &sched_ctx; /* safety net; tramp always calls thread_exit() */
    sigaddset(&t->ctx.uc_sigmask, SIGVTALRM);

    t->id = next_id++;
    t->state = SAF_READY;
    t->fn = fn;
    t->arg = arg;
    t->map = gs.map;
    t->map_len = gs.map_len;
    t->vg_id = gs.vg_id;

    makecontext(&t->ctx, tramp, 0);

    rc = rq_enqueue(t);
    if (rc != 0) {
        free_mapped_stack(gs.map, gs.map_len, gs.vg_id);
        free(t);
        next_id--;
        return rc;
    }

    return t->id;
}

void thread_yield(void) {
    if (current == NULL) {
        return;
    }
    assert(current->state == SAF_RUNNING); /* I10 */

    sig_block_timer();
    current->state = SAF_READY;
    swapcontext(&current->ctx, &sched_ctx);
    sig_unblock_timer();
}

void thread_exit(void) {
    if (current == NULL) {
        return;
    }

    sig_block_timer();
    current->state = SAF_FINISHED;
    setcontext(&sched_ctx);
    /* unreachable */
}

/* SAF_TICK_US: default 10000, minimum 100; anything else invalid
 * (non-numeric, trailing garbage, below minimum) falls back to the
 * default rather than erroring (per the public API's documented
 * behavior). */
static long read_tick_us(void) {
    const char *env = getenv("SAF_TICK_US");
    if (env == NULL) {
        return DEFAULT_TICK_US;
    }

    char *end = NULL;
    long val = strtol(env, &end, 10);
    if (end == env || *end != '\0' || val < 100) {
        return DEFAULT_TICK_US;
    }
    return val;
}

static int preempt_is_enabled(void) {
    const char *env = getenv("SAF_PREEMPT");
    return !(env != NULL && strcmp(env, "0") == 0);
}

int scheduler_run(void) {
    if (current != NULL) {
        return -EBUSY;
    }

    sig_block_timer();

    guarded_stack_t sched_stack;
    int rc = alloc_guarded_stack(&sched_stack);
    if (rc != 0) {
        sig_unblock_timer();
        return rc;
    }

    if (getcontext(&sched_ctx) == -1) {
        int err = errno;
        free_mapped_stack(sched_stack.map, sched_stack.map_len, sched_stack.vg_id);
        sig_unblock_timer();
        return -err;
    }
    sched_ctx.uc_stack.ss_sp = sched_stack.sp;
    sched_ctx.uc_stack.ss_size = sched_stack.sp_len;
    sched_ctx.uc_stack.ss_flags = 0;
    sched_ctx.uc_link = &main_ctx;
    makecontext(&sched_ctx, sched_loop, 0);

    if (preempt_is_enabled()) {
        rc = timer_arm(read_tick_us());
        if (rc != 0) {
            free_mapped_stack(sched_stack.map, sched_stack.map_len, sched_stack.vg_id);
            sig_unblock_timer();
            return rc;
        }
    }

    swapcontext(&main_ctx, &sched_ctx);

    timer_disarm();

    size_t run_max_queue_len = rq_max_size();
    if (run_max_queue_len > g_max_queue_len) {
        g_max_queue_len = run_max_queue_len;
    }

    free_mapped_stack(sched_stack.map, sched_stack.map_len, sched_stack.vg_id);
    rq_destroy();
    rq_initialized = 0;
    next_id = 1;

    sig_unblock_timer();

    return 0;
}

#ifndef NDEBUG
saf_state_t saf_debug_current_state(void) {
    return current->state;
}
#endif

void saf_get_stats(saf_stats_t *out) {
    if (out == NULL) {
        return;
    }
    out->switches = g_switches;
    out->preemptions = g_preemptions;
    out->deferred = g_deferred;
    out->ticks = timer_ticks();
    out->max_queue_len = (long)g_max_queue_len;
}
