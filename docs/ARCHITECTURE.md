# Architecture

C17, pthreads, SDL2. Each universe owns its tape, colony, scheduler, RNG streams, and capture ring.

| Thread | Work |
| --- | --- |
| Main | SDL events, windows, uploads, HUD, presentation |
| Controller per universe | Commands, lifecycle, capture, CPU frame conversion |
| Shared ant worker pool | Leased instruction batches |

Graphical application threads: `1 + M + W` for M universes and W process-wide workers, excluding driver threads. SDL presentation remains serial on main.

| State | Synchronization |
| --- | --- |
| Membership and global dispatch | Pool mutex, then member scheduler mutexes in slot order |
| Leases, fair credit, recovery | Member scheduler mutex; one lease per ant |
| Tape | Independent relaxed atomic byte stores |
| Residency | Atomic occupancy bytes, CAS claim/release |
| Position, heading, state | Worker-local during grant; publication on release |
| Runtime rule tables | Scheduler updates only unleased ants; new spawns start from library rules |
| Completed frames | Short mutex-protected handoff; three ARGB buffers |

One dispatcher selects across all registered universes; workers perform selection themselves, without a dispatcher thread. Per-universe scheduler objects retain token, recovery, and control state. `--workers` sets the process-wide pool size (default two); `--ants` remains per universe. Headless uses main plus W workers. Android retains its standalone single-universe scheduler.

Suspend removes a universe from selection and drains its leases before reset or destruction. Other universes continue. The pool survives window closure and timed restarts; final shutdown joins it. Control changes explicitly wake sleeping workers. Empty selection sleeps until the next token or collision-recovery deadline; paused/zero-rate universes wait for an event. Short timed waits retain 1 ms coalescing. Blocked occupancy reclamation retries after 1 ms.

A shared wake event has its own mutex and generation counter. Workers register, rescan, then compare generations before sleeping, preventing lost wakeups. Never hold the event mutex while acquiring pool/scheduler locks. Suspension detaches the event under the scheduler mutex before allowing destruction.

Workers execute outside all dispatch locks. Instructions and movement are non-transactional. Published positions can lag residency. Capture samples live cells independently; completed frames remain stable during conversion/presentation.

[Instruction semantics](SPEC.md) · [Frame ownership](RENDERING.md) · [Storage](MEMORY_AND_PERF.md)
