# Benchmark Hygiene Audit

## GitHub / submission state

| Property | Value |
|----------|-------|
| Repo URL | `github.com/beme08/Thermal-Pulse-Solver-TB3` |
| Visibility | PRIVATE |
| Default branch | `main` |
| Audited branch | `wip/multi-instance-verifier` |
| Audited commit | `6fdb66c62334e462c37d319f4157d1f035425dff` |
| Tag | `thermal-pulse-multi-instance-gate-v1` |
| This file | Pre-submission documentation (WIP) |
| Implementation files changed | No |
| Local vs remote | `wip/multi-instance-verifier` is 2 commits ahead of `origin/main`. The multi-instance verifier, solution artifact, tests, and trusted oracle are not yet pushed to `origin/main`. |

## Public/private split

### What is public?
- `environment/app/solution.cpp` — starter stub (emits zeros)
- `environment/app/thermal_oracle.hpp` — public oracle declarations only (9 function signatures: `heat_source`, `initial_temp`, `boundary_value`, `conductivity_x`, `conductivity_y`, `rho_cp`, `domain_T`, `heat_source_x`, `heat_source_y`). Note: `instance_freq()` and `exact_temp()` are NOT declared here.
- `environment/Dockerfile` — builds `/app` from `app/`
- `instruction.md` — agent-facing task description with one input/output JSON schema example
- `task.toml` — configuration

### What is private?
- `tests/thermal_oracle_impl.cpp` — trusted oracle implementation containing manufactured solution formulas, hidden parameters (freq, sharp, cx, cy, shape_b, amp, slow), and instance selection logic.
- `tests/test_thermal.py` — verifier logic: exact truth function (`exact_temp`), query point generation (seeded random, fixed sample times/locations), error threshold, shared-budget accounting
- `tests/thermal_oracle.hpp` — verifier-side oracle header (same declarations as public, but linked against the trusted implementation)
- `tests/Dockerfile` — builds verifier container
- `tests/baselines/` — empty (`.gitkeep`)
- `tests/sweeps/` — gate/tuning tooling (standalone ADI/CN solver, sweep runners, Docker wrappers)

### What is deterministic but hidden?
- Two private thermal instances: `freq=96.0` (instance 0) and `freq=192.0` (instance 1)
- RNG seed: `20260630 + 97 * instance` for query-point generation (768 points per instance)
- Fixed query times: `[0.137, 0.333, 0.591, 0.827]`
- Fixed query locations: `(0.21,0.35), (0.43,0.58), (0.67,0.72), (0.81,0.24)`
- Shared budget: 180s via `THERMAL_SHARED_BUDGET_SEC`

### Where private truth lives
- `tests/thermal_oracle_impl.cpp` (C++ compiled into verifier process, never in `/app`)
- `tests/test_thermal.py` (Python truth function)

### Confirmation private truth is not in /app
- Confirmed: `environment/app/` contains only `solution.cpp` (starter) and `thermal_oracle.hpp` (public declarations). No `thermal_oracle_impl.cpp`, no `test_thermal.py`, no hidden parameter values, no `instance_freq()`, no `exact_temp()`.

### Confirmation hidden eval outputs are not public
- Confirmed: The verifier generates expected temperatures on-the-fly from the hardcoded analytical formula. No pre-computed output file exists in the environment.

## Public examples

**Are public examples smoke tests only?** Yes. The `instruction.md` shows a single input/output JSON schema example with placeholder values (`{"temperatures": [1.234]}`). No `f*` value, no hidden eval points, no truth temperatures.

**Could public examples reveal the hidden parameter or full model?** No. The instruction explains the oracle is black-box. The input/output example conveys only the JSON schema.

**Could a solver fit public examples and pass hidden eval?** No. There is only one schema example with no numerical meaning. A solver must actually sample the oracle and solve the PDE.

**Are full hidden-style eval cases absent from public files?** Yes. No hidden case data, eval points, or truth temperatures appear in `environment/`, `instruction.md`, or any public file.

## Baseline gates

| Gate | Expected | Status |
|------|----------|--------|
| nop (starter solution emits zeros) | reward = 0.0 | Confirmed: verifier-nop-docker reports 0.0 |
| reference (oracle artifact) | reward = 1.0 | Confirmed: verifier-reference-docker reports 1.0 |
| coarse_dt (Nx=320, Ny=320, Nt=128) | reward = 0.0 | Confirmed: DESIGN.md shows rel-error 0.978 |
| explicit (Nx=320, Ny=320, Nt=4096, forward Euler) | reward = 0.0 or unstable | Confirmed: unstable fail |
| brute_overresolve (Nx=480, Ny=480, Nt=65536) | reward = 0.0 or timeout | Confirmed: 180.0s timeout |
| private f* not in /app | True | Confirmed |
| public examples not enough to infer f* | True | Confirmed |

**Known gap — fixed-grid overresolve leak:** At the reference spatial grid (Nx=Ny=320), blind high-Nt ADI (Nt=65536, 98304) can still pass under budget. Mitigated by shared-budget multi-instance evaluation (two instances under one 180s verifier budget causes the second instance to timeout). Final baseline gates for the full multi-instance verifier are still pending formal Harbor agent trials.

## Leakage checks

| Check | Result |
|-------|--------|
| Hidden frequencies / private params not in environment/app | PASS — freq=96/192, sharp, cx, cy, shape_b, amp, slow are only in `tests/thermal_oracle_impl.cpp` and `tests/test_thermal.py` |
| Hidden cases not copied into public data | PASS — no hidden eval data in `environment/` or `instruction.md` |
| Reference outputs not available to the agent | PASS — `tests/test_thermal.py` generates truth on-the-fly; no pre-computed output file |
| Solution files not in /app | PASS — `solution/solution.cpp` and `solution/solve.sh` are outside the environment build context |
| No answer keys in README/instruction/public docs | PASS — `instruction.md` has no truth values, no f* values, no eval coordinates |
| No hidden eval IDs or labels exposed publicly | PASS — instance IDs (0, 1) are passed via `THERMAL_INSTANCE` env var only at eval time |

## Development vs leaderboard / trial results

- Development sweeps (DESIGN.md, EXPERIMENTS.md, `tests/sweeps/`) are for task design and gate calibration. These are not trial results.
- Harbor standard/cheat trials have not yet been run. TRIAL_RESULTS.md records only local Docker verifier smoke checks (nop=0.0, reference=1.0).
- Invalid infra exceptions must be reported separately from model failures.
- Exceptions and verifier errors must be distinguished from legitimate reward outcomes.

## Audit result

**PASS WITH NOTES / WIP**

The public/private split is clean. Private truth lives only in the verifier container. Baseline gates are proven for nop, reference, coarse_dt, explicit, and brute_overresolve at the spatial-upgrade grid. The known fixed-grid overresolve leak is documented in DESIGN.md and mitigated by shared-budget multi-instance evaluation. Full Harbor agent trials are pending, and final baseline gates should be confirmed after those trials.
