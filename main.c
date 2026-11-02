#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "preempt.h"
#include "tcb.h"
#include "timer.h"

static int g_infinite = 0;
static int g_max_iterations = 4;

static void worker(void *arg) {
    int id = *(int *)arg;
    int count = 0;
    while (g_infinite || count < g_max_iterations) {
        count++;
        saf_printf("Thread %d (ID: %d) - iteration %d\n", id, id, count);
        thread_yield();
    }
}

typedef struct {
    int id;
    long count;
} spin_ctx_t;

static void spin_worker(void *arg) {
    spin_ctx_t *ctx = (spin_ctx_t *)arg;
    while (timer_ticks() < 300) {
        ctx->count++;
    }
    saf_printf("Thread %d spun %ld iterations\n", ctx->id, ctx->count);
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-i|--infinite] [iterations]\n", prog);
    fprintf(stderr, "       %s --spin N\n", prog);
}

static void print_stats(void) {
    saf_stats_t stats;
    saf_get_stats(&stats);
    saf_printf("[Stats] switches=%ld preemptions=%ld deferred=%ld ticks=%ld max_queue_len=%ld\n",
               stats.switches, stats.preemptions, stats.deferred,
               stats.ticks, stats.max_queue_len);
}

static int run_spin(int n) {
    spin_ctx_t *ctxs = calloc((size_t)n, sizeof(spin_ctx_t));
    if (ctxs == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    for (int i = 0; i < n; i++) {
        ctxs[i].id = i + 1;
        ctxs[i].count = 0;
        int rc = thread_create(spin_worker, &ctxs[i]);
        if (rc < 0) {
            fprintf(stderr, "thread_create failed: %d\n", rc);
            free(ctxs);
            return 1;
        }
    }

    int rc = scheduler_run();
    free(ctxs);
    if (rc < 0) {
        fprintf(stderr, "scheduler_run failed: %d\n", rc);
        return 1;
    }
    print_stats();
    return 0;
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--spin") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            char *end = NULL;
            long n = strtol(argv[i + 1], &end, 10);
            if (end == argv[i + 1] || *end != '\0' || n <= 0) {
                usage(argv[0]);
                return 1;
            }
            return run_spin((int)n);
        }

        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--infinite") == 0) {
            g_infinite = 1;
            continue;
        }

        char *end = NULL;
        long val = strtol(argv[i], &end, 10);
        if (end == argv[i] || *end != '\0' || val <= 0) {
            usage(argv[0]);
            return 1;
        }
        g_max_iterations = (int)val;
    }

    printf("==================================================\n");
    printf("   SAF-Scheduler: Stage 1 Cooperative Test        \n");
    printf("==================================================\n");
    if (g_infinite) {
        printf("[Config] Running indefinitely (Ctrl+C to stop)\n");
    } else {
        printf("[Config] Running %d iterations per thread\n", g_max_iterations);
    }

    int ids[3] = {1, 2, 3};

    printf("[Init] Registering 3 cooperative user threads...\n");
    for (int i = 0; i < 3; i++) {
        int rc = thread_create(worker, &ids[i]);
        if (rc < 0) {
            fprintf(stderr, "thread_create failed: %d\n", rc);
            return 1;
        }
    }

    printf("[Run] Starting scheduler...\n");
    int rc = scheduler_run();
    if (rc < 0) {
        fprintf(stderr, "scheduler_run failed: %d\n", rc);
        return 1;
    }

    print_stats();
    printf("[Done] All user threads completed successfully.\n");
    printf("[Cleanup] Memory cleanup complete. Stage 1 milestone passed!\n");

    return 0;
}
