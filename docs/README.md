# Documentation

[Turmite Universe](../README.md) is an academic art piece exploring concurrent cellular automata. Linux is the working implementation; the STM32 port remains a scaffold.

Run all shell commands from the repository root. Unless stated otherwise, source and tool paths in these guides are relative to that root.

| Guide | Contents |
| --- | --- |
| [Running](RUNNING.md) | Dependencies, command-line options, monitors, keyboard controls |
| [Behavior specification](SPEC.md) | Rules, scheduling, collisions, mutation, population, lifecycle, limitations |
| [Architecture](ARCHITECTURE.md) | Simulation model, synchronization, threads, frame ownership |
| [Rendering](RENDERING.md) | Six-color palette and portable display boundary |
| [Rule lab](RULE_LAB.md) | Offline experiments, rule generation, C export, missing trails |
| [Debugging](DEBUGGING.md) | Capture rings, Q dumps, analysis, interpretation limits |
| [Development](DEVELOPMENT.md) | Tests, source map, profiling tools, manual patch workflow |
| [Memory and performance](MEMORY_AND_PERF.md) | Storage budgets, benchmark interfaces, measurement caveats |
| [Experiments](EXPERIMENTS.md) | Reproducible measurement procedures |
| [Optimization queue](OPTIMIZATION_QUEUE.md) | Outstanding performance work |
| [STM32 port](stm32/README.md) | Existing scaffold and hardware decisions still open |
| [STM32 integration](stm32/INTEGRATION.md) | Proposed firmware bring-up and shared-state requirements |

[Optimization history](history/OPTIMIZATION_NOTES.md) preserves earlier decisions and measurements. Use the specification and current guides for present behavior.
