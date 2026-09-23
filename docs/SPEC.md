# Simulation specification

## Machine

- Toroidal tape: six logical colors, one relaxed atomic byte per cell.
- Maximum 32 ants; rule dimensions 1–4 states × 1–6 colors.
- Startup selects immutable `RULES[]` entries in `src/rules.c`.
- Instruction order: spend token → read/select → write → state/turn → HALT or move.
- Relative and absolute turns supported. `TURN_H` holds position; HALT ends the grant.
- Ants spawn with a random color offset 0–5. Local color is `(tape + 6 - offset) % 6`; unsupported colors select the highest local color. Writes translate back with `(local + offset) % 6`. The initial background remains global zero. Clones inherit offsets, mutations retain them, and rebirth chooses a fresh offset.
- Unsupported state: preserve tape and state, move forward.
- No global tick, transactional instruction, or rendering callback.
- Packed positions: `(y << 16) | x`; `world_init()` permits dimensions up to 65535 per axis, subject to allocation.

## Scheduling

One worker lease per ant. Selection and release use the scheduler mutex; execution occurs outside it.

Policy: WFQ-like credit scan across every registered universe (at most 32 slots each). Eligible candidates gain weight; greatest credit wins, ties favor lower universe slot, then lower ant index. Release subtracts executed work. Global selection recenters all active member credits on the winner; this preserves ordering and gives restarted universes a current baseline. Credit uses `double`; tokens use unsigned Q16.

| Phenotype | Initial / HALT-reborn range |
| --- | --- |
| Rate | 50,000–2,000,000 instructions/s before scaling |
| Capacity | 128–4096 tokens |
| Weight | 1–16 |
| Initial balance | Full bucket |

Accrual uses monotonic microseconds at scheduler boundaries, with elapsed time clamped to one second. The global divisor scales refill without changing inherited rates.

Normal dispatch threshold: `min(min_service, quantum, floor(capacity))`. Grant: available whole tokens, capped by quantum. Draining ants bypass the threshold. No eligible work: approximately 1 ms condition-variable wait.

## Collisions and mutation

Occupancy byte: 0 = empty, 1–32 = ant index + 1. Movement conditionally releases the source, then CAS-claims the destination. Greater token balance wins; ties favor lower universe slot, then lower ant index. A winning challenger replaces the resident.

1. Mark loser `CLOBBERED | DISPLACED`.
2. Stop at an instruction boundary and release lease. Failed claims end the grant immediately; an in-flight resident instruction may finish.
3. Wait 50 ms from the first idle recovery scan.
4. Change exactly one field in one private rule action: probability 1/16 toggles HALT; otherwise change a mutable write, turn, or next-state field to another valid value.
5. Set `WAITING` until the retained cell can be reclaimed. Occupied cells extend the wait without repeated mutation or teleportation.

Collision mutation preserves dimensions, position, heading, state, rate, capacity, weight, balance, and fair credit. RNG and mutation counter advance; ordinary refill and draining continue. Clear the old event before reclaim so a new collision remains pending. Mutate only unleased ants.

## HALT rebirth

HALT takes precedence over a simultaneous collision. At the retained position, generate a fresh rule and reset heading, state, scheduling phenotype, full balance, and fair credit; population remains unchanged.

| Random property | Distribution |
| --- | --- |
| State count 1–4 | Weights 8:4:2:1 |
| Color count 1–6 | Weights 32:16:8:4:2:1 |
| Write, turn, next state | Valid random values |
| HALT | Probability 1/64 per action |

HALT is the exception to collision-only runtime variation. Cloning copies existing rules without mutation. Runtime tables are private per slot and omitted from dumps.

## Population

- **Double:** up to 32; repeatedly clone the healthiest eligible, non-leased source. Inherit rule, heading, state, rate, capacity, weight; assign new RNG stream, random empty position, full bucket. New clones can become sources in the same request.
- **Halve:** select weak, enabled, non-leased, non-draining ants; set generation to zero. Retire below one token, release occupancy, decrement population.
- Requests are best-effort under active leases. Collision mutation preserves draining; HALT clears it. Intermediate counts may be arbitrary.
- Initial placement uses bounded random probes; callers currently assume success.

## Randomness and lifecycle

32-bit Galois LFSR, feedback `0x80200003`; zero remapped. Host entropy: `/dev/urandom`, process/time fallback. Initial display ordinal i uses `base_seed XOR (0x9e3779b9 × i)` modulo 2³²; zero becomes 1. Ordinal is independent of SDL display index.

Seeds reproduce initialization, not thread timing, token accrual, or collisions. Restart suspends the universe and drains its outstanding leases, clears state and capture history, discards runtime variants, obtains fresh entropy, and restores startup configuration. Pause blocks new normal dispatches; existing work, lifecycle, capture, and elapsed-time refill remain active.

Instruction/mutation counters accumulate per slot across HALT rebirths within a universe. `mutations` counts collision edits only.

## Limits

- One-second accrual clamp can discard refill time, especially at large divisors.
- No rule-driven scheduling fields, alternative scheduler policy, or gene-pool archive.
- Live captures are observational; see [dump limitations](DEBUGGING.md).
- [STM32 integration](stm32/INTEGRATION.md) remains unimplemented.

[Runtime defaults](RUNNING.md) · [Concurrency](ARCHITECTURE.md) · [Rendering](RENDERING.md)
