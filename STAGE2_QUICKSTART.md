# Stage 2 Quickstart

**Goal:** threads that never call `thread_yield()` still take turns.
A timer fires every 10 ms and forces a switch.

Full details live in `STAGE2_PLAN_FOR_CLAUDE.md`. If the two files disagree, that file wins.

---

## Who does what

| Person A: Preemption Engine | Person B: Scheduling & Safety |
|---|---|
| Thread switching, stacks, scheduler, timer | Run queue, safety wrappers, Makefile, tests |
| Owns `tcb.c`, `timer.c`, `sched_internal.h` | Owns `runqueue.c`, `preempt.c`, `Makefile`, `tests/` |

Only edit the other person's files after telling them first.

---

## The big idea (learn this first)

1. **Threads never switch to each other directly.** They always go back to the scheduler, and the
   scheduler picks the next thread. Think of a hub with spokes.
2. **The timer signal is blocked while the scheduler is working**, so it can't interrupt itself.
3. **The timer must not interrupt `printf` or `malloc`.** If it does, the program crashes. We
   tested this: it crashed in every run. So we use `saf_printf` and `saf_malloc`. These say
   "don't switch me right now"; the switch waits until the call finishes.

---

## Order of work

| # | Task | Who | Day | Start after |
|---|---|---|---|---|
| 00 | Set up folders and headers | Both | 1 | — |
| 01 | Makefile and leak-check script | B | 1 | 00 |
| 02 | Ring buffer queue and its tests | B | 1–2 | 01 |
| 03 | Convert C++ to C, stacks with guard page | A | 1–2 | 00 |
| 04 | **Scheduler context (hardest)** | Both | 3 | 02 and 03 |
| 05 | `saf_printf` / `saf_malloc` guard | B | 4 | 04 |
| 06 | Timer that only counts ticks | A | 4 | 04 |
| 07 | Timer actually switches threads | A | 4–5 | 05 and 06 |
| 08 | Fairness and stress tests | B | 4–5 | 05 (merge after 07) |
| 09 | Cleanup, docs, final merge | Both | 6–7 | 07 and 08 |

If task 04 isn't working by the end of Day 3, both of you stay on it. Everything after it depends
on it.

---

## Git in 6 rules

1. Never push directly to `main` or `stage2`.
2. Use one branch per task, named like `s2/03-c-port`, made from the latest `stage2`.
3. Before opening a pull request: `git fetch && git rebase origin/stage2`.
4. Merge only if `make && make test && make test-mem` all pass.
5. The other person reviews before merging. Merge with `git merge --no-ff`.
6. Found a bug in merged code? Make a new branch `s2/fix-something`.

**First commands (Day 1, once):**
```
git checkout main && git tag stage1-final && git push --tags
git checkout -b stage2 && git push -u origin stage2
git checkout -b s2/00-contract
```

---

## Things that will trip you up (we tested these)

- Compile with `-D_GNU_SOURCE` in the Makefile, and **never** also `#define` it in a `.c` file.
  Doing both is a build error under `-Werror`.
- The timer can't go faster than about 4 ms on Linux. Write tests as "stop after 300 ticks", not
  "stop after 3 seconds".
- Under `-Werror`, an infinite-recursion test won't compile. Use a depth counter.

---

## Using Claude for a task

Open the repo with both plan files in it and say:
> "Read STAGE2_PLAN_FOR_CLAUDE.md. I am Person A. Do task 03."

Claude will explain the changes afterwards. **Read that explanation.** You have to defend this code
in the viva.

---

## Done when

- [ ] `./scheduler --spin 3` shows all 3 threads getting about equal CPU time
- [ ] `SAF_PREEMPT=0` still gives the exact 1, 2, 3, 1, 2, 3 order
- [ ] Valgrind reports 0 leaks
- [ ] Both of you can explain the scheduler loop and the signal handler line by line
