# SAF-Scheduler — Stage 2 Implementation Plan (Instructions for Claude)

> This file is written for an AI coding assistant (Claude). It is the single source of truth for
> Stage 2. Read it fully before touching code. A simpler human-facing version exists in
> `STAGE2_QUICKSTART.md`; if the two ever disagree, **this file wins** — flag the disagreement.

---

## 0. Operating rules for Claude

1. **One task at a time.** Work only on the task the developer names (e.g. "do task 03"). Do not
   start the next task until the current one meets its acceptance criteria.
2. **Stay on the task's branch.** Never commit to `main` or `stage2`. If you are on the wrong
   branch, stop and say so.
3. **Only touch the files listed for the task.** If another file must change, stop, state which
   file and why, and wait for approval.
4. **Respect the invariants in §4.** If a request would break one, refuse that part and explain
   which invariant it breaks.
5. **The team has a viva.** After finishing each task, give the developer a short explanation
   (≤ 1 page) of what changed and why each important line exists, plus 3 likely viva questions
   with answers. The developers must be able to explain every line; prefer clear code over clever
   code.
6. **Never silence a warning** to make `-Werror` pass (no casts-to-silence, no pragmas). Fix the
   cause.
7. **Verify, don't assume.** Every acceptance criterion has a command. Run it and report output.
8. **Ask before guessing** on anything not covered here (API names, behaviour changes).

Platform assumptions: Linux, x86-64, glibc ≥ 2.31, GCC, Valgrind 3.x. Single kernel thread
(1:N) in Stage 2.

---

## 1. Starting point (end of Stage 1)

Files: `main.c`, `tcb.h`, `tcb.cpp` (C++), `test_mem.sh`, `README.md`, `stage1.md`, `tasks.md`.

How Stage 1 works: a `std::vector<TCB*>` scanned round-robin; `thread_yield()` calls
`scheduler()` **on the yielding thread's own stack**, which calls `swapcontext` directly to the
next thread. `main_context` is saved on the first switch and restored with `setcontext` when no
thread is READY.

Known defects to remove in Stage 2 (do not reintroduce):

| ID | Defect | Fixed in task |
|----|--------|---------------|
| D1 | Running thread is labelled READY when it is the only READY thread (`previous == next` in `context_switch`) | 04 |
| D2 | No ring buffer; O(n) scan over a vector that keeps FINISHED threads | 02, 04 |
| D3 | C++ file in a C project; `std::vector` allocates (unsafe near signals) | 03 |
| D4 | Caller owns TCB memory (stack-allocated TCBs in `main`) | 03 |
| D5 | Finished threads' stacks freed only at shutdown | 04 |
| D6 | No stack guard page; overflow silently corrupts heap | 03 |
| D7 | Library calls `exit()` | 03 |
| D8 | Unprefixed enum names `READY/RUNNING/FINISHED` | 00 |
| D9 | `test_mem.sh` passes when Valgrind is missing | 01 |
| D10 | Valgrind `#include` in public header changes struct layout | 03 |
| D11 | Scheduler runs on user-thread stacks (fatal in Stage 3 M:N) | 04 |

---

## 2. Stage 2 goal and scope

**Milestone (from `tasks.md`):** a stable 1:N preemptive round-robin scheduler. Threads that
never call `thread_yield()` must still share the CPU, driven by `SIGVTALRM` from
`setitimer(ITIMER_VIRTUAL)` every ~10 ms.

**In scope:** C11 port, ring-buffer run queue, dedicated scheduler context, guard-paged stacks,
timer + signal handler, signal masking, deferred preemption for non-reentrant library calls,
automated tests, Valgrind-clean shutdown, basic counters.

**Out of scope (do not implement):** pthread worker pool, CPU pinning, TLS, work stealing,
deques, `timer_create`, telemetry/CSV. Keep APIs forward-compatible with them (see §9).

---

## 3. Verified facts (prototype run on Linux x86-64, glibc 2.39, GCC, Valgrind 3.22)

