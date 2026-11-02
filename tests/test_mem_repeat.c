#include "tcb.h"
#include "sched_internal.h"
#include "preempt.h"

#include <stdio.h>
#include <stdlib.h>

/* assert() disappears under -DNDEBUG; this test depends on the
 * debug-only g_tcb_destroyed counter (sched_internal.h), so the whole
 * file is compiled out under NDEBUG, same as test_coop.c's debug-only
 * tests. */
#ifndef NDEBUG

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define RUNS 300
#define THREADS_PER_RUN 5

static void exits_immediately(void *arg) {
    (void)arg;
}

static void yields_a_few_times(void *arg) {
    (void)arg;
    for (int i = 0; i < 3; i++) {
        thread_yield();
    }
}

static void allocates_then_exits(void *arg) {
    int id = *(int *)arg;
    void *p = saf_malloc((size_t)(id + 1) * 16);
    CHECK(p != NULL);
    saf_free(p);
}

static void spins_a_few_iterations(void *arg) {
    (void)arg;
    for (volatile int i = 0; i < 10000; i++) {
        /* busy work; relies on preemption or an eventual yield to
         * hand control back, exercising sched_on_tick's mid-work
         * context switch path when preemption is on. */
    }
    thread_yield();
}

typedef void (*worker_fn_t)(void *);

/* Every TCB created across RUNS cycles of scheduler_run() must be torn
 * down (stack munmap'd and struct freed) before the process exits -
 * nothing here running on an outer loop's leftover state. g_tcb_destroyed
 * is the scheduler's own ground truth for "a TCB was fully freed" (it is
 * incremented once, inside tcb_destroy(), right after free_mapped_stack()
 * and free()), so comparing it against the exact number of threads we
 * created is a precise leak check, independent of any instrumentation
 * requiring valgrind. */
static void test_repeated_runs_leak_nothing(void) {
    g_tcb_destroyed = 0;
    long total_created = 0;

    static int ids[THREADS_PER_RUN];
    for (int i = 0; i < THREADS_PER_RUN; i++) {
        ids[i] = i;
    }

    worker_fn_t kinds[] = {
        exits_immediately,
        yields_a_few_times,
        allocates_then_exits,
        spins_a_few_iterations,
    };
    size_t n_kinds = sizeof(kinds) / sizeof(kinds[0]);

    for (int run = 0; run < RUNS; run++) {
        setenv("SAF_PREEMPT", (run % 2 == 0) ? "0" : "1", 1);

        for (int i = 0; i < THREADS_PER_RUN; i++) {
            worker_fn_t fn = kinds[(size_t)(run + i) % n_kinds];
            int rc = thread_create(fn, &ids[i]);
            CHECK(rc > 0);
            total_created++;
        }

        int rc = scheduler_run();
        CHECK(rc == 0);
    }

    CHECK(g_tcb_destroyed == total_created);
}

int main(void) {
    test_repeated_runs_leak_nothing();
    printf("PASS test_mem_repeat\n");
    return 0;
}

#else /* NDEBUG */

int main(void) {
    printf("SKIP test_mem_repeat (requires debug build)\n");
    return 0;
}

#endif
