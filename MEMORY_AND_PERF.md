# Memory and performance notes

The prototype treats optimization as an experiment, not a license to change the machine's semantics.

## Ant layout

The ant structure has been reduced by removing the unused `quantum_hint` field, packing lifecycle booleans into one atomic flag word, removing the redundant ant ID field (array index is the ID), replacing floating-point token balances with Q16 fixed-point, and giving each ant its own non-atomic LFSR state.

Only fields that can be observed or changed by another thread are atomic. Rule pointer, scheduler-only time bookkeeping, and the ant-local RNG belong to the leased worker/scheduler ownership model.

## Hot-loop changes

The expensive operations removed from the instruction loop are:

* `clock_gettime()` once per instruction; time is sampled at dispatch boundaries.
* repeated token-rate/capacity floating-point arithmetic; token accounting occurs at scheduler boundaries.
* general-purpose modulo in the hot world index path; x/y are kept in range and the index is `y * width + x`.
* repeated global RNG contention; runtime mutation uses each ant's private LFSR.
* scheduler quantum locking; quantum is an atomic read.

Collision discovery remains an O(32) scan intentionally because 32 is the architectural population ceiling.

## Headless mode

Use `--headless` to remove SDL rendering completely. Workers then run continuously while the control thread wakes periodically to service the debug dump and stdin.

Type `Q` followed by Enter to stop and write the rolling debug dump. `SIGUSR1` can be used by an external harness as a future extension.

## Measuring the result

Useful Linux tools:

```bash
perf stat -d ./turmite --headless --ants 32 --quantum 64 --minutes 0.25 --dump-pages 1
perf record -g ./turmite --headless --ants 32 --quantum 64 --minutes 0.25 --dump-pages 1
perf report
```

For cache behavior:

```bash
perf stat -e cycles,instructions,cache-references,cache-misses,branches,branch-misses \
  ./turmite --headless --ants 32 --quantum 64 --minutes 0.25 --dump-pages 1
```
