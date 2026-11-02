#ifndef TIMER_H
#define TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Arm SIGVTALRM via setitimer(ITIMER_VIRTUAL) to fire every `usec`
 * microseconds. Returns 0 on success, -EINVAL if usec < 100, -errno on
 * sigaction/setitimer failure (sigaction is undone if setitimer fails). */
int timer_arm(long usec);

/* Disarm the timer and restore the previous SIGVTALRM action. Idempotent. */
void timer_disarm(void);

/* Number of ticks observed so far. */
long timer_ticks(void);

#ifdef __cplusplus
}
#endif

#endif
