#include "tcb.h"
#include "sched_internal.h"
#include "runqueue.h"
#include "deque.h"
#include "worker.h"
#include "preempt.h"
#include "preempt_internal.h"
#include "timer.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
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
_Thread_local volatile sig_atomic_t g_preemptions = 0;
_Thread_local volatile sig_atomic_t g_deferred = 0;

// Worker pool data structures
static int g_num_workers = 1;
static WSDeque **g_deques = NULL;

// Termination and activity tracking
static _Atomic int g_total_threads_created = 0;
static _Atomic int g_total_threads_finished = 0;

// Global statistics accumulation
static _Atomic long g_total_switches = 0;
static _Atomic long g_total_preemptions = 0;
static _Atomic long g_total_deferred = 0;
static size_t g_max_queue_len = 0;

#ifndef NDEBUG
long g_tcb_destroyed = 0;
#endif

static int next_id = 1;

int alloc_guarded_stack(guarded_stack_t *gs) {
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

void free_mapped_stack(char *map, size_t map_len, unsigned vg_id) {
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
    atomic_fetch_add_explicit(&g_total_threads_finished, 1, memory_order_release);
#ifndef NDEBUG
    g_tcb_destroyed++;
#endif
}

static void tramp(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    current->fn(current->arg);
    thread_exit();
}

// Try to pop from local deque first, or steal from peers if empty
static TCB *sched_next_thread(int my_id) {
    // 1. Try local deque (front gives fair FIFO round-robin)
    TCB *t = deque_steal(g_deques[my_id]);
    if (t != NULL) {
        return t;
    }

    // 2. Steal from peers (FIFO)
    if (g_num_workers > 1) {
        for (int i = 1; i < g_num_workers; i++) {
            int victim = (my_id + i) % g_num_workers;
            t = deque_steal(g_deques[victim]);
            if (t != NULL) {
                return t;
            }
        }
    }

    return NULL;
}

// Check if all user threads have finished
static inline int all_threads_done(void) {
    return atomic_load_explicit(&g_total_threads_finished, memory_order_acquire) ==
           atomic_load_explicit(&g_total_threads_created, memory_order_acquire);
}

// Stage 3 CHANGE: Removed the original rq_deque with Work Stealing Deque.

/* Runs on sched_ctx's own stack, with SIGVTALRM blocked (I2). The only
 * context switches anywhere are thread -> sched_ctx and sched_ctx ->
 * thread (I1); re-enqueueing happens only here, after the switch back
 * has completed (I4); only this loop frees a thread, after switching
 * off it (I5). preempt_pending is cleared every time a thread is
 * switched in, so a deferred preemption from one thread's turn can't
 * leak into the next thread's turn. */
static void sched_loop(void) {
    int my_id = current_worker_id;

    while (!all_threads_done()) {
        TCB *t = sched_next_thread(my_id);
        if (t == NULL) {
            /* Deque is empty and no task could be stolen right now.
             * Yield this OS thread briefly to avoid hogging CPU while peers make progress. */
            sched_yield();
            continue;
        }

        assert(current == NULL); /* I10: at most one RUNNING thread */

        current = t;
        t->state = SAF_RUNNING;
        preempt_pending = 0;
        g_switches++;

        swapcontext(&sched_ctx, &t->ctx);

        current = NULL;
        if (t->state == SAF_READY) {
            deque_push(g_deques[my_id], t);
        } else if (t->state == SAF_FINISHED) {
            tcb_destroy(t);
        }
    }

    // Accumulate local thread-local counters into globals upon worker exit
    atomic_fetch_add_explicit(&g_total_switches, g_switches, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_total_preemptions, g_preemptions, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_total_deferred, g_deferred, memory_order_relaxed);
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

// Temporary registration queue before scheduler_run initializes deques
static TCB *g_init_threads[SAF_MAX_THREADS];
static int g_init_count = 0;

int thread_create(void (*fn)(void *), void *arg) {
    if (fn == NULL) {
        return -EINVAL;
    }

    if (g_init_count >= SAF_MAX_THREADS) {
        return -EAGAIN;
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

    // If scheduler is already running with deques, push directly to current worker
    if (g_deques != NULL) {
        if (!deque_push(g_deques[current_worker_id], t)) {
            free_mapped_stack(gs.map, gs.map_len, gs.vg_id);
            free(t);
            next_id--;
            return -EAGAIN;
        }
    } else {
        g_init_threads[g_init_count++] = t;
    }

    atomic_fetch_add_explicit(&g_total_threads_created, 1, memory_order_relaxed);
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

    int num_cores = worker_get_online_cores();
    g_num_workers = worker_get_count();
    if (g_num_workers > num_cores) {
        g_num_workers = num_cores;
    }

    // Allocate worker deques
    g_deques = malloc((size_t)g_num_workers * sizeof(WSDeque *));
    if (g_deques == NULL) {
        return -ENOMEM;
    }
    for (int i = 0; i < g_num_workers; i++) {
        g_deques[i] = deque_create();
        if (g_deques[i] == NULL) {
            for (int j = 0; j < i; j++) deque_destroy(g_deques[j]);
            free(g_deques);
            g_deques = NULL;
            return -ENOMEM;
        }
    }

    // Distribute initial threads across deques (round-robin)
    size_t initial_q_len = (size_t)g_init_count;
    for (int i = 0; i < g_init_count; i++) {
        deque_push(g_deques[i % g_num_workers], g_init_threads[i]);
    }
    g_init_count = 0;

    int preempt_on = preempt_is_enabled();
    long tick_us = read_tick_us();

    // Spawn background worker pthreads (Workers 1 .. M-1)
    worker_t *workers = NULL;
    if (g_num_workers > 1) {
        workers = malloc((size_t)(g_num_workers - 1) * sizeof(worker_t));
        for (int i = 1; i < g_num_workers; i++) {
            worker_spawn(&workers[i - 1], i, i % num_cores, sched_loop, preempt_on, tick_us);
        }
    }

    // Calling thread executes as Worker 0
    worker_run_local(0, 0 % num_cores, sched_loop, preempt_on, tick_us);

    // Wait for background workers to complete
    if (g_num_workers > 1) {
        for (int i = 1; i < g_num_workers; i++) {
            worker_join(&workers[i - 1]);
        }
        free(workers);
    }

    if (initial_q_len > g_max_queue_len) {
        g_max_queue_len = initial_q_len;
    }

    // Free all deques
    for (int i = 0; i < g_num_workers; i++) {
        deque_destroy(g_deques[i]);
    }
    free(g_deques);
    g_deques = NULL;

    next_id = 1;
    atomic_store_explicit(&g_total_threads_created, 0, memory_order_relaxed);
    atomic_store_explicit(&g_total_threads_finished, 0, memory_order_relaxed);

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
    out->switches = atomic_load_explicit(&g_total_switches, memory_order_relaxed);
    out->preemptions = atomic_load_explicit(&g_total_preemptions, memory_order_relaxed);
    out->deferred = atomic_load_explicit(&g_total_deferred, memory_order_relaxed);
    out->ticks = timer_ticks();
    out->max_queue_len = (long)g_max_queue_len;
}
