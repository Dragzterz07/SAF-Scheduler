#ifndef PREEMPT_INTERNAL_H
#define PREEMPT_INTERNAL_H

#include <signal.h>

/* S3 CHANGE: _Thread_local so each pthread worker has its own independent
 * preemption-disable nesting count and pending flag. */

/* Shared with the signal handler in tcb.c (I7): preempt_count tracks
 * nesting of preempt_disable/preempt_enable; preempt_pending is set by
 * the handler when a tick arrives while preempt_count > 0. */
extern _Thread_local volatile sig_atomic_t preempt_count;
extern _Thread_local volatile sig_atomic_t preempt_pending;

#endif
