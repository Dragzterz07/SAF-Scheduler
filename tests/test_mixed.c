#include "preempt.h"
#include "tcb.h"
#include "timer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define DEFAULT_STOP_TICKS 500

typedef struct {
    int id;
    long iterations;
} worker_data_t;

static worker_data_t yielders[2];
static worker_data_t spinners[2];
static worker_data_t writers[2];
static long stop_ticks = DEFAULT_STOP_TICKS;

static void yielder(void *arg) {
    worker_data_t *worker = arg;
    while (timer_ticks() < stop_ticks) {
        worker->iterations++;
        thread_yield();
    }
}

static void spinner(void *arg) {
    worker_data_t *worker = arg;
    while (timer_ticks() < stop_ticks) {
        worker->iterations++;
    }
}

static void writer(void *arg) {
    worker_data_t *worker = arg;
    while (timer_ticks() < stop_ticks) {
        worker->iterations++;
        saf_printf("writer %d iteration %ld\n", worker->id, worker->iterations);

        /* Hold the guard across a tick to exercise deferred preemption. */
        preempt_disable();
        long tick = timer_ticks();
        while (timer_ticks() == tick) {
            worker->iterations++;
        }
        preempt_enable();
    }
}

static long read_stop_ticks(void) {
    const char *value = getenv("SAF_MIXED_TICKS");
    if (value == NULL) {
        return DEFAULT_STOP_TICKS;
    }

    char *end = NULL;
    errno = 0;
    long ticks = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || ticks <= 0) {
        return -1;
    }
    return ticks;
}

int main(void) {
    stop_ticks = read_stop_ticks();
    CHECK(stop_ticks > 0);

    CHECK(freopen("/dev/null", "w", stdout) != NULL);

    for (int i = 0; i < 2; i++) {
        yielders[i].id = i + 1;
        yielders[i].iterations = 0;
        spinners[i].id = i + 1;
        spinners[i].iterations = 0;
        writers[i].id = i + 1;
        writers[i].iterations = 0;
        CHECK(thread_create(yielder, &yielders[i]) > 0);
        CHECK(thread_create(spinner, &spinners[i]) > 0);
        CHECK(thread_create(writer, &writers[i]) > 0);
    }

    CHECK(scheduler_run() == 0);

    for (int i = 0; i < 2; i++) {
        CHECK(yielders[i].iterations > 0);
        CHECK(spinners[i].iterations > 0);
        CHECK(writers[i].iterations > 0);
    }

    saf_stats_t stats;
    saf_get_stats(&stats);
    CHECK(stats.deferred > 0);

    fprintf(stderr, "PASS test_mixed\n");
    return 0;
}
