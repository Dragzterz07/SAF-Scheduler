#include "preempt.h"
#include "preempt_internal.h"
#include "tcb.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* assert() disappears under -DNDEBUG, which would silently skip every
 * check below (and trip -Wunused-variable on the now-dead locals).
 * CHECK() always evaluates and always aborts loudly on failure. */
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

static int g_t1_entered = 0;
static int g_t1_yielded = 0;
static int g_t2_ran = 0;

static void worker_t2(void *arg) {
    (void)arg;
    /* Thread 2 should only run after Thread 1 yields */
    CHECK(g_t1_yielded == 1);
    g_t2_ran = 1;
}

static void worker_t1(void *arg) {
    (void)arg;
    g_t1_entered = 1;

    /* Nesting test: disable twice */
    preempt_disable();
    CHECK(preempt_count == 1);

    preempt_disable();
    CHECK(preempt_count == 2);

    /* Simulate a timer tick arriving while disabled */
    preempt_pending = 1;

    /* First enable: count drops to 1, should NOT yield even with pending=1 */
    preempt_enable();
    CHECK(preempt_count == 1);
    CHECK(preempt_pending == 1);
    CHECK(g_t2_ran == 0);

    /* Second enable: count drops to 0, pending=1 triggers thread_yield() */
    g_t1_yielded = 1;
    preempt_enable();

    /* When we return, Thread 2 should have executed */
    CHECK(preempt_count == 0);
    CHECK(preempt_pending == 0);
    CHECK(g_t2_ran == 1);
}

static void test_wrappers(void) {
    CHECK(saf_printf(NULL) == -EINVAL);

    /* 1. saf_free(NULL) must be safe */
    saf_free(NULL);

    /* 2. saf_malloc */
    int *nums = (int *)saf_malloc(5 * sizeof(int));
    CHECK(nums != NULL);
    for (int i = 0; i < 5; i++) {
        nums[i] = i * 10;
    }

    /* 3. saf_realloc */
    nums = (int *)saf_realloc(nums, 10 * sizeof(int));
    CHECK(nums != NULL);
    for (int i = 0; i < 5; i++) {
        CHECK(nums[i] == i * 10);
    }
    for (int i = 5; i < 10; i++) {
        nums[i] = i * 10;
    }
    for (int i = 0; i < 10; i++) {
        CHECK(nums[i] == i * 10);
    }
    saf_free(nums);

    /* 4. saf_calloc (memory must be zeroed) */
    char *buf = (char *)saf_calloc(64, sizeof(char));
    CHECK(buf != NULL);
    for (int i = 0; i < 64; i++) {
        CHECK(buf[i] == 0);
    }
    saf_free(buf);

    /* 5. saf_printf */
    int written = saf_printf("Testing saf_printf: %d %s\n", 42, "ok");
    CHECK(written > 0);
}

int main(void) {
    /* Turn off timer ticks for deterministic unit testing */
    setenv("SAF_PREEMPT", "0", 1);

    test_wrappers();

    /* Test nesting and deferred preemption with two threads */
    int rc1 = thread_create(worker_t1, NULL);
    CHECK(rc1 > 0);
    int rc2 = thread_create(worker_t2, NULL);
    CHECK(rc2 > 0);

    int rc = scheduler_run();
    CHECK(rc == 0);

    CHECK(g_t1_entered == 1);
    CHECK(g_t1_yielded == 1);
    CHECK(g_t2_ran == 1);
    CHECK(preempt_count == 0);
    CHECK(preempt_pending == 0);

    printf("PASS test_preempt_guard\n");
    return 0;
}
