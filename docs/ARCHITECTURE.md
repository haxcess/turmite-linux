# Simulation and presentation architecture

Run commands from the repository root; source paths below are relative to that root.

## Current behavior

- 2, 4, 8, 16, or 32 initial ants; two POSIX ant worker threads per universe by default.
- Twenty-one built-in rules, with up to four internal states and six logical colors.
- A wrapping 2-D world with one relaxed atomic byte per logical cell. A complete turmite instruction is deliberately not a transaction.
- A separate atomic byte per cell records occupancy for O(1) collision lookup. Token balance determines collision health; lower ant ID wins ties.
- Q16 token buckets, weighted-fair credit, configurable quantum, minimum-service batching, and a universe-wide token-rate divisor.
- Population doubling clones eligible ants with full token buckets; halving requests gradual token-drain retirement.
- SDL2 presentation at a target 60 FPS, independent of worker execution, with one world cell per display pixel.
- Platform-independent six-color frame conversion with a fixed, hand-tuned Solarized-inspired Linux preview palette. The core stores only logical colors; the host owns its prepared RGB frames. There is no palette drift or ink history.
- A five-minute software lifecycle stops workers, clears the universe, and starts again with a fresh seed. It is not a hardware watchdog.

See [SPEC.md](SPEC.md) for exact behavior and known limitations.

## Concurrency and presentation

The scheduler mutex protects leases and fair-credit bookkeeping; each ant can be leased by only one worker at a time. Workers execute outside that mutex. Occupancy updates use compare-and-swap; tape writes use independent relaxed byte stores. Workers do no rendering work.

Workers keep position, heading, and state local during a quantum and publish them when the grant ends, including an early collision/HALT exit. The occupancy array is the live residency index. Collision losers pause for 50 ms, receive one action-field mutation, and resume when their retained cell is free. Other ant properties are preserved. HALT instead creates a fresh ant with a random rule biased toward fewer states/colors. Startup selects only built-in rules, including the three saved lab exports. See [SPEC.md](SPEC.md) for recovery and ownership details.

An explicit seed defines initialization and RNG streams, but timing and thread interleaving can make repeated runs diverge. A dump is observational, not a replay checkpoint.

Each graphical universe has a controller thread for lifecycle, input commands, capture, and CPU frame preparation, plus its own ant worker pool. The controller samples the tape into a stable, row-major frame of indices 0..5 and uses portable conversion to prepare RGB pixels. Three RGB buffers let it publish the latest complete frame without overwriting one being presented. Main owns SDL events, uploads, HUD drawing, and presentation; it does no world sampling or color conversion. With M monitors and W ant workers per universe, there are `1 + M × (W + 1)` application threads, excluding any SDL/driver threads. SDL presentation work still scales with the number of windows. Headless runs allocate no display snapshot or RGB buffers. See [RENDERING.md](RENDERING.md) for the platform boundary.