A throwaway prototype of the §4 design was built and tested before this plan was written.
Results — treat these as established; do not "fix" the design away from them:

| # | Fact | Evidence |
|---|------|----------|
| V1 | `-std=c11` alone hides POSIX/XSI declarations (`SA_RESTART`, `MAP_ANONYMOUS`, …) → build fails. Must compile with `-D_GNU_SOURCE`. | Compile error without it; clean build with it. |
| V2 | `#define _GNU_SOURCE` in a source file **plus** `-D_GNU_SOURCE` on the command line is a redefinition error under `-Werror`. Define it **only in the Makefile**. | Compile error reproduced. |
| V3 | Dedicated scheduler context + ring buffer gives exact cooperative order `123123123`. | Test output. |
| V4 | Timer preemption of 3 non-yielding spin threads: max/min progress ratio ≈ 1.01–1.03 over 300–500 ticks; also ≈ 1.016 under Valgrind. A threshold of 1.2 is safe. | 5 repeated runs + Valgrind run. |
| V5 | `mmap` stack with lowest page `mprotect(PROT_NONE)` turns overflow into an immediate SIGSEGV (exit 139). | Bounded deep-recursion test. |
| V6 | **Unguarded `malloc`/`free` under preemption crashes** (SIGSEGV 139 / SIGABRT 134) in every run, single- or multi-threaded process. It corrupts glibc's heap (lock-free per-thread cache), it does not merely deadlock. With the preempt guard (§4 I7) it runs clean. | 6 crashing runs without guard; clean runs with guard (≈ 100+ preemptions, ≈ 200+ deferred). |
| V7 | `saf_printf` with the guard under preemption: 28.7M lines, 0 malformed. | grep over output. |
| V8 | Valgrind: 0 errors, 0 bytes in use at exit for cooperative, preemptive spin and guarded malloc storm, when stacks are registered with `VALGRIND_STACK_REGISTER` and deregistered before `munmap`. `ITIMER_VIRTUAL` works under Valgrind. | Valgrind reports. |
| V9 | `ITIMER_VIRTUAL` cannot tick faster than the kernel tick: requesting 100 µs gave ≈ 4 ms/tick on the test machine. Stress tests must be written in **tick counts**, never wall time, and must not assume sub-ms ticks. | 500 ticks took 2.0 s at 100 µs requested. |
| V10 | `-Wextra -Werror` rejects obvious infinite recursion (`-Winfinite-recursion`) and misleading indentation. Overflow tests must use bounded recursion (depth counter). | Compile errors reproduced. |

Reasoned but not directly observed:
- **R1** glibc's `swapcontext`/`setcontext` apply the target's signal mask *before* loading its
  registers. If the target mask has `SIGVTALRM` unblocked, a signal can land while the CPU is still
  on the scheduler stack. Invariant I3 removes this window entirely; implement it even though no
  failure was observed.
- **R2** Calling `swapcontext` from a signal handler is not async-signal-safe per POSIX. It works on
  Linux/glibc (V4–V8) and is the standard technique for user-level preemption. Document it as a
  platform assumption in `STAGE2.md`.

---

## 4. Architecture and invariants

```
            main()
              │ scheduler_run()
              ▼
        ┌───────────┐  swapcontext   ┌──────────┐
        │ sched_ctx │ ─────────────► │ thread T │
        │ (own stack│ ◄───────────── │          │
        │  signal   │  yield / exit /│          │
        │  BLOCKED) │  SIGVTALRM     └──────────┘
        └───────────┘
   Threads NEVER switch directly to each other.
```

Invariants — every task must preserve all of them:

- **I1 Hub-and-spoke switching.** The only context switches are thread → `sched_ctx` and
  `sched_ctx` → thread. No thread-to-thread switches.
- **I2 Scheduler runs with `SIGVTALRM` blocked.** Any code that runs on the scheduler stack, or that
  reads/writes the run queue, `current`, or thread states, runs with `SIGVTALRM` blocked.
