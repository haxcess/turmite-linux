# Architecture

C17, pthreads, SDL2. Each universe owns its tape, colony, scheduler, RNG streams, and capture ring.

| Thread | Work |
| --- | --- |
| Main | SDL events, windows, uploads, HUD, presentation |
| Controller per universe | Commands, lifecycle, capture, CPU frame conversion |
| Ant workers per universe | Leased instruction batches |

Graphical application threads: `1 + M × (W + 1)` for M universes and W workers each, excluding driver threads. SDL presentation remains serial on main.

| State | Synchronization |
| --- | --- |
| Leases, fair credit, recovery | Scheduler mutex; one lease per ant |
| Tape | Independent relaxed atomic byte stores |
| Residency | Atomic occupancy bytes, CAS claim/release |
| Position, heading, state | Worker-local during grant; publication on release |
| Runtime rule tables | Scheduler updates only unleased ants; clone by value |
| Completed frames | Short mutex-protected handoff; three ARGB buffers |

Workers execute outside the scheduler mutex. Instructions and movement are non-transactional. Published positions can lag residency. Capture samples live cells independently; completed frames remain stable during conversion/presentation.

[Instruction semantics](SPEC.md) · [Frame ownership](RENDERING.md) · [Storage](MEMORY_AND_PERF.md)
