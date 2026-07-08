# Thermal Pulse Solver — TB3 Task

Terminal-Bench 3 task: implement a C++ transient 2D heat-diffusion solver on the unit square with oracle-only access to source/boundary/coefficient data. The verifier evaluates multiple deterministic private instances under one shared wall-clock budget.

**Difficulty mechanism:** hidden temporal frequency in a manufactured solution — agents must discover the required resolution through oracle sampling or silently under-resolve. Brute-force over-resolution times out under the shared budget.

## Status

|| Standard (3 trials) | Adversarial (1 trial) |
|---|---|:---|
| Codex (GPT-5.5) | **3/3 clean failures** | **1/1 clean failure** (timeout) |
| Claude (Opus 4.8) | pending | pending |

All failures are numerical/budget — zero exceptions, no leakage, no verifier exploit. Every identified cheap path (coarse dt, explicit scheme, brute over-resolve, saved-artifact replay) has a named baseline with reward 0.0.

## Key files

- [`tasks/thermal-pulse-solver-cpp/instruction.md`](tasks/thermal-pulse-solver-cpp/instruction.md) — task prompt (28 lines)
- [`tasks/thermal-pulse-solver-cpp/DESIGN.md`](tasks/thermal-pulse-solver-cpp/DESIGN.md) — gate architecture and manufacturing solution
- [`tasks/thermal-pulse-solver-cpp/TRIAL_RESULTS.md`](tasks/thermal-pulse-solver-cpp/TRIAL_RESULTS.md) — full Codex matrix with error values and trajectory notes
- [`tasks/thermal-pulse-solver-cpp/EXPERIMENTS.md`](tasks/thermal-pulse-solver-cpp/EXPERIMENTS.md) — boundary sweeps and gate measurements

## Results (Codex)

| Trial | Reward | Failure | Exceptions |
|---|---|---|---|
| Standard #1 | 0.0 | i0 rel_error 0.0501 > 0.005 | 0 |
| Standard #2 | 0.0 | i2 rel_error 0.0091 > 0.005 | 0 |
| Standard #3 | 0.0 | i0 rel_error 0.0182 > 0.005 | 0 |
| Cheat | 0.0 | i2 timeout at 180s | 0 |

## Replay baselines

| Artifact | Before hardening | After hardening |
|---|---|---|
| codex_pass_1 | 1.0 | 0.0 (i2 error 0.00636) |
| cheat_codex_pass_1 | 1.0 | 0.0 (i2 error 0.00610) |

## Blog series

- [Part 1: Hybrid Retrieval Fusion](https://shkumbins.dev/blog/hybrid-retrieval-fusion-tb3)
- Part 2: The False Signal (thermal-stack-calibration)
- Part 3: The Loop That Finally Worked (this task — Codex arc)