- **I3 Every context switched *into* has `SIGVTALRM` in its saved mask.**
  - New thread: `sigaddset(&t->ctx.uc_sigmask, SIGVTALRM)` after `getcontext`; the trampoline's
    first action is to unblock it.
  - Voluntary yield: `thread_yield` blocks before `swapcontext` and unblocks after it returns.
  - Preempted thread: the kernel blocks `SIGVTALRM` during the handler (via `sa_mask`), so the saved
    mask already has it; `sigreturn` restores the pre-signal mask when the handler returns.
- **I4 Run-queue contents = exactly the READY threads.** The RUNNING thread is never in the queue.
  Re-enqueueing happens **only in the scheduler loop, after the switch back has completed**.
- **I5 Only the scheduler frees a thread**, after switching off it (never on its own stack).
- **I6 The signal handler** only: increments counters of type `volatile sig_atomic_t`, reads
  `current`/state, sets `preempt_pending`, calls `swapcontext`. No stdio, malloc, locks, or
  `exit`.
- **I7 Deferred preemption.** User-visible non-reentrant calls (`printf` family, `malloc`,
  `calloc`, `realloc`, `free`) are wrapped in `preempt_disable()`/`preempt_enable()`. When the
  handler sees `preempt_count > 0` it sets `preempt_pending = 1` and returns; `preempt_enable()`
  yields when the count returns to 0 and a preemption is pending. The scheduler clears
  `preempt_pending` each time it switches a thread in.
- **I8 Library never calls `exit()` / `abort()`** outside `assert`. Errors are negative return
  values (`-errno` style) or `NULL`.
- **I9 `_GNU_SOURCE` is defined only in the Makefile** (V1, V2).
- **I10 Debug assertions** (`#ifndef NDEBUG`): at most one RUNNING thread; `current` is RUNNING
  whenever a thread runs; `rq_size()` == number of READY threads.

### Mechanism reference (validated by the prototype; adapt names to §5 API)

Signal handler:
```c
static void on_tick(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)si; (void)uc;
    g_ticks++;
    if (current == NULL || current->state != SAF_RUNNING) return;   /* on scheduler stack */
    if (preempt_count > 0) { preempt_pending = 1; g_deferred++; return; }
    g_preemptions++;
    current->state = SAF_READY;
    swapcontext(&current->ctx, &sched_ctx);      /* resumes here when rescheduled */
}
```

Scheduler loop (runs on `sched_ctx`, `SIGVTALRM` blocked):
```c
static void sched_loop(void) {
    TCB *t;
    while ((t = rq_dequeue()) != NULL) {
        current = t; t->state = SAF_RUNNING; preempt_pending = 0; g_switches++;
        swapcontext(&sched_ctx, &t->ctx);
        current = NULL;
        if (t->state == SAF_READY)         rq_enqueue(t);   /* I4: re-enqueue only here */
        else if (t->state == SAF_FINISHED) tcb_destroy(t);  /* I5 */
    }
}                                                            /* uc_link -> main_ctx */
```

`thread_yield`: block → `state = READY` → `swapcontext(&current->ctx, &sched_ctx)` → unblock.
`thread_exit`: block → `state = FINISHED` → `setcontext(&sched_ctx)`.
Trampoline (`void tramp(void)`, no arguments): unblock → `current->fn(current->arg)` →
`thread_exit()`.

Do **not** pass the user's `void *arg` through `makecontext`'s variadic `int` arguments (passing
pointers there is not portable). Store `fn`/`arg` in the TCB; the trampoline reads them via
`current`.

---

## 5. Target layout and public API

```
include/  tcb.h  runqueue.h  preempt.h  timer.h
src/      tcb.c  sched_internal.h  runqueue.c  preempt.c  preempt_internal.h  timer.c
tests/    test_runqueue.c test_coop.c test_preempt_guard.c test_spin.c test_mixed.c test_stress.c
scripts/  test_mem.sh
main.c  Makefile  README.md  STAGE2.md  STAGE2_PLAN_FOR_CLAUDE.md  STAGE2_QUICKSTART.md
```

