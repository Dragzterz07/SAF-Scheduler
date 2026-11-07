# SAF-Scheduler — Stage 3: The M:N Engine & Work-Stealing

Stage 3 upgrades the scheduler from a **1:N** model (single kernel thread running multiple green threads) to a true **M:N** model ($M$ POSIX kernel threads running $N$ green threads in parallel across CPU cores).

---

## Architecture Overview

```text
                                  scheduler_run()
                                         │
                 ┌───────────────────────┴───────────────────────┐
                 ▼ Spawns M-1 pthreads                           ▼ Becomes Worker 0
         Worker 1 (Core 1)               Worker 2 (Core 2)       Worker 0 (Core 0)
   ┌───────────────────────────┐   ┌───────────────────────────┐   ┌───────────────────────────┐
   │ TLS: sched_ctx, current   │   │ TLS: sched_ctx, current   │   │ TLS: sched_ctx, current   │
   │ Per-thread timer (SIGVT)  │   │ Per-thread timer (SIGVT)  │   │ Per-thread timer (SIGVT)  │
   │ Deque 1 (WSDeque)         │   │ Deque 2 (WSDeque)         │   │ Deque 0 (WSDeque)         │
   │ sched_loop()              │   │ sched_loop()              │   │ sched_loop()              │
   └─────────────┬─────────────┘   └─────────────┬─────────────┘   └─────────────┬─────────────┘
                 │                               │                               │
                 └─────────────── steals from ───┴─────── steals from ───────────┘
                                                 │
                                                 ▼
                                     pthread_join() on completion
```

---

## Key Modules & Design

### 1. Worker Module (`include/worker.h`, `src/worker.c`)
- **`worker_t` Struct:** Encapsulates the worker ID, core assignment, `pthread_t` handle, and scheduling function.
- **CPU Core Pinning (`pthread_setaffinity_np`):** Pins Worker $i$ to core $i \pmod{\text{num\_cores}}$ to avoid thread migration and maximize L1/L2 cache locality.
- **Worker Configuration:** Reads `SAF_WORKERS` from the environment or `--workers N` CLI flag; defaults to 1 for backwards compatibility with Stage 2 single-worker tests.

### 2. Work-Stealing Deque (`include/deque.h`, `src/deque.c`)
- **Chase-Lev Lock-Free Deque:**
  - Owner pushes and pops from the **bottom (tail)** of its own deque.
  - Remote idle workers steal from the **top (front)** of peer deques.
- **CAS Race Resolution:** When only 1 element remains, `atomic_compare_exchange_strong` on `front` arbitrates between the owner's `pop` and a thief's `steal`, ensuring exactly one wins.

### 3. Per-Thread Timers & Signals (`src/timer.c`, `src/preempt.c`)
- **`timer_create(CLOCK_THREAD_CPUTIME_ID)` with `SIGEV_THREAD_ID`:** Replaces the process-wide `setitimer(ITIMER_VIRTUAL)` so timer ticks target the specific worker thread using its kernel TID (`SYS_gettid`).
- **`pthread_sigmask`:** Replaces `sigprocmask()` for POSIX-compliant, thread-safe signal masking scoped strictly to the calling kernel worker.

### 4. Thread-Local Storage (`_Thread_local`)
- `current`, `sched_ctx`, `preempt_count`, `preempt_pending`, and local switch/preemption counters are declared `_Thread_local`.
- Each worker executes its own hub context without interfering with concurrent workers.

---

## Test Suites

| Test File | Workload | Pass Condition |
|---|---|---|
| `test_deque.c` | LIFO pop, FIFO steal, capacity limits, concurrent CAS race | Zero errors, exactly one winner on single-element race |
| `test_mn_basic.c` | 2 workers, 30 tasks yielding across workers | All tasks complete successfully |
| `test_stealing.c` | Worker 0 overloaded with tasks, peers idle | Active work-stealing relieves worker 0 |
| `test_mn_stress.c` | 4 workers, 60 threads (spinners + yielders), 1000µs timer preemption | All complete clean with zero data races or crashes |

---

## Build and Run

```bash
# Compile and run the complete test suite (12 tests)
make clean && make test

# Run scheduler in M:N mode with 4 workers
./scheduler --workers 4

# Run spin test with preemption across 4 workers
SAF_WORKERS=4 ./scheduler --spin 4

# Run with ThreadSanitizer (requires libtsan installed)
make tsan
```
