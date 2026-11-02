#include "tcb.h"
#include "timer.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define THREAD_COUNT 3
#define STOP_TICKS 300

typedef struct {
    long iterations;
} spin_data_t;

static spin_data_t workers[THREAD_COUNT];

static void spin_worker(void *arg) {
    spin_data_t *worker = arg;
    while (timer_ticks() < STOP_TICKS) {
        worker->iterations++;
    }
}

int main(void) {
    for (int i = 0; i < THREAD_COUNT; i++) {
        workers[i].iterations = 0;
        CHECK(thread_create(spin_worker, &workers[i]) > 0);
    }

    CHECK(scheduler_run() == 0);

    long min_iterations = workers[0].iterations;
    long max_iterations = workers[0].iterations;
    for (int i = 0; i < THREAD_COUNT; i++) {
        CHECK(workers[i].iterations > 0);
        if (workers[i].iterations < min_iterations) {
            min_iterations = workers[i].iterations;
        }
        if (workers[i].iterations > max_iterations) {
            max_iterations = workers[i].iterations;
        }
    }

    saf_stats_t stats;
    saf_get_stats(&stats);
    CHECK(stats.preemptions > 0);
    CHECK(stats.max_queue_len >= THREAD_COUNT);
    CHECK(min_iterations > 0);
    CHECK((double)max_iterations / (double)min_iterations < 1.2);

    printf("PASS test_spin\n");
    return 0;
}