`include/tcb.h` (public; `struct TCB` is **opaque** here):
```c
typedef struct TCB TCB;
typedef enum { SAF_READY, SAF_RUNNING, SAF_FINISHED } saf_state_t;
int  thread_create(void (*fn)(void *), void *arg);  /* >0 thread id, <0 error */
void thread_yield(void);
void thread_exit(void);                              /* does not return */
int  scheduler_run(void);                            /* 0 when all threads finished, <0 error */
typedef struct { long switches, preemptions, deferred, ticks, max_queue_len; } saf_stats_t;
void saf_get_stats(saf_stats_t *out);
```
Environment configuration (read once in `scheduler_run`): `SAF_PREEMPT=0` disables the timer;
`SAF_TICK_US` sets the interval (default `10000`, minimum `100`; invalid values → default).

`src/sched_internal.h` (private): full `struct TCB` { `int id; saf_state_t state; ucontext_t ctx;
void (*fn)(void*); void *arg; char *map; size_t map_len; unsigned vg_id;` }, `extern TCB *current;`,
`extern ucontext_t sched_ctx;`, `void sched_on_tick(void);`, counters.

`include/runqueue.h`: `int rq_init(size_t cap); void rq_destroy(void); int rq_enqueue(TCB *t);
TCB *rq_dequeue(void); size_t rq_size(void);` — header comment: *callers must hold SIGVTALRM
blocked (I2)*. Capacity default 1024 (`SAF_MAX_THREADS`); `thread_create` fails with `-EAGAIN`
when full.

`include/preempt.h`: `void preempt_disable(void); void preempt_enable(void);
void sig_block_timer(void); void sig_unblock_timer(void);
int saf_printf(const char *fmt, ...); void *saf_malloc(size_t n); void *saf_calloc(size_t n, size_t sz);
void *saf_realloc(void *p, size_t n); void saf_free(void *p);`

`include/timer.h`: `int timer_arm(long usec); void timer_disarm(void); long timer_ticks(void);`

---

## 6. Branching and merge rules

```
main ── stage1-final tag ── … ── stage2-final tag
  └─ stage2  (integration branch)
       ├─ s2/00-contract   ├─ s2/04-sched-ctx     ├─ s2/08-tests
       ├─ s2/01-build      ├─ s2/05-preempt-guard ├─ s2/09-hardening
       ├─ s2/02-runqueue   ├─ s2/06-timer-ticks
       ├─ s2/03-c-port     ├─ s2/07-preempt
```

- Create a task branch from the **latest** `stage2` only after its dependencies are merged.
- Before a PR: `git fetch && git rebase origin/stage2`.
- Merge gate (must pass on the rebased branch): `make clean && make && make test && make test-mem`.
  (Before task 01 exists, the gate is: the program builds and runs.)
- Merge with `git merge --no-ff` (keeps each task revertible with `git revert -m 1 <merge>`).
- The other team member reviews and approves every PR.
- Bugs in merged work: new branch `s2/fix-<topic>` from `stage2`.
- Use `git mv` for moves (task 00 only).
- `stage2` → `main` once, at the end, then tag `stage2-final`.

Merge order:

| # | Branch | Owner | Branch after | Blocks |
|---|--------|-------|--------------|--------|
| 1 | s2/00-contract | A+B | — | all |
| 2 | s2/01-build | B | 00 | 02 |
| 3 | s2/02-runqueue | B | 01 | 04 |
| 4 | s2/03-c-port | A | 00 (rebase on 02 before PR) | 04 |
| 5 | s2/04-sched-ctx | A+B (A types) | 02 + 03 | 05, 06 |
| 6 | s2/05-preempt-guard | B | 04 | 07 |
| 7 | s2/06-timer-ticks | A | 04 | 07 |
| 8 | s2/07-preempt | A | 05 + 06 | 08 merge |
| 9 | s2/08-tests | B | 05 (rebase after 07; merge after 07) | 09 |
| 10 | s2/09-hardening | A+B | 07 + 08 | stage2 → main |

