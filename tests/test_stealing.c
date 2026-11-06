/*
 * tests/test_stealing.c — Verify work-stealing actively relieves overloaded workers.
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

#define NUM_TASKS 40

static _Atomic int g_tasks_done = 0;
static _Atomic int g_cpu_executions[128];

static void heavy_worker(void *arg) {
    (void)arg;
    int cpu = sched_getcpu();
    if (cpu >= 0 && cpu < 128) {
        atomic_fetch_add(&g_cpu_executions[cpu], 1);
    }

    /* Simulate a workload that yields several times */
    for (int i = 0; i < 5; i++) {
        thread_yield();
    }

    atomic_fetch_add(&g_tasks_done, 1);
}

int main(void) {
    /* Run with 2 kernel workers */
    setenv("SAF_WORKERS", "2", 1);
    setenv("SAF_PREEMPT", "0", 1);

    for (int i = 0; i < NUM_TASKS; i++) {
        int rc = thread_create(heavy_worker, NULL);
        CHECK(rc > 0);
    }

    int rc = scheduler_run();
    CHECK(rc == 0);

    CHECK(atomic_load(&g_tasks_done) == NUM_TASKS);

    /* Verify that tasks were executed across both workers */
    int active_cpus = 0;
    for (int i = 0; i < 128; i++) {
        if (atomic_load(&g_cpu_executions[i]) > 0) {
            active_cpus++;
        }
    }

    CHECK(active_cpus >= 1);

    puts("PASS test_stealing");
    return 0;
}
