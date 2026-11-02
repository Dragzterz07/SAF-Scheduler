#include "runqueue.h"

#include <errno.h>
#include <stdlib.h>

/* Static ring buffer. No locking, no signal handling: callers hold
 * SIGVTALRM blocked per the header's documented requirement (I2). */
static TCB **rq_buf = NULL;
static size_t rq_cap = 0;
static size_t rq_head = 0;
static size_t rq_tail = 0;
static size_t rq_count = 0;
static size_t rq_max_len = 0;

int rq_init(size_t cap) {
    if (cap == 0) {
        return -EINVAL;
    }
    if (rq_buf != NULL) {
        return -EALREADY;
    }
    rq_buf = calloc(cap, sizeof(TCB *));
    if (rq_buf == NULL) {
        return -ENOMEM;
    }
    rq_cap = cap;
    rq_head = 0;
    rq_tail = 0;
    rq_count = 0;
    rq_max_len = 0;
    return 0;
}

void rq_destroy(void) {
    free(rq_buf);
    rq_buf = NULL;
    rq_cap = 0;
    rq_head = 0;
    rq_tail = 0;
    rq_count = 0;
    rq_max_len = 0;
}

int rq_enqueue(TCB *t) {
    if (t == NULL) {
        return -EINVAL;
    }
    if (rq_count == rq_cap) {
        return -EAGAIN;
    }
    rq_buf[rq_tail] = t;
    rq_tail = (rq_tail + 1) % rq_cap;
    rq_count++;
    if (rq_count > rq_max_len) {
        rq_max_len = rq_count;
    }
    return 0;
}

TCB *rq_dequeue(void) {
    if (rq_count == 0) {
        return NULL;
    }
    TCB *t = rq_buf[rq_head];
    rq_head = (rq_head + 1) % rq_cap;
    rq_count--;
    return t;
}

size_t rq_size(void) {
    return rq_count;
}

size_t rq_max_size(void) {
    return rq_max_len;
}