Owners: **A = Preemption Engine** (contexts, stacks, scheduler, timer). **B = Scheduling & Safety**
(run queue, preempt guard, build, tests). Only B edits the Makefile. Only A edits `tcb.c`, `timer.c`
and `sched_internal.h`; only B edits `runqueue.c`, `preempt.c` and `preempt_internal.h` — except
the explicitly listed joint tasks.

---

## 7. Tasks

Each task lists: files, specification, acceptance criteria (with commands).

### Task 00 — `s2/00-contract` (A+B, Day 1)

Files:
- `git mv tcb.h include/tcb.h`, `git mv tcb.cpp src/tcb.cpp`, `git mv test_mem.sh scripts/test_mem.sh`.
- Create `include/runqueue.h`, `include/preempt.h`, `include/timer.h` with the §5 prototypes and
  doc comments (one line per function: purpose, return values, blocking requirements).
- Create `src/preempt_internal.h` (private, owned by B) declaring
  `extern volatile sig_atomic_t preempt_count, preempt_pending;` — read by the handler/scheduler in
  `tcb.c`, defined in `preempt.c`.
- Create `src/preempt.c` with **working** `sig_block_timer`/`sig_unblock_timer` (`sigprocmask`
  with a static `sigset_t` containing `SIGVTALRM`) and **stub** guard/wrappers: `preempt_disable`
  / `preempt_enable` do nothing; `saf_*` call the libc function directly.
- Update `include/tcb.h`: rename enum values to `SAF_*` (D8) and add the §5 prototypes as
  comments marked `/* Stage 2 API — implemented in task 03/04 */` so the old code still compiles.
- Minimal Makefile (or adjust the existing one) so the Stage 1 program still builds from the new
  paths.

Acceptance: Stage 1 demo still builds and prints 1,2,3 alternation. No logic changes.

### Task 01 — `s2/01-build` (B, Day 1)

`Makefile`:
- `CC = gcc`, `CFLAGS = -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -g -pthread -Iinclude`
  (I9). `DEBUG ?= 1`; when `DEBUG=0` add `-O2 -DNDEBUG`.
- Library sources by wildcard: `SRC = $(wildcard src/*.c)`; objects into `build/`.
- Each `tests/test_*.c` builds to `build/tests/test_*` linked with the library objects.
- Targets: `all` (builds `./scheduler` from `main.c`), `test` (builds and runs every test; fails on
  first non-zero exit), `test-mem` (runs `scripts/test_mem.sh`), `test-long` (added in 08), `clean`.
- Until task 03 lands, `tcb.cpp` still exists: add a temporary rule compiling it with `g++`
  (remove in 03). Mark it `# TEMP: removed in task 03`.

`scripts/test_mem.sh`:
- `set -euo pipefail`. If `valgrind` is missing: print an error and `exit 1` (D9).
- Run `valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,indirect,possible --error-exitcode=1`
  on `./scheduler` and every test binary except those listed in a `SKIP_VALGRIND` array (start
  empty). Print a pass/fail summary; exit non-zero if any failed.

`.gitignore`: `build/`, `scheduler`, `*.o`, `vgcore.*`.

Acceptance: `make clean && make` passes; `make test-mem` passes on a machine with Valgrind;
`PATH=/usr/bin/false make test-mem`-style check (or temporarily renaming valgrind) shows failure
when Valgrind is absent.

### Task 02 — `s2/02-runqueue` (B, Days 1–2)

`src/runqueue.c`:
- Static array of `TCB *` allocated by `rq_init(cap)` (`calloc`; return `-ENOMEM` on failure,
  `-EINVAL` for `cap == 0`, `-EALREADY` if already initialised).
