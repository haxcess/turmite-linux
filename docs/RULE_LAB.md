# Rule lab

Open [rule-lab.html](../rule-lab.html) directly in a browser with the repository's `tools/` directory beside it. No server, CDN, or installation is needed. The historical `turmite-ruleTesting.html` remains available, but its catalogue and visual behavior differ from the C implementation; use the rule lab for C-compatible experiments.

## Investigating invisible ants

Two distinct mechanisms explain invisible ants without a missing-pixel renderer:

1. **Unreachable batch size (fixed).** With `--quantum 790 --min-service 1000`, the previous dispatcher required 790 tokens. Capacities are randomized from 128 to 4096. An ant with capacity below 790 could never qualify, even with a full bucket; its occupied cell remained available for collisions, but it executed no drawing instructions. The minimum is now `min(min_service, quantum, floor(token_capacity))`. This is checked when dispatching, so mutated/cloned phenotypes and runtime quantum changes use the correct threshold. `-v 3` changes refill rate, not the capacity or eligibility threshold. Draining ants retain their batching exemption.
2. **Invisible movement (valid rule behavior).** Snowflake's state 2/color 0 action is `A(0, TURN_F, 2)`: preserve the background, continue straight, stay in state 2. On an empty canvas it can move indefinitely without leaving a trail. Encountering color 1 takes it out of that state. Linux randomly selects each ant's initial state and heading, so this is a reachable startup behavior, not merely a theoretical table entry.

To reproduce the second case, load **Snowflake**, set **Start state** to **2**, and step 1,000 instructions. The movement count rises while non-background cells remain zero. Keep the marker enabled to see the ant. The marker is an overlay and never changes the tape. Compare with start state 0.

Other rules can trace small repeated areas, erase their own trail, or traverse a color outside their declared range without changing it. A visible pattern is not an ant counter. All current catalogue entries have valid active actions and none currently uses HALT. These checks do not prove that every pattern is expanding or exclude separate collision/lifecycle issues in concurrent runs.

## Generate and test

1. Load a built-in rule, or choose 1–4 states and 1–6 colors and enter a nonzero 32-bit seed (decimal or `0x` hexadecimal).
2. **Generate** constructs a reproducible table. **New seed** changes the seed input; press Generate to use it. **Mutate one action** changes one selected turn in the current table. It is also seeded and does not introduce HALT.
3. Edit the write color, turn, next state, and optional HALT flag for every state/color pair. F/R/L/B are relative turns; H holds position; N/E/S/W are absolute headings. Generation uses relative turns by default; enable the checkbox to also allow hold and compass turns.
4. Run, pause, step once, or advance 1,000 instructions. Resets start on an empty toroidal canvas at its center. The starting state and heading are selectable. Action edits reset and pause to avoid mixing two rules' histories.
5. Examine movement, color-changing writes, non-background cells, visited cells, and the number of instructions since the last color change. Try several initial states/headings before judging a candidate.

This is a single-ant rule experiment. It does not model token scheduling, collisions, mutation, or five-minute restarts. It matches the C interpreter's write → state/turn → optional HALT → move order. HALT stops the experiment; Linux instead generates a fresh random rule and scheduling phenotype, favoring smaller state/color counts. Collision mutations change a single action field while preserving other ant properties. Unrecognized tape colors use the C fallback: preserve color and state and move forward. The six preview colors are exported from `src/renderer.c`.

Generated tables may be stationary, blank, cyclic, or visually similar to known rules. The lab warns about exact catalogue-table matches; it does not prove mathematical novelty. Save interesting candidates with JSON export before reloading or closing the page. JSON import expects the lab's complete rule object, not the historical playground's preset format.

## Export to C

Give the rule a unique lowercase ID and a printable ASCII display name. Copy or download the C entry and insert it as an element of `static const TurmiteRule RULES[]` in `src/rules.c`. The exported rows use its existing `A(write, TURN_*, next_state)` and `HLT(...)` macros, with a trailing comma after the entry. Rebuild with `make`. Rule selection and catalogue count already derive from the array; no separate registration is needed. The lab does not modify `rules.c` or promise a generated rule will be selected in any particular random run.

After changing the C rules or palette, run:

```sh
make rule-catalog
```

This compiles the small C exporter and refreshes `tools/rule-catalog.js`. The generated data is checked in so the lab works without a build. It records the actual compiled table, including macro-expanded NOOP actions, rather than relying on a hand-maintained duplicate.

## Validation

```sh
make capacity-test        # no SDL or JavaScript runtime needed
make core-test            # also includes the capacity regression
make rule-lab-test        # additionally requires Node.js (NODE can override it)
```

The capacity test covers full buckets below/equal/above the configured batch threshold, partial normal buckets that must wait, and draining ants. The original scheduler fails the bounded test; the corrected scheduler passes.

The rule tests check generation reproducibility, mutation, input validation, all turns, wrapping, hold, HALT, and unsupported-color fallback. C reference traces cover every active catalogue state and heading for 10,000 instructions on a 64×48 canvas, comparing the full tape hash, final position/state/heading, instruction count, and non-background count. The target also compiles and runs 24 generated C entries, including HALT and escaped names. These are semantic tests; they do not validate a browser's visual layout or concurrent Linux collision behavior.


## Runtime evolution and saved exports

The three exports in `patches/lab/` are now startup catalogue entries: `red_walker`, `mutant_bcff52d4`, and `mutant_f6438bee`. The source export files are unchanged; Red Walker receives a distinct catalogue ID/name because two exports originally shared an ID. The C catalogue and refreshed browser catalogue contain the same 21 rules.

The browser's mutation button still changes one turn and does not introduce HALT. Linux collision mutation is broader: it changes one action's write color, turn, next state, or HALT flag. The table dimensions stay fixed. Only HALT rebirth chooses new dimensions, using decreasing weights for larger counts. The browser's explicit dimension choices and seeded generator remain independent of the Linux per-ant RNG sequence. Neither system guarantees expanding or interesting rules; one-color rules and holds can remain invisible or stationary.

Debug dumps intentionally omit runtime tables. Save interesting lab candidates as JSON or C exports before adding them to the built-in catalogue; a Q dump is not a mutant rule archive.
