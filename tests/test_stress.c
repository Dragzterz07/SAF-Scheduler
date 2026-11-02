#include "preempt.h"
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

#define STOP_TICKS 2000
#define THREAD_COUNT 6

typedef enum {
    WORK_YIELD,
    WORK_SPIN,
    WORK_ALLOCATE,
    WORK_PRINT
} work_kind_t;

typedef struct {
    int id;
    work_kind_t kind;
    long iterations;
} worker_data_t;

static worker_data_t workers[THREAD_COUNT];

static void stress_worker(void *arg) {
    worker_data_t *worker = arg;
    size_t allocation_size = (size_t)worker->id * 37;

    while (timer_ticks() < STOP_TICKS) {
        worker->iterations++;
        switch (worker->kind) {
        case WORK_YIELD:
            thread_yield();
            break;
        case WORK_SPIN:
            break;
        case WORK_ALLOCATE: {
            void *memory = saf_malloc(allocation_size);
            CHECK(memory != NULL);
            saf_free(memory);
            allocation_size = allocation_size % 2048 + 1;
            break;
        }
        case WORK_PRINT:
            saf_printf("stress writer %d iteration %ld\n",
                       worker->id, worker->iterations);
            break;
        }
    }
}

int main(void) {
    CHECK(setenv("SAF_TICK_US", "100", 1) == 0);
    CHECK(freopen("/dev/null", "w", stdout) != NULL);

    for (int i = 0; i < THREAD_COUNT; i++) {
        workers[i].id = i + 1;
        workers[i].kind = (work_kind_t)(i % 4);
        workers[i].iterations = 0;
        CHECK(thread_create(stress_worker, &workers[i]) > 0);
    }

    CHECK(scheduler_run() == 0);
    CHECK(timer_ticks() >= STOP_TICKS);

    for (int i = 0; i < THREAD_COUNT; i++) {
        CHECK(workers[i].iterations > 0);
    }

    fprintf(stderr, "PASS test_stress\n");
    return 0;
}