- `head`, `tail`, `count`; indices advance with `% cap`. Use `count` to distinguish full/empty.
- `rq_enqueue`: `-EAGAIN` if full, `-EINVAL` for `NULL`. Update a `max_len` statistic.
- `rq_dequeue`: `NULL` if empty. `rq_destroy` frees the array and resets all fields (idempotent).
- No locking, no signal handling inside (caller's responsibility, I2).

`tests/test_runqueue.c` (plain `assert`-based, prints `PASS test_runqueue` at end):
FIFO order; dequeue on empty → `NULL`; enqueue on full → `-EAGAIN`; 10,000 enqueue/dequeue cycles
across the wrap boundary keep order; `rq_destroy` then `rq_init` works; `rq_init(0)` → `-EINVAL`.
Use dummy `TCB *` values cast from distinct addresses of a local array (TCB is opaque).

Acceptance: `make test` and `make test-mem` pass.

### Task 03 — `s2/03-c-port` (A, Days 1–2)

- `git rm src/tcb.cpp`; create `src/tcb.c`; remove the TEMP rule from the Makefile (coordinate
  with B: this is the one allowed Makefile edit by A, in a separate commit).
- Create `src/sched_internal.h` with the full `struct TCB` (§5). Move the Valgrind include into
  `src/tcb.c` guarded by `__has_include` (D10).
- `thread_create(fn, arg)`: validate `fn != NULL` (`-EINVAL`); `calloc` the TCB (D4); assign
  increasing ids from 1; allocate stack with `mmap(NULL, STACK_SIZE + page, PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0)`; `mprotect(map, page, PROT_NONE)` guard (D6);
  `VALGRIND_STACK_REGISTER(map + page, map + map_len)`; `getcontext`; set `uc_stack` to the usable
  region (above the guard page); add `SIGVTALRM` to `uc_sigmask` (I3); `makecontext(&ctx, tramp, 0)`;
  enqueue. On any failure, undo everything already done and return the negative error (no leaks).
  `page = sysconf(_SC_PAGESIZE)`; `STACK_SIZE = 64 KiB`.
- `tcb_destroy(t)`: `VALGRIND_STACK_DEREGISTER` **before** `munmap`; `free(t)`.
- In this task the scheduling logic may stay structurally like Stage 1 (round-robin, now using
  the run queue) — the hub-and-spoke rewrite is task 04. Remove all `exit()` calls (D7).
- `main.c`: one `static void worker(void *arg)` reading `*(int *)arg`; `ids[]` array; create via
  `thread_create`; check return values; keep `-i` and iteration-count flags but parse with
  `strtol` and reject invalid input with a usage message.
- `tests/test_coop.c`: 3 threads append their id to a shared array then yield, 3 iterations;
  assert the sequence is exactly `1,2,3,1,2,3,1,2,3`. Must run with `SAF_PREEMPT=0` (call
  `setenv` before `scheduler_run`; harmless before task 06).

Acceptance: `make test`, `make test-mem` pass. Manual (not committed) check: a thread doing
bounded deep recursion (depth counter, `volatile char buf[4096]` per frame, V10) dies with SIGSEGV
(exit 139) instead of corrupting memory.

### Task 04 — `s2/04-sched-ctx` (A+B, A types, Day 3) — critical path

`src/tcb.c`:
- Add `sched_ctx` with its own guard-paged stack, `main_ctx`, and `sched_loop` exactly as in §4.
- `scheduler_run()`: `sig_block_timer()`; init `sched_ctx` (`getcontext` while blocked, so its
  mask is blocked; `uc_link = &main_ctx`; `makecontext(sched_loop)`); `swapcontext(&main_ctx,
  &sched_ctx)`; after return, free the scheduler stack (deregister first), `rq_destroy()`,
  `sig_unblock_timer()`; return 0. Return `-EBUSY` if called re-entrantly.
- `thread_yield`, `thread_exit`, `tramp` exactly as in §4 (I1–I5). `uc_link` of every thread =
  `&sched_ctx` (safety net; `tramp` always calls `thread_exit`).
- Delete Stage 1 `scheduler()`, `context_switch()`, `current_index`, the vector, and remove their
  prototypes from `tcb.h` (D1, D2, D5, D11).
- Add the I10 debug assertions in `sched_loop` (before each switch) and in `thread_yield`.
- `thread_yield()` / `thread_exit()` called when `current == NULL` (from `main`) → no-op /
  return; document it.

`tests/test_coop.c` additions: single thread yielding 5 times sees its own state as RUNNING after
each yield (add a debug-only accessor `saf_debug_current_state()` in `sched_internal.h`, compiled
only without `NDEBUG`); a thread that finishes early is freed before the others finish (check via
a debug counter of destroyed threads incremented in `tcb_destroy`).

Acceptance: all tests and Valgrind pass; `./scheduler` output unchanged. If this task is not
passing by end of Day 3, both developers stay on it — nothing else starts.

### Task 05 — `s2/05-preempt-guard` (B, Day 4 morning)

`src/preempt.c` (replace stubs):
- Define `volatile sig_atomic_t preempt_count` and `preempt_pending` (declared in
  `src/preempt_internal.h`).
- `preempt_disable`: `preempt_count++`. `preempt_enable`: `assert(preempt_count > 0)`;
  `preempt_count--`; if it reached 0 and `preempt_pending`: clear it and call `thread_yield()`.
  (A signal landing between the decrement and the check simply preempts directly; at worst one
  extra yield happens — acceptable.)
- `saf_printf` uses `va_list` + `vprintf` inside the guard; `saf_malloc/calloc/realloc/free`
  likewise. (The deferred-preemption counter lives in the handler in `tcb.c`, task 07 — not here.)
- **Must not edit `src/tcb.c`.**

`tests/test_preempt_guard.c`: nesting (disable, disable, enable → no yield; enable → yield if
pending) using a manually set `preempt_pending` and two threads that record order; wrappers return
correct values; `saf_free(NULL)` is safe.

Acceptance: tests + Valgrind pass. Merge by mid-Day 4 (task 07 depends on it).

### Task 06 — `s2/06-timer-ticks` (A, Day 4, parallel with 05)

`src/timer.c`:
- `timer_arm(usec)`: validate `usec >= 100` (`-EINVAL`); `sigaction(SIGVTALRM)` with
  `sa_sigaction = on_tick`, `sa_flags = SA_SIGINFO | SA_RESTART`, `sa_mask = {SIGVTALRM}`; save the
  previous action; `setitimer(ITIMER_VIRTUAL, {usec, usec})`. Return `-errno` on failure (undo
  `sigaction` if `setitimer` fails).
- `timer_disarm()`: zero timer; restore previous action. Idempotent.
- `on_tick` in this task only increments `g_ticks` (`volatile sig_atomic_t`). `timer_ticks()`
  returns it.

`src/tcb.c`: in `scheduler_run`, read `SAF_PREEMPT` / `SAF_TICK_US` (§5); arm the timer **after**
`sched_ctx` is fully initialised and **before** the first switch; disarm right after control
returns to `main_ctx`.

Acceptance: a temporary (uncommitted) spin workload of ~3 s CPU shows `timer_ticks()` ≈ CPU time /
10 ms (±20%); all tests and Valgrind pass; `SAF_PREEMPT=0` produces 0 ticks.

### Task 07 — `s2/07-preempt` (A, Days 4–5)

- `src/timer.c`: `on_tick` increments `g_ticks` then calls `sched_on_tick()`.
- `src/tcb.c`: implement `sched_on_tick()` exactly like the §4 handler body (I6, I7). Counters
  `g_preemptions`, `g_deferred`, `g_switches` (`volatile sig_atomic_t` or `long` written only with
  the signal blocked/inside the handler — document which). Fill `saf_get_stats`.
- `main.c`: add `--spin N` mode: N threads loop on a counter without yielding until
  `timer_ticks() >= 300`, then print per-thread counts with `saf_printf` and exit.

Acceptance: `./scheduler --spin 3` shows all counters > 0 and max/min < 1.2 (V4);
`SAF_PREEMPT=0 make test` passes (cooperative order intact); Valgrind clean.

### Task 08 — `s2/08-tests` (B, Days 4–5; merge after 07)

All tests stop on **tick counts** via `timer_ticks()`, never on wall-clock time (V9). Each prints
`PASS <name>` and exits 0, or prints the reason and exits 1.

| File | Workload | Pass condition |
|------|----------|----------------|
| `test_spin.c` | 3 non-yielding counters until 300 ticks | all > 0; max/min < 1.2; `preemptions > 0` |
| `test_mixed.c` | 2 yielders, 2 spinners, 2 `saf_printf` writers (to a pipe/`/dev/null` via `freopen`, or count via `snprintf` into a buffer) until 500 ticks | terminates; every line well-formed if captured; `deferred > 0` |
| `test_malloc_storm.c` | 3 threads looping `saf_malloc`/`saf_free` of varying sizes until 300 ticks | no crash; Valgrind clean (V6 shows the unguarded version crashes — do not add an unguarded variant to the suite) |
| `test_stress.c` | `SAF_TICK_US=100`, 6 mixed threads, 2,000 ticks | no crash; I10 assertions hold (DEBUG build) |

`Makefile`: add `test-long` target that runs `test_mixed` with 30,000 ticks (≈ 5 min at 10 ms).
Add slow tests to `SKIP_VALGRIND` only if a Valgrind run exceeds 5 minutes; record why.

Acceptance: `make test`, `make test-mem`, `make test-long` pass; run `make test` 10 times in a
loop with zero failures (flakiness check).

### Task 09 — `s2/09-hardening` (A+B, Days 6–7)

- Print `saf_get_stats` at the end of `./scheduler` runs.
- Build with `DEBUG=0` (`-O2 -DNDEBUG`) and run the full suite — optimisation exposes missing
  `volatile` on handler-shared variables.
- Review checklist: every `mmap` has a matching `munmap`; every `VALGRIND_STACK_REGISTER` a
  `DEREGISTER`; no `exit()` in `src/`; no stdio/malloc reachable from `on_tick`; all handler-shared
  variables `volatile sig_atomic_t`; every public function validates input.
- `STAGE2.md`: architecture diagram (§4), invariants, deferred-preemption rule with the V6
  evidence, platform assumptions (R1, R2, V9), known limitations (§9), how to run tests.
- `README.md`: build/test instructions.
- Final: `make clean && make && make test && make test-mem && DEBUG=0 make test` on a fresh clone;
  merge `stage2` → `main` with `--no-ff`; tag `stage2-final`.

---

## 8. Definition of done (Stage 2)

- [ ] All tasks merged in order; `stage2-final` tag on `main`.
- [ ] `make test` passes 10 consecutive runs; `make test-mem` 0 errors, 0 bytes lost.
- [ ] `./scheduler --spin 3` shows fair sharing (ratio < 1.2).
- [ ] `SAF_PREEMPT=0` reproduces exact Stage 1 cooperative order.
- [ ] Stack overflow → SIGSEGV at guard page (manual demo for viva).
- [ ] Both developers can explain §4 invariants and the handler line by line.

---

## 9. Known limitations and Stage 3 forward-compatibility

- Blocking system calls (`read`, `sleep`) block every user thread (1:N). `ITIMER_VIRTUAL` also
  stops counting while the process sleeps.
- Third-party libraries that call `malloc` internally are not guarded; only `saf_*` calls are
  safe under preemption.
- `SA_RESTART` does not restart every syscall; user code must handle `EINTR`.
- **Stage 3 changes (do not do now; keep these seams):**
  - `sigprocmask` → `pthread_sigmask` (only inside `sig_block_timer`/`sig_unblock_timer`).
  - `setitimer` (process-wide, delivered to an arbitrary thread) → per-worker `timer_create` with
    `SIGEV_THREAD_ID` (only inside `timer.c`).
  - `current`, `sched_ctx`, `preempt_count`, `preempt_pending`, counters → `_Thread_local`.
  - The single run queue → per-worker deques; I4's "re-enqueue only after switch-back" rule is
    what makes stealing safe — never relax it.
  - Unguarded `malloc` will then also risk deadlock on glibc arena locks, not only corruption.
