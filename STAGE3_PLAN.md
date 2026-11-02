# SAF-Scheduler — Stage 3 Plan: The M:N Engine & Work-Stealing

> **Your role:** You are building Stage 3 on top of the completed Stage 2 codebase.
> All 8 Stage 2 tests pass cleanly. The codebase is already structured for Stage 3 —
> `STAGE2_PLAN_FOR_CLAUDE.md §9` documents the exact "seams" left open for you.

---

## What Stage 3 Must Deliver

**Goal (from `tasks.md`):** Full M:N scheduling — M kernel `pthread` worker threads each running a
scheduling loop over N user green threads, distributed via per-worker **work-stealing deques**.
Idle cores steal tasks from overloaded cores.

**Milestone:** A workload where thread 1 creates 50 tasks and threads 2 & 3 are idle proves that
idle workers steal and balance execution. All cores actively busy with near-equal work.

---

## The 4 Core Changes (from the Stage 2 seams, STAGE2_PLAN_FOR_CLAUDE.md §9)

```
sigprocmask       → pthread_sigmask        (only in sig_block_timer / sig_unblock_timer)
setitimer         → per-worker timer_create with SIGEV_THREAD_ID   (only in timer.c)
current, sched_ctx,
preempt_count,
preempt_pending,
counters          → _Thread_local
single run queue  → per-worker deques + work-stealing steal()
```

> Rule I4 — "re-enqueue only after the switch back to the scheduler" — must stay intact.
> This is what makes stealing safe. A READY thread is only in one deque at a time.

---

## Step-by-Step Breakdown

### Step 1 — Make All Scheduler State Thread-Local
Files: src/sched_internal.h, src/tcb.c, src/preempt.c, src/preempt_internal.h

| Current global | Becomes |
|---|---|
| TCB *current | _Thread_local TCB *current |
| ucontext_t sched_ctx | _Thread_local ucontext_t sched_ctx |
| volatile sig_atomic_t preempt_count | _Thread_local volatile sig_atomic_t preempt_count |
| volatile sig_atomic_t preempt_pending | _Thread_local volatile sig_atomic_t preempt_pending |
| long g_switches | per-worker, aggregated in saf_get_stats() |

After this step, run `make test` — all 8 Stage 2 tests must still pass.

---

### Step 2 — Build the Per-Worker Work-Stealing Deque
New files: include/deque.h, src/deque.c

Chase-Lev work-stealing deque layout:

    Worker's OWN deque:
    [ ... | T | T | T | ... ]
      ^                   ^
     top                bottom
    (remote steal,     (local push/pop,
     CAS needed)        no lock needed)

Public interface (include/deque.h):
```c
typedef struct WorkDeque WorkDeque;
WorkDeque *deque_create(size_t cap);
void       deque_destroy(WorkDeque *d);
int        deque_push(WorkDeque *d, TCB *t);    /* local worker only */
TCB       *deque_pop(WorkDeque *d);             /* local worker only */
TCB       *deque_steal(WorkDeque *d);           /* remote, thread-safe CAS */
size_t     deque_size(WorkDeque *d);
```

Use C11 <stdatomic.h> — atomic_size_t for top and bottom indices.
When deque has exactly 1 element, pop_bottom and steal_top race — resolve with
atomic_compare_exchange_strong on the top index.

---

### Step 3 — POSIX Worker Thread Pool
File: src/tcb.c

In scheduler_run(), instead of running sched_loop() directly:

1. Read M from SAF_WORKERS env var (default: sysconf(_SC_NPROCESSORS_ONLN)).
2. Allocate M per-worker deques.
3. Distribute initial threads round-robin: deque_push(deques[i % M], tcb).
4. Spawn M-1 pthreads; each runs its own TLS sched_loop.
5. Pin each worker to a core with pthread_setaffinity_np.
6. Calling thread becomes worker 0, also runs sched_loop.
7. pthread_join all workers on completion.

---

