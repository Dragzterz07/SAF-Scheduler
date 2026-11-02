# SAF-Scheduler

A small user-space **1:N thread scheduler** prototype. Stage 2 adds timer-driven preemption to cooperative threads; M:N scheduling is planned for a later stage.

## Build and test

The documented target is Linux x86-64 with GCC and glibc.

```sh
make
make test
make test-long
make test-mem
make clean && make DEBUG=0 test
```

`make test-mem` requires Valgrind. See [STAGE2.md](STAGE2.md) for the architecture, safety rules, platform assumptions, limitations, and detailed test instructions.
