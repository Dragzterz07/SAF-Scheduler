#ifndef TCB_H
#define TCB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TCB TCB;
typedef enum { SAF_READY, SAF_RUNNING, SAF_FINISHED } saf_state_t;

int  thread_create(void (*fn)(void *), void *arg);  /* >0 thread id, <0 error */
void thread_yield(void);
void thread_exit(void);                              /* does not return */
int  scheduler_run(void);                            /* 0 when all threads finished, <0 error */

/* Scheduler counters are cumulative; max_queue_len is the lifetime high-water mark. */
typedef struct { long switches, preemptions, deferred, ticks, max_queue_len; } saf_stats_t;
void saf_get_stats(saf_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
