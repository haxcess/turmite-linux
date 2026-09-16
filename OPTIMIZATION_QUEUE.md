# Optimization Queue

1. Profile scheduler dispatch frequency and actual instructions per dispatch.
2. Optimize the O(32) collision search further if `perf report` shows it remains hot. Current layout keeps a compact 32-entry position array/bitset in L1.
3. Evaluate world-cell atomic load/store cost and cache behavior.
4. Evaluate token accounting and scheduler lock contention.
5. Compare scheduling quantum values under identical seeds.
6. Revisit data-oriented layout after profile evidence.

The Busy Beaver rule is intentionally excluded. HALT remains supported by the rule format, but a HALT transition should self-reincarnate rather than decrease population.
