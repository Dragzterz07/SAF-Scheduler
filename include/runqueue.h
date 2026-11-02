#ifndef RUNQUEUE_H
#define RUNQUEUE_H

#include <stddef.h>
#include "tcb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ring-buffer run queue of READY threads. Callers must hold SIGVTALRM
 * blocked (I2) before calling any function below. */

/* Allocate the queue with capacity `cap`. Returns 0 on success,
 * -EINVAL if cap == 0, -ENOMEM on allocation failure,
 * -EALREADY if already initialised. */
int rq_init(size_t cap);

/* Free the queue and reset all fields. Idempotent. */
void rq_destroy(void);

/* Enqueue a thread. Returns 0 on success, -EAGAIN if full, -EINVAL if t is NULL. */
int rq_enqueue(TCB *t);

/* Dequeue the next thread. Returns NULL if empty. */
TCB *rq_dequeue(void);

/* Number of threads currently queued. */
size_t rq_size(void);

/* Largest number of threads queued since the most recent rq_init(). */
size_t rq_max_size(void);

#ifdef __cplusplus
}
#endif

#endif
