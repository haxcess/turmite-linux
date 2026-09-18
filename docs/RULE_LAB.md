# Rule lab

Open [rule-lab.html](../rule-lab.html) with `tools/` alongside it. Offline; no server or dependencies. `turmite-ruleTesting.html` is a legacy playground with a different catalogue.

## Edit and simulate

1. Load a catalogue rule or choose 1–4 states and 1–6 colors.
2. Enter a nonzero 32-bit seed; **Generate** builds a reproducible table. **New seed** only changes the seed field.
3. Edit write color, turn, next state, and HALT. F/R/L/B: relative; H: hold; N/E/S/W: absolute. Generation defaults to relative turns; optional checkbox enables hold/compass turns.
4. Select start state/heading; run, pause, single-step, or advance 1,000 instructions. Reset uses an empty torus with centered ant. Action edits reset and pause.
5. Inspect movement, writes, non-background/visited cells, and instructions since last color change. Export JSON to preserve candidates.

**Mutate one action** changes one seeded turn; no HALT insertion. Linux collision mutation can change any action field; see [runtime evolution](SPEC.md#collisions-and-mutation).

Instruction order matches C: write → state/turn → HALT or move. Unsupported colors preserve color/state and move forward. HALT stops the lab; Linux performs rebirth. The lab has no concurrent collisions, token scheduling, or lifecycle restarts. Exact catalogue-match warnings do not establish novelty.

## Export to C

Use a unique lowercase ID and printable ASCII name. Insert the exported initializer into `RULES[]` in `src/rules.c`; entries use `A(write, TURN_*, next_state)` / `HLT(...)` and a trailing comma. Selection/count derive from the array.

```sh
make
make rule-catalog
make rule-lab-test
```

`rule-catalog` regenerates `tools/rule-catalog.js` from compiled rules and palette. Refresh after source edits. JSON import requires the lab's complete rule object. Q dumps omit runtime tables; save candidates through lab JSON/C export.

## Investigating invisible ants

- **No writes:** Snowflake state 2/color 0 uses `A(0, TURN_F, 2)`. Start state 2 on an empty canvas and step 1,000 instructions: movement increases without a trail. The marker is an overlay. Compare state 0.
- **Local/erasing rules:** Holds, small cycles, erasure, and unsupported-color fallback can hide activity.
- **Historical dispatch starvation:** Large batches exceeded some bucket capacities. The threshold is now capped by capacity; `make capacity-test` covers this regression. `-v` changes refill rate, not capacity.

## Validation

`make rule-lab-test` requires Node.js (`NODE` override). Checks generation, mutation, validation, turns, wrapping, hold, HALT, fallback; compares every catalogue state/heading against 10,000-step C traces on a 64 × 48 canvas; compiles/runs 24 generated C entries. Browser layout and concurrent Linux behavior require separate checks.
