# Suggested First Experiments

Run the same explicit seed under different scheduler parameters and compare the resulting worlds.

```sh
./turmite --seed 0x12345678 --ants 8 --quantum 1
./turmite --seed 0x12345678 --ants 8 --quantum 8
./turmite --seed 0x12345678 --ants 8 --quantum 64
./turmite --seed 0x12345678 --ants 8 --quantum 256
```

Then try population changes during execution:

- `+` / `=` doubles the population up to 32.
- `-` begins token-drain expiration until the population is halved.

Useful things to watch are collision rate, mutation frequency, and whether a scheduler/quantum combination produces qualitatively different structures from the same starting seed.

The development HUD can be hidden with `H`; the universe itself never draws ant labels or textual information.
