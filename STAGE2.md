# SAF-Scheduler — Stage 2

Stage 2 adds timer-driven preemption to the cooperative user-thread scheduler. It is a **1:N scheduler**: many user threads run on one underlying kernel thread. The M:N worker-pool design is a later stage.

## Platform and scope

The validated target described by the implementation plan is Linux x86-64, glibc, GCC, and Valgrind 3.x. The scheduler uses `ucontext` for user-thread contexts and `ITIMER_VIRTUAL`/`SIGVTALRM` for timer ticks. Other platforms may have different `ucontext`, signal, timer, and Valgrind behavior; a successful build there is not a substitute for the Linux x86-64 checks.

## Architecture

```text
                         scheduler_run()
                              │
                              ▼
                    ┌───────────────────┐
                    │ scheduler context │
                    │ own stack; timer  │
                    │ signal blocked    │
                    └─────────┬─────────┘
                              │ dispatch
             ┌────────────────┼────────────────┐
             ▼                ▼                ▼
        user thread 1    user thread 2    user thread N
             │                │                │
             └──── yield, exit, or timer ──────┘
                              │
                              └── back to scheduler
```

Threads do not switch directly to other threads. They return to the scheduler, which chooses the next READY thread from the FIFO ring-buffer run queue. The scheduler uses its own stack, not a user thread's stack.

## Scheduling and safety rules

These invariants (rules that must always remain true) guide changes to the code:

1. **Hub and spokes:** context switches are between a user thread and the scheduler only.
2. **Scheduler signal mask:** `SIGVTALRM` is blocked while the scheduler runs or changes the run queue, `current`, or thread states.
3. **Context signal masks:** a context is entered with `SIGVTALRM` blocked. A new thread's trampoline unblocks it after startup; a yielding thread blocks it around its switch.
4. **Run queue contents:** the queue contains exactly READY threads. The RUNNING thread is not queued. A switched-out READY thread is re-enqueued only after control has returned to the scheduler.
5. **Thread cleanup:** only the scheduler frees a finished thread, after switching off its stack.
6. **Timer handler limits:** the handler counts the tick and, when appropriate, updates scheduling state and switches context. It must not print, allocate memory, take locks, or call `exit()`.
7. **Deferred preemption:** `saf_printf`, `saf_malloc`, `saf_calloc`, `saf_realloc`, and `saf_free` use the preemption guard. If a tick arrives while the guard count is nonzero, the handler records a pending request and returns. When the count reaches zero, the wrapper yields if a request is pending. The scheduler clears the pending flag when it dispatches a thread.
8. **Library error handling:** library functions report errors to their caller (negative error values or `NULL` as appropriate); they do not terminate the application with `exit()` or `abort()`.
9. **Feature-test macro:** `_GNU_SOURCE` is supplied by the Makefile, not separately defined in source files.
10. **Debug checks:** in a debug build, assertions check that at most one thread is RUNNING, `current` names that RUNNING thread, and the queue length matches the number of READY threads.

## Timer and deferred-preemption flow

1. `ITIMER_VIRTUAL` causes `SIGVTALRM` while the process is using CPU time.
2. The signal handler increments the tick count and calls the scheduler's tick handler.
3. If no user thread is running, the tick is counted but no user-thread switch is needed.
4. If preemption is guarded, the tick handler sets `preempt_pending` and increments the deferred count.
5. Otherwise, it marks the current thread READY and switches back to the scheduler context.
6. Once a guarded library call finishes, `preempt_enable()` yields if a tick was deferred.

The guard is needed because interrupting a library routine such as `malloc` or formatted output partway through its work can corrupt that library's internal state. Only calls made through the project's `saf_*` wrappers receive this protection; third-party library calls are not automatically guarded.

## Stacks and cleanup

Each user thread receives a dynamically mapped stack with a protected page at its low end. The protected page is a **guard page**: if the stack grows into it, the process gets a fault instead of silently writing over adjacent memory. The scheduler also runs on its own mapped stack.

Every successful mapping must eventually be unmapped. When Valgrind stack registration is available, each registration must be deregistered before that stack is unmapped. Error paths during stack setup must release any resources already acquired.

## Statistics

`./scheduler` prints statistics after a successful scheduler run. The counters are cumulative for the process; `max_queue_len` is the largest run-queue length observed. The fields are:

- `switches`: scheduler thread dispatches.
- `preemptions`: timer ticks that switched out a running user thread.
- `deferred`: ticks postponed because preemption was guarded.
- `ticks`: timer signals counted.
- `max_queue_len`: highest number of READY threads queued at once.

A short cooperative run can legitimately report zero ticks if it finishes before the first timer signal. `--spin` is the mode for checking timer-driven sharing among threads that do not call `thread_yield()`.

## Build and test

From the repository root:

```sh
make clean && make
make test
make test-long
make test-mem
make clean && make DEBUG=0 test
```

`make test` runs every `tests/test_*.c` test. The tests use timer-tick counts rather than wall-clock deadlines. `make test-long` runs the mixed workload for 30,000 ticks and can take several minutes. `make test-mem` requires Valgrind; the script fails if Valgrind is unavailable rather than silently skipping the check. Run the memory check on the documented Linux x86-64 target before final release.

Useful manual checks:

```sh
SAF_PREEMPT=0 ./scheduler
./scheduler --spin 3
```

With preemption disabled, the cooperative demo should retain its round-robin order. In spin mode, compare the per-thread iteration counts; the plan's fairness threshold is maximum progress divided by minimum progress below 1.2.

## Platform evidence and assumptions

The implementation plan records these prototype findings; they describe the tested Linux/glibc environment, not a guarantee for every operating system:

- The plan's V6 experiment found that unguarded `malloc`/`free` under timer preemption crashed in six prototype runs; the guarded version ran cleanly. This is why the allocator wrappers and deferred-preemption rule matter.
- The plan's V9 experiment found that requesting a 100-microsecond timer interval produced about 4 milliseconds per tick on its test machine. Tests therefore stop by tick count and must not assume sub-millisecond timer accuracy.
- R1 explains why contexts must carry the blocked timer signal in their saved masks: changing the mask before restoring all context registers could otherwise leave a window for a signal to arrive on the scheduler stack.
- R2 notes that POSIX does not guarantee `swapcontext()` is safe inside a signal handler. This project relies on the tested Linux/glibc behavior and must not present it as universally portable.

## Known limitations and future work

- This is 1:N: a blocking system call such as `read()` or `sleep()` blocks all user threads. `ITIMER_VIRTUAL` also stops counting while the process is not using CPU time.
- Only the project's `saf_*` calls are protected. Third-party code that calls `malloc` internally is not automatically safe during preemption.
- `SA_RESTART` does not restart every system call; callers must handle `EINTR` where needed.
- Stage 3 may replace `sigprocmask` with `pthread_sigmask`, use per-worker timers, make scheduler state thread-local, and replace the single run queue with per-worker deques. The rule that a READY thread is re-enqueued only after switching back to the scheduler is important for that future work.
