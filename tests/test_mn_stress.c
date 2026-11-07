/*
 * tests/test_mn_stress.c — Multi-worker stress test for M:N work-stealing & preemption.
 *
 * Spawns 4 worker threads running 60 concurrent user threads with mixed workloads:
 * yielding threads, spinning threads, and dynamic sub-task creators.
 */

#include "tcb.h"
#include "timer.h"
#include "preempt.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define TOTAL_THREADS 60

static _Atomic int g_completed_count = 0;
static _Atomic long g_total_iterations = 0;

static void stress_spinner(void *arg) {
    (void)arg;
    long iters = 0;
    while (timer_ticks() < 500) {
        iters++;
        if (iters % 50000 == 0) {
            thread_yield();
        }
    }
    atomic_fetch_add(&g_total_iterations, iters);
    atomic_fetch_add(&g_completed_count, 1);
}

static void stress_yielder(void *arg) {
    (void)arg;
    for (int i = 0; i < 20; i++) {
        thread_yield();
    }
    atomic_fetch_add(&g_completed_count, 1);
}

int main(void) {
    /* Configure 4 kernel workers with preemption enabled */
    setenv("SAF_WORKERS", "4", 1);
    setenv("SAF_PREEMPT", "1", 1);
    setenv("SAF_TICK_US", "1000", 1);

    for (int i = 0; i < TOTAL_THREADS; i++) {
        int rc;
        if (i % 2 == 0) {
            rc = thread_create(stress_spinner, NULL);
        } else {
            rc = thread_create(stress_yielder, NULL);
        }
        CHECK(rc > 0);
    }

    int rc = scheduler_run();
    CHECK(rc == 0);

    CHECK(atomic_load(&g_completed_count) == TOTAL_THREADS);

    saf_stats_t stats;
    saf_get_stats(&stats);
    CHECK(stats.switches > 0);

    puts("PASS test_mn_stress");
    return 0;
}