### Step 4 — Per-Worker Timer (setitimer → timer_create)
File: src/timer.c

setitimer(ITIMER_VIRTUAL) is process-wide, delivered to an arbitrary thread —
unusable in multi-threaded scheduling.

Replace with timer_create(CLOCK_THREAD_CPUTIME_ID) with SIGEV_THREAD_ID so
SIGVTALRM is delivered only to the specific worker thread.

Also: replace sigprocmask with pthread_sigmask in sig_block_timer/sig_unblock_timer.

---

### Step 5 — Work-Stealing Scheduler Loop
File: src/tcb.c (sched_loop)

Pseudocode:
    while (!termination_condition()) {
        t = deque_pop(my_deque);          // try own deque first
        if (!t) t = try_steal();          // scan other workers
        if (!t) { mark_idle(); pause(); continue; }

        current = t; t->state = SAF_RUNNING;
        swapcontext(&sched_ctx, &t->ctx); // TLS sched_ctx
        current = NULL;

        if (t->state == SAF_READY)    deque_push(my_deque, t);  // I4: re-enqueue here only
        else if (t->state == SAF_FINISHED) tcb_destroy(t);
    }

Termination: all workers idle AND all deques empty AND no thread RUNNING.
Use atomic_int busy-worker counter.

---

### Step 6 — Tests + TSan
New tests:
- tests/test_mn_basic.c    — 2 workers, 6 threads, verify all complete
- tests/test_stealing.c    — worker 0 gets 20 tasks, worker 1 idle → verify stealing occurs
- tests/test_mn_stress.c   — 4 workers, 100 threads, 2000 ticks, no crash, no races

ThreadSanitizer check (separate from Valgrind):
    make CFLAGS="-fsanitize=thread -g" test

Add `make tsan` target to Makefile for this.

---

## File Ownership Map

| File | Action |
|---|---|
| include/deque.h | NEW |
| src/deque.c | NEW |
| src/sched_internal.h | MODIFY — TLS annotations, worker-pool state |
| src/tcb.c | MODIFY — worker pool, TLS sched_ctx, work-stealing sched_loop |
| src/timer.c | MODIFY — timer_create(SIGEV_THREAD_ID), pthread_sigmask |
| src/preempt.c | MODIFY — pthread_sigmask |
| src/preempt_internal.h | MODIFY — _Thread_local |
| tests/test_mn_*.c | NEW |
| Makefile | MODIFY — add tsan target, pick up deque.c |
| STAGE3.md | NEW — architecture doc |

---

## Suggested Order of Work

Step 1 (TLS) → Step 2 (deque + unit test) → Step 3 (worker pool) → Step 4 (per-worker timer) → Step 5 (work-stealing loop) → Step 6 (tests + TSan)

Run `make test` after each step to confirm Stage 2 tests still pass.

---

## Key Invariants to Preserve

| # | Rule | Why in Stage 3 |
|---|---|---|
| I1 | Hub-and-spoke via sched_ctx | Each worker has its own TLS sched_ctx |
| I2 | Scheduler runs with SIGVTALRM blocked | pthread_sigmask must block it per-worker |
| I4 | Re-enqueue only after switch-back | Makes stealing safe — TCB in one deque at a time |
| I5 | Only scheduler frees a thread | Local worker that last ran the thread frees it |
| I6 | Signal handler: no malloc/stdio/locks | TLS access is safe in a handler |
| I7 | Deferred preemption guard | Malloc still not reentrant |

---

## Viva Questions to Prepare

1. Why does Chase-Lev use CAS only at the top (steal side) and not the bottom (local side)?
2. What happens if two workers simultaneously try to steal the last element from the same deque?
3. Why is setitimer(ITIMER_VIRTUAL) insufficient for M:N but timer_create(SIGEV_THREAD_ID) works?
4. Why must _Thread_local be used for sched_ctx rather than a global array indexed by worker ID?
5. How does the termination condition avoid a race where a worker exits early before all work is stolen?
