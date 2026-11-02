#include "runqueue.h"
#include "sched_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/* assert() disappears under -DNDEBUG, which would silently skip every
 * check below (and trip -Wunused-variable on the now-dead locals).
 * CHECK() always evaluates and always aborts loudly on failure. */
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

int main(void) {
    TCB dummies[16];
    TCB *tp[16];
    for (int i = 0; i < 16; i++) {
        tp[i] = &dummies[i];
    }

    /* rq_init(0) -> -EINVAL */
    CHECK(rq_init(0) == -EINVAL);

    /* basic FIFO, empty/full edge cases */
    CHECK(rq_init(4) == 0);
    CHECK(rq_dequeue() == NULL);
    CHECK(rq_enqueue(NULL) == -EINVAL);
    CHECK(rq_enqueue(tp[0]) == 0);
    CHECK(rq_enqueue(tp[1]) == 0);
    CHECK(rq_enqueue(tp[2]) == 0);
    CHECK(rq_size() == 3);
    CHECK(rq_enqueue(tp[3]) == 0);
    CHECK(rq_size() == 4);
    CHECK(rq_enqueue(tp[4]) == -EAGAIN);
    CHECK(rq_dequeue() == tp[0]);
    CHECK(rq_dequeue() == tp[1]);
    CHECK(rq_dequeue() == tp[2]);
    CHECK(rq_dequeue() == tp[3]);
    CHECK(rq_dequeue() == NULL);
    CHECK(rq_size() == 0);

    /* rq_destroy then rq_init works */
    rq_destroy();
    CHECK(rq_init(2) == 0);
    CHECK(rq_enqueue(tp[0]) == 0);
    rq_destroy();

    /* 10,000 enqueue/dequeue cycles across the wrap boundary keep order */
    CHECK(rq_init(8) == 0);
    for (int i = 0; i < 10000; i++) {
        int a = i % 16;
        int b = (i + 1) % 16;
        CHECK(rq_enqueue(tp[a]) == 0);
        CHECK(rq_enqueue(tp[b]) == 0);
        CHECK(rq_dequeue() == tp[a]);
        CHECK(rq_dequeue() == tp[b]);
    }
    CHECK(rq_size() == 0);
    rq_destroy();

    printf("PASS test_runqueue\n");
    return 0;
}
