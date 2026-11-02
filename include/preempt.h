#ifndef PREEMPT_H
#define PREEMPT_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Increment the preempt-disable count. While count > 0, a timer tick is
 * deferred instead of switching threads (I7). */
void preempt_disable(void);

/* Decrement the preempt-disable count. If it reaches 0 and a preemption
 * was deferred, yields immediately (I7). */
void preempt_enable(void);

/* Block SIGVTALRM on the calling thread/process via sigprocmask. */
void sig_block_timer(void);

/* Unblock SIGVTALRM on the calling thread/process via sigprocmask. */
void sig_unblock_timer(void);

/* Guarded printf; returns vprintf's result, or -EINVAL if fmt is NULL. */
int saf_printf(const char *fmt, ...);

/* malloc/calloc/realloc/free wrapped in the preempt guard;
 * safe to call under preemption (I7). */
void *saf_malloc(size_t n);
void *saf_calloc(size_t n, size_t sz);
void *saf_realloc(void *p, size_t n);
void saf_free(void *p);

#ifdef __cplusplus
}
#endif

#endif
