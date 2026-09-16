# Turmite Universe — Linux Concept Specification

## Goal

A small embedded-oriented computational artwork in which logical turmites (ants) execute concurrently against a shared two-dimensional, six-color memory space. Linux is the prototype substrate; a later RTOS MCU version should preserve the machine model.

## Fixed rules

- C17 implementation.
- SDL2 graphical window.
- World cell is 4x4 screen pixels.
- Six cell colors.
- Maximum 32 ants; population levels are 2, 4, 8, 16, 32.
- Two worker threads by default; Linux schedules them normally.
- An ant can execute on at most one worker at a time.
- The scheduler is a shared concurrent object protected by normal synchronization.
- World cells use relaxed C atomics; a turmite transition is not a transaction.
- Last world write wins.
- No occupancy RAM; collision detection scans ant contexts.
- Collision winner is the ant with greater accumulated token health. Equal health uses lower ant ID as the winner.
- The weaker ant is clobbered, keeps its position, and is reincarnated with a random rule, direction, internal state, and scheduling phenotype.
- Newborns reread the cell on their next instruction.
- Token bucket accounting uses fractional time. One token buys one turmite instruction.
- The scheduler runs when a worker needs another ant; there is no global ant simulation tick.
- Initial scheduler policy is weighted-fair/deficit-like scheduling and is selected at universe initialization. Only WFQ-like policy is implemented initially.
- Quantum is a global scheduler parameter and is controllable at runtime.
- Ants can own mutable scheduling parameters. The rule-action data model leaves room for rule-driven schedule mutations; the first library does not yet exercise that extension.
- Doubling clones live ants into inactive contexts at random positions while retaining rule, direction, state, and scheduler phenotype; clone token balance starts empty.
- Halving places the weakest eligible ants under drain pressure by setting token generation to zero. They expire when their token budget is exhausted.
- Rules come from a built-in library and may grow over time.
- A separate gene pool/bookmark concept is reserved for future evolution experiments.
- Randomness is a Galois LFSR seeded from host entropy (or an explicit command-line seed). The seed is therefore enough to define the initial random sequence, while live Linux scheduling can still cause concurrent divergence between runs.
- SDL renders at 60 FPS while workers continue running independently.
- The framebuffer is an observation window into the current world.
- No e-ink behavior is simulated in the Linux concept.
- A five-minute lifecycle is emulated by a timer; the final MCU should replace this with a genuine watchdog reset.
- Pressing `Q` performs a debug quit and writes a headless rolling tape capture. The default is 127 pages sampled at 1 Hz. Accepted page counts are 1, 3, 7, 15, 31, 63, 127, 255, 511, and 1023.
- Each captured page contains the six-color world plus a forensic metadata snapshot: world hash/change count, age, LFSR state, active population, quantum, dispatch count, collisions, total instructions, and per-ant position/rule/state/token/scheduler/health metadata.
- Debug pages are SDL-independent raw byte images: one uint8 color index per world cell in row-major order.

## Intentionally unresolved for later experiments

- Exact weighted-fair algorithm beyond the compact first implementation.
- Rule-driven scheduler-parameter mutations.
- More scheduler policies such as CBWFQ-like classes or WRED-like behaviors.
- Better population pressure/drop semantics.
- Exact e-paper refresh behavior.
- MCU/display selection.

## V6 execution batching clarification

`quantum` remains the maximum number of instructions in one dispatch. `min_service` is a scheduler batching threshold: a normal ant is not dispatched until it has at least `min(min_service, quantum)` whole tokens available. This does not create tokens or alter the ant's configured token generation rate; it only changes burst size. Draining ants bypass the threshold so population reduction can consume their remaining token budget.

During a worker lease, the atomic occupancy index is the authoritative read-head residency structure. Packed ant positions are published at dispatch boundaries for debugging, dump capture, cloning/reincarnation, and cold-path observation; they are not consulted by the normal collision hot path.
