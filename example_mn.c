#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include "tcb.h"
#include "preempt.h"

/*
 * M:N Demonstration:
 * M = 8 User-Level Green Threads
 * N = 4 Kernel Worker Threads (pthreads pinned to CPU cores)
 * (M = 2 * N)
 */

#define NUM_WORKERS 4
#define NUM_GREEN_THREADS 8  /* M = 2 * N (8 user threads on 4 kernel workers) */

typedef struct {
    int thread_id;
    int iterations;
    int last_cpu_core;   // Tracks the previous CPU core this thread ran on
} task_data_t;

static void user_task(void *arg) {
    task_data_t *data = (task_data_t *)arg;

    for (int i = 1; i <= data->iterations; i++) {
        // Query the physical CPU core this user thread is currently executing on
        int current_cpu = sched_getcpu();

        if (data->last_cpu_core == -1) {
            // First time running: initial placement
            saf_printf("[User Thread %2d] Iteration %d/%d on Core %d (Initial Assignment)\n",
                       data->thread_id, i, data->iterations, current_cpu);
        } else if (data->last_cpu_core != current_cpu) {
            // Core changed! The thread was STOLEN by another worker
            saf_printf(">>> [User Thread %2d] Iteration %d/%d on Core %d *** STOLEN from Core %d! ***\n",
                       data->thread_id, i, data->iterations, current_cpu, data->last_cpu_core);
        } else {
            // Ran on the same core
            saf_printf("[User Thread %2d] Iteration %d/%d on Core %d (Local Deque)\n",
                       data->thread_id, i, data->iterations, current_cpu);
        }

        data->last_cpu_core = current_cpu;

        // Voluntarily yield back to scheduler
        thread_yield();
    }

    saf_printf("[User Thread %2d] ===> Finished execution!\n", data->thread_id);
}

int main(void) {
    char workers_str[16];
    snprintf(workers_str, sizeof(workers_str), "%d", NUM_WORKERS);
    setenv("SAF_WORKERS", workers_str, 1);

    printf("======================================================================\n");
    printf("   SAF-Scheduler: M:N Demonstration (M = %d, N = %d)\n", NUM_GREEN_THREADS, NUM_WORKERS);
    printf("   Mapping %d User Green Threads across %d Kernel Worker Cores\n", NUM_GREEN_THREADS, NUM_WORKERS);
    printf("======================================================================\n\n");

    task_data_t tasks[NUM_GREEN_THREADS];

    printf("[Step 1] Creating %d green threads...\n", NUM_GREEN_THREADS);
    for (int i = 0; i < NUM_GREEN_THREADS; i++) {
        tasks[i].thread_id = i + 1;
        tasks[i].iterations = 4;
        tasks[i].last_cpu_core = -1;  // Not scheduled yet
        int tid = thread_create(user_task, &tasks[i]);
        if (tid < 0) {
            fprintf(stderr, "Failed to create thread %d\n", i + 1);
            return 1;
        }
    }

    printf("[Step 2] Launching scheduler with %d parallel worker threads...\n\n", NUM_WORKERS);
    int rc = scheduler_run();
    if (rc != 0) {
        fprintf(stderr, "scheduler_run error: %d\n", rc);
        return 1;
    }

    printf("\n======================================================================\n");
    printf("[Step 3] All %d user threads completed across the %d worker cores!\n", NUM_GREEN_THREADS, NUM_WORKERS);
    saf_stats_t stats;
    saf_get_stats(&stats);
    printf("[Statistics] Total Context Switches: %ld | Max Queue Depth: %ld\n",
           stats.switches, stats.max_queue_len);
    printf("======================================================================\n");

    return 0;
}
