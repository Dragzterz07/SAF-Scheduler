/*
 * tests/test_mn_basic.c — Basic M:N multi-worker execution and work distribution test.
 */

#include "tcb.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define NUM_TASKS 30

static _Atomic int g_completed_tasks = 0;
static _Atomic int g_worker_hits[128];

static void mn_worker(void *arg) {
    (void)arg;
    int cpu = sched_getcpu();
    if (cpu >= 0 && cpu < 128) {
        atomic_fetch_add(&g_worker_hits[cpu], 1);
    }

    for (int i = 0; i < 3; i++) {
        thread_yield();
    }

    atomic_fetch_add(&g_completed_tasks, 1);
}

static void test_multi_worker_execution(void) {
    setenv("SAF_WORKERS", "2", 1);
    setenv("SAF_PREEMPT", "0", 1);

    for (int i = 0; i < NUM_TASKS; i++) {
        int rc = thread_create(mn_worker, NULL);
        CHECK(rc > 0);
    }

    int rc = scheduler_run();
    CHECK(rc == 0);

    CHECK(atomic_load(&g_completed_tasks) == NUM_TASKS);
}

int main(void) {
    test_multi_worker_execution();
    puts("PASS test_mn_basic");
    return 0;
}
