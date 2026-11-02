#include "preempt.h"
#include "preempt_internal.h"
#include "tcb.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

volatile sig_atomic_t preempt_count = 0;
volatile sig_atomic_t preempt_pending = 0;

static sigset_t vtalrm_set;
static int vtalrm_set_ready = 0;

static void ensure_vtalrm_set(void) {
    if (!vtalrm_set_ready) {
        sigemptyset(&vtalrm_set);
        sigaddset(&vtalrm_set, SIGVTALRM);
        vtalrm_set_ready = 1;
    }
}

void sig_block_timer(void) {
    ensure_vtalrm_set();
    sigprocmask(SIG_BLOCK, &vtalrm_set, NULL);
}

void sig_unblock_timer(void) {
    ensure_vtalrm_set();
    sigprocmask(SIG_UNBLOCK, &vtalrm_set, NULL);
}

void preempt_disable(void) {
    preempt_count++;
}

void preempt_enable(void) {
    assert(preempt_count > 0);
    preempt_count--;
    if (preempt_count == 0 && preempt_pending) {
        preempt_pending = 0;
        thread_yield();
    }
}

int saf_printf(const char *fmt, ...) {
    if (fmt == NULL) {
        return -EINVAL;
    }
    preempt_disable();
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    preempt_enable();
    return ret;
}

void *saf_malloc(size_t n) {
    preempt_disable();
    void *p = malloc(n);
    preempt_enable();
    return p;
}

void *saf_calloc(size_t n, size_t sz) {
    preempt_disable();
    void *p = calloc(n, sz);
    preempt_enable();
    return p;
}

void *saf_realloc(void *p, size_t n) {
    preempt_disable();
    void *res = realloc(p, n);
    preempt_enable();
    return res;
}

void saf_free(void *p) {
    preempt_disable();
    free(p);
    preempt_enable();
}
