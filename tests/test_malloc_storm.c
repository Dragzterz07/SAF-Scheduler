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

#define THREAD_COUNT 3
#define STOP_TICKS 300

typedef struct {
    int id;
    long allocations;
} worker_data_t;

static worker_data_t workers[THREAD_COUNT];

static void allocator_worker(void *arg) {
    worker_data_t *worker = arg;
    size_t size = 1;

    while (timer_ticks() < STOP_TICKS) {
        unsigned char *memory = saf_malloc(size);
        CHECK(memory != NULL);
        memory[0] = (unsigned char)worker->id;
        memory[size - 1] = (unsigned char)(worker->id + 1);
        saf_free(memory);
        worker->allocations++;

        size = size % 4096 + 1;
    }
}

int main(void) {
    for (int i = 0; i < THREAD_COUNT; i++) {
        workers[i].id = i + 1;
        workers[i].allocations = 0;
        CHECK(thread_create(allocator_worker, &workers[i]) > 0);
    }

    CHECK(scheduler_run() == 0);

    for (int i = 0; i < THREAD_COUNT; i++) {
        CHECK(workers[i].allocations > 0);
    }

    printf("PASS test_malloc_storm\n");
    return 0;
}
