#include "timer.h"
#include "sched_internal.h"

#include <errno.h>
#include <signal.h>
#include <sys/time.h>

static volatile sig_atomic_t g_ticks = 0;
static struct sigaction prev_action;
static int armed = 0;

static void on_tick(int sig, siginfo_t *si, void *uc) {
    (void)sig;
    (void)si;
    (void)uc;
    g_ticks++;
    sched_on_tick();
}

int timer_arm(long usec) {
    if (usec < 100) {
        return -EINVAL;
    }

    struct sigaction sa;
    sa.sa_sigaction = on_tick;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGVTALRM);

    if (sigaction(SIGVTALRM, &sa, &prev_action) == -1) {
        return -errno;
    }

    struct itimerval it;
    it.it_value.tv_sec = usec / 1000000;
    it.it_value.tv_usec = usec % 1000000;
    it.it_interval = it.it_value;

    if (setitimer(ITIMER_VIRTUAL, &it, NULL) == -1) {
        int err = errno;
        sigaction(SIGVTALRM, &prev_action, NULL);
        return -err;
    }

    armed = 1;
    return 0;
}

void timer_disarm(void) {
    if (!armed) {
        return;
    }

    struct itimerval it;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 0;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_VIRTUAL, &it, NULL);

    sigaction(SIGVTALRM, &prev_action, NULL);
    armed = 0;
}

long timer_ticks(void) {
    return (long)g_ticks;
}
