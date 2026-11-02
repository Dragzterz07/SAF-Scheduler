#ifndef PREEMPT_INTERNAL_H
#define PREEMPT_INTERNAL_H

#include <signal.h>

/* Shared with the signal handler in tcb.c (I7): preempt_count tracks
 * nesting of preempt_disable/preempt_enable; preempt_pending is set by
 * the handler when a tick arrives while preempt_count > 0. */
extern volatile sig_atomic_t preempt_count;
extern volatile sig_atomic_t preempt_pending;

#endif
