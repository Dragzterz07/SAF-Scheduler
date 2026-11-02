#include "tcb.h"
#include "sched_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* assert() disappears under -DNDEBUG; test_sequence() must run
 * (and its checks must hold) under every build config, so it uses
 * CHECK() instead. The two tests below are debug-only already (they
 * depend on debug-only instrumentation), so plain assert() is fine
 * there - the whole function is compiled out under NDEBUG anyway. */
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* --- Test 1: exact cooperative round-robin sequence --- */

static int g_sequence[9];
static int g_seq_len = 0;

static void seq_worker(void *arg) {
    int id = *(int *)arg;
    for (int i = 0; i < 3; i++) {
        g_sequence[g_seq_len++] = id;
        thread_yield();
    }
}

static void test_sequence(void) {
    setenv("SAF_PREEMPT", "0", 1);

    int ids[3] = {1, 2, 3};
    for (int i = 0; i < 3; i++) {
        int rc = thread_create(seq_worker, &ids[i]);
        CHECK(rc > 0);
    }

    int rc = scheduler_run();
    CHECK(rc == 0);

    int expected[9] = {1, 2, 3, 1, 2, 3, 1, 2, 3};
    CHECK(g_seq_len == 9);
    for (int i = 0; i < 9; i++) {
        CHECK(g_sequence[i] == expected[i]);
    }
}

#ifndef NDEBUG

/* --- Test 2: a thread sees itself as RUNNING after every yield --- */

static void yield_state_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        thread_yield();
        assert(saf_debug_current_state() == SAF_RUNNING);
    }
}

static void test_yield_state(void) {
    int rc = thread_create(yield_state_worker, NULL);
    assert(rc > 0);

    rc = scheduler_run();
    assert(rc == 0);
}

/* --- Test 3: an early-finishing thread is freed before the others finish --- */

static void short_worker(void *arg) {
    (void)arg;
    thread_yield();
    /* returns here; tramp calls thread_exit() automatically */
}

static void long_worker(void *arg) {
    (void)arg;
    thread_yield();
    assert(g_tcb_destroyed == 1);
    thread_yield();
}

static void test_early_finish_freed(void) {
    g_tcb_destroyed = 0;

    int rc = thread_create(short_worker, NULL);
    assert(rc > 0);
    rc = thread_create(long_worker, NULL);
    assert(rc > 0);

    rc = scheduler_run();
    assert(rc == 0);
    assert(g_tcb_destroyed == 2);
}

#endif /* NDEBUG */

int main(void) {
    test_sequence();
#ifndef NDEBUG
    test_yield_state();
    test_early_finish_freed();
#endif

    printf("PASS test_coop\n");
    return 0;
}
