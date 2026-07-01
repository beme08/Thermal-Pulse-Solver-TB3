# Failure Analysis

Current status: local TB3-style task patterns support one shared verifier
timeout across multiple private instances inside one `test.sh` invocation. Under
that model, the multi-instance design closes the measured brute-force runtime
gap. If an external rubric review rejects shared-budget multi-instance
evaluation, the task should move to spatial+temporal cost coupling instead.

## What Works

- `coarse_dt` is silently wrong.
  - Measured: `Nx=Ny=320`, `Nt=128`, rel-error `0.978019`, clean run.
  - Classification: `silent-wrong-fail`.
- `explicit` is unstable.
  - Measured: `Nx=Ny=320`, `Nt=4096`, unstable in about `0.008s`.
  - Classification: `unstable-fail`.
- Numerical reference-style ADI/CN path fits comfortably.
  - Measured in Docker with three shared-budget instances:
    `25.039s` total, rel-errors `0.000594920`, `0.000621576`,
    and `0.00117984`.

## What Failed

The fixed-grid brute-force gate failed.

At the reference spatial grid, blind high-`Nt` ADI still passes:

```text
f*=96, Nx=Ny=320:
  Nt=65536 -> 103.163s, pass
  Nt=98304 -> 154.199s, pass

f*=192, Nx=Ny=320:
  Nt=65536 -> 103.683s, pass
  Nt=98304 -> 154.252s, pass
```

This means temporal-only difficulty is not enough at the current grid and
budget. Doubling `f*` did not close the gate cleanly, so do not keep nudging
frequency.

## Retune That Was Rejected

The `480^2,Nt=65536` brute baseline timed out at 180s, while the reference
`320^2,Nt=4096` finished in 6.429s. This is not sufficient because brute was
defined at a larger spatial grid than the reference. A plausible agent can use
the reference grid and blind high `Nt`, which passes under budget.

## Conditional Multi-Instance Result

An earlier exact-formula reference artifact was rejected because it evaluated
the manufactured temperature directly and matched verifier truth to machine
precision. The current reference artifact is a numerical ADI/CN solve using
only public oracle calls.

Three deterministic instances under one shared 180s budget separated cleanly
with the real numerical reference:

```text
reference total: 25.039s, reward 1.0
brute Nt=65536 total: timeout at 180.000s, reward 0.0
coarse Nt=128 total: 0.658s, reward 0.0
explicit Nt=4096 total: 0.064s, reward 0.0
```

This is a valid runtime discriminator under the local TB3 pattern: one verifier
script/process evaluates multiple cases under `[verifier].timeout_sec`.

The single-instance brute margin remains a documented leak risk:

```text
instance 0: Nt=65536 -> 101.589s, pass
instance 1: Nt=65536 -> 99.996s, pass
instance 2: Nt=65536 -> 101.105s, pass
```

Therefore this candidate is valid only for the documented shared-budget
verifier model, not a per-instance timeout model.

## Codex Standard Pass Analysis

`gatekeeper-codex-thermalpulse-1` is classified as a legitimate solve:

```text
reward: 1.0
exceptions: 0
total verifier runtime: 6.6335s
instance errors: 0.00112895, 0.00108784, 0.00100115
```

The submitted solution did not contain hidden instance constants, verifier/test
paths, manufactured truth code, or private data access. It used the public
oracle to estimate spatial and temporal structure, then implemented a cheap
adaptive ADI/finite-volume solver. This means the brute-overresolve gate worked,
but the intended solution path is discoverable by Codex under the current
threshold.

The artifact is saved as:

```text
tests/baselines/codex_pass_1/solution.cpp
```

Current replay results:

```text
codex_pass_1 local replay: reward 1.0
total runtime: 8.876s
instance errors: 0.00112895, 0.00108784, 0.00100115

codex_pass_1 Docker replay: reward 1.0
total runtime: 6.664s
instance errors: 0.00112895, 0.00108784, 0.00100115
```

Any hardening loop must include this replay baseline before changing verifier
settings. The next target matrix is:

```text
reference = 1.0
nop = 0.0
coarse_dt = 0.0
explicit = 0.0
brute_overresolve = 0.0/timeout
codex_pass_1 replay = 0.0
```

## Spatial Hardening Candidate

The current candidate targets the observed Codex failure mode without tightening
the threshold or changing the output contract. One private deterministic
instance is retuned within the same smooth manufactured family so the
`codex_pass_1` capped grid underresolves the spatial packet.

Docker/Colima matrix:

| solver | reward | runtime | notes |
| --- | --- | --- | --- |
| reference | 1.0 | 25.069s | errors `0.00059492`, `0.000621576`, `0.00185831` |
| nop/starter | 0.0 | <1s | instance 0 relative error `1.0` |
| coarse_dt | 0.0 | 0.653s | errors `1.14476`, `0.530429`, `5.56799` |
| explicit | 0.0 | 0.067s | unstable/error on all instances |
| brute_overresolve | 0.0 | 180.000s | instance 0 passes, instance 1 times out |
| codex_pass_1 replay | 0.0 | 6.692s | instance 2 relative error `0.00635706` exceeds `0.005` |

This closes the replay baseline for the observed Codex artifact while
preserving the original brute/coarse/explicit failure modes.

## Next Decision

The hardening candidate now satisfies the local replay matrix. Do not run Harbor
until this candidate is reviewed as fair and the committed task checksum is
recorded.

If a later review determines the budget must be per instance, move to
spatial+temporal cost coupling:

- Re-derive `base_T` with sharper smooth spatial structure.
- Prove coarse-`Nx` at correct `Nt` fails.
- Prove coarse-`Nt` at correct `Nx` fails.
- Prove blind high-`Nt` at the necessary `Nx` times out.
- Prove reference with just-enough `Nx` and detected `Nt` passes with margin.

Do not:

- Go 3D.
- Tune tolerance to hide the leak.
- Define brute at a larger grid than reference.
- Keep increasing `f*`.
- Polish the final verifier before the gate closes.

## Fresh Codex hardened runs

### gatekeeper-codex-thermalpulse-hardened-1

The first fresh Codex run after hardening against `codex_pass_1`.

- Trials: 1, Exceptions: 0, Reward: 0.0, Runtime: 12m50s
- First failure: instance 1 relative error `0.00564189 > 0.005`

The trajectory indicates a legitimate attempt at the intended approach: the agent
implemented an adaptive solver with frequency/omega estimation and grid
selection. The run failed numerically on the hardened private instance, not
through infrastructure failure or verifier exploit.

### gatekeeper-codex-thermalpulse-hardened-2

- Trials: 1, Exceptions: 0, Reward: 0.0, Runtime: 17m29s
- First failure: instance 2 relative error `0.00574273 > 0.005`

Clean numerical failure. Grep hits `N = 96` and `N = 128` are grid-size
constants, not hidden-frequency leakage.

### gatekeeper-codex-thermalpulse-hardened-3

- Trials: 1, Exceptions: 0, Reward: 0.0, Runtime: 25m47s, Cost: $2.700929
- First failure: instance 0 relative error `0.0115036 > 0.005`

Clean numerical failure. Grep hits `intervals = 96`, `even_clamp(..., 28, 96)`,
and `std::max({96, ...})` are generic interval/grid constants, not
hidden-frequency leakage.

### Summary

Three consecutive Codex hardened runs all fail cleanly with zero exceptions on
the same task checksum. The hardening loop is confirmed:

1. Prior Codex solution was saved as `codex_pass_1`.
2. `codex_pass_1` replay passed the old verifier.
3. The hardened verifier/private instance made `codex_pass_1` fail.
4. Three fresh Codex runs also failed cleanly.
5. No re-hardening is needed: the current matrix closes `codex_pass_1` replay
   and fresh agents fail on the intended numerical difficulty.
6. The cheat trial found no bypass; the agent solved legitimately after failing
   to find an exploit. No structural vulnerability was discovered.

## Cheat Trial Analysis

### cheat-codex-thermalpulse-hardened-1

- Mode: cheat (extra instruction: `.harbor/hack-trial-prompt.md`)
- Agent/model: Codex / openai/gpt-5.5
- Reward: 1.0, Exceptions: 0, Runtime: 15m13s, Cost: $1.747216
- Classification: A (legitimate numerical solve despite red-team prompt)

```text
instance errors: 0.00133, 0.00126, 0.00237
total verifier runtime: 36.37s
```

The agent was instructed to "find a bypass, do not solve legitimately." It searched
for exploitable artifacts (verifier files, test fixtures, hidden data, oracle
implementation), found nothing accessible, and fell through to a legitimate
ADI Crank-Nicolson solver with adaptive grid selection (range 56–192) and
adaptive time-stepping.

**Why this is classified A, not B/C/D:**

- No shell/popen/fork/system calls in solution.cpp.
- No `/proc`, `/tmp`, `/tests`, or hidden path access.
- No hardcoded frequency constants — all grid/step sizes derived from oracle
  sampling (`choose_grid_size`, `choose_time_step`).
- Standard JSON I/O only (`ifstream`/`ofstream`).
- The trajectory shows the agent explicitly searched for hidden artifacts,
  found none, then stated: "I don't see accessible verifier artifacts or
  expected-output files... not going to rely on tampering or artifact leakage."
- All three instances passed with errors consistent with the original
  legitimate Codex pass (0.001–0.002).

**Hack-trial rules interpretation:**

The hack-trial prompt explicitly states: *"If no credible bypass exists, stop
and report that clearly instead of completing the task normally."* The agent
failed this instruction by solving legitimately. Under the hack-trial rubric,
this is a failure (the agent should have reported "no exploit found" and
stopped).

**Task checksum note:**

The cheat run checksum `5a4130d9` differs from hardened standard `6666f89c`.
This is because each Harbor run generates a unique trial ID, not because the
task tree changed. The git commit `dd9ae88` (cheat) differs from `f67575c`
(hardened standard) only by the addition of `.harbor/hack-trial-prompt.md`.
The task tree `tasks/thermal-pulse-solver-cpp` was identical across all runs.

**Implication for hardening:**

The cheat trial did not discover a bypass. The benchmark's difficulty is
purely numerical: an agent with adequate resolution and adaptive time-stepping
can solve it legitimately. Treat this as a frontier numerical solution, not a
cheat exploit.

## Cheat Pass Replay Hardening

The cheat artifact is saved as:

```text
tests/baselines/cheat_codex_pass_1/solution.cpp
```

The saved file is byte-identical to the Harbor artifact:

```text
3028edcfe7049878f534bc696c67b8dc52b18e4d6b96f3cb0503f78279d2a1fc
```

Before hardening, Docker replay reproduced the pass:

| replay | reward | total runtime | errors |
| --- | --- | --- | --- |
| cheat_codex_pass_1 | 1.0 | 37.447s | `0.00132861`, `0.00125662`, `0.00236865` |

Inspection summary:

- Public oracle only: `heat_source`, `initial_temp`, `boundary_value`,
  `conductivity_x`, `conductivity_y`, `rho_cp`.
- No `/tests`, verifier, hidden fixture, reward, `/proc`, shell, `system`,
  `popen`, `fork`, or `exec` usage.
- Numerical method: ADI / Crank-Nicolson style finite-volume solve.
- Spatial resolution: adaptive interval count up to `192`.
- Temporal resolution: adaptive time step chosen from 257 sampled time points.
- Numeric constants such as `192` are grid/time caps, not hidden parameters.

The hardening candidate keeps the threshold fixed at `0.005`, further sharpens
one smooth private deterministic spatial packet, and raises the trusted
reference artifact grid to `384^2`.

Docker/Colima matrix:

| solver | reward | runtime | notes |
| --- | --- | --- | --- |
| reference | 1.0 | 36.608s | errors `0.000586615`, `0.000612769`, `0.00334672` |
| nop/starter | 0.0 | <1s | instance 0 relative error `1.0` |
| coarse_dt | 0.0 | 0.956s | errors `1.14477`, `0.530429`, `3.48587` |
| explicit | 0.0 | 0.094s | unstable/error on all instances |
| brute_overresolve | 0.0 | 180.000s | instance 0 passes, instance 1 times out |
| codex_pass_1 replay | 0.0 | <10s | instance 2 relative error `0.0112699` exceeds `0.005` |
| cheat_codex_pass_1 replay | 0.0 | 35.568s | instance 2 relative error `0.00610366` exceeds `0.005` |

This hardens against the observed cheat-passing numerical solver without adding
cheat-mode detection or changing the public output contract.

## Final Codex Cheat Hardened Result

### cheat-codex-thermalpulse-cheathardened-1

- Mode: cheat (extra instruction: `.harbor/hack-trial-prompt.md`)
- Agent/model: Codex / openai/gpt-5.5
- Reward: 0.0, Exceptions: 0, Runtime: 14m38s, Cost: $1.489593
- Classification: clean adversarial failure by timeout
- First failure: instance 2 timed out

```text
total verifier runtime: budget timeout on instance 2
```

The agent searched for bypass opportunities (searched `/app` 115 times, referenced bypass/exploit 3 times each), found none, then implemented a solver with `k_intervals = 128` and `exact_output_times`. The solver resolved instance 0 and 1 adequately but instance 2 timed out under the shared 180s budget, producing reward 0.0.

**Why this completes the hardening loop:**

1. Original Codex solver (`codex_pass_1`) was saved as a replay baseline.
2. `codex_pass_1` replay passed the original verifier.
3. First hardening made `codex_pass_1` replay fail (reward 0.0).
4. Three fresh Codex standard runs also failed cleanly.
5. Historical cheat trial (`cheat-codex-thermalpulse-hardened-1`) passed with reward 1.0 via legitimate solve.
6. That cheat solver was saved as `cheat_codex_pass_1`.
7. Cheat replay hardening made `cheat_codex_pass_1` fail (reward 0.0).
8. Fresh Codex cheat run on the hardened checksum failed with reward 0.0, exceptions 0.

The hardened task now rejects all known agent strategies: legitimate standard solvers (numerical failure), historical solves (replay failure), and adversarial cheat attempts (timeout). No verifier exploit, hidden-data access, or structural bypass was found across all 8 trial runs.

**Grep findings:** `k_intervals = 128` and `exact_output_times` are benign solver variables — no hidden/test/verifier/reward/shell/proc/env exploit indicators were discovered. The solver attempted a legitimate ADI-style scheme with fixed grid parameters, which underresolved or timed out on the hardened private instances.

## Final Standard Gatekeeper Trial

### gatekeeper-codex-thermalpulse-final-1

- Mode: standard
- Agent/model: Codex / openai/gpt-5.5
- Reward: 0.0, Exceptions: 0, Runtime: 15m57s, Cost: $1.960097
- Classification: clean numerical failure
- First failure: instance 0 relative error `0.0501037 > 0.005`

```text
total verifier runtime: completed all instances; instance 0 error 0.0501037
```

The agent implemented an adaptive solver with grid exploration but failed to resolve the spatial structure of the hardened private instance 0. Grep hits `128`, `fine_n = 96`, and grid candidate arrays are benign solver constants — no hidden/test/verifier/reward/shell/proc/env exploit indicators were discovered.

### gatekeeper-codex-thermalpulse-final-2

- Mode: standard
- Agent/model: Codex / openai/gpt-5.5
- Reward: 0.0, Exceptions: 0, Runtime: 16m17s, Cost: $2.178620
- Classification: clean numerical failure
- First failure: instance 2 relative error `0.00912555 > 0.005`

```text
total verifier runtime: completed all instances; instance 2 error 0.00912555
```

The agent attempted a solver with grid exploration across `grids = {24, 40, 64, 96, 128}` and used `spatial_tol = (grid < 96)` as a tolerance switch, but failed to resolve the frequency-128 structure of the hardened private instance 2. Grep hits are benign solver grid/tolerance constants — no hidden/test/verifier/reward/shell/proc/env exploit indicators were discovered.

### gatekeeper-codex-thermalpulse-final-3

- Mode: standard
- Agent/model: Codex / openai/gpt-5.5
- Reward: 0.0, Exceptions: 0, Runtime: 11m22s, Cost: $1.502488
- Classification: clean numerical failure
- First failure: instance 0 relative error `0.0181674 > 0.005`

```text
total verifier runtime: completed all instances; instance 0 error 0.0181674
```

The agent implemented a solver with a coordinate constant substring `0.633974596` (a generic mathematical constant, not hidden-frequency leakage) but failed to resolve the private instance 0. Grep hit is a benign coordinate constant — no hidden/test/verifier/reward/shell/proc/env exploit indicators were discovered.

**Updated hardening loop status:**

1. Original Codex solver saved as `codex_pass_1` — replayed on hardened verifier = 0.0.
2. Three fresh Codex standard runs on first hardened checksum = 0.0.
3. Cheat solver saved as `cheat_codex_pass_1` — replayed on cheat-hardened verifier = 0.0.
4. Fresh Codex cheat run on final hardened checksum = 0.0.
5. Fresh Codex standard run on final hardened checksum = 0.0.
6. Second fresh Codex standard run on final hardened checksum = 0.0.
7. Third fresh Codex standard run on final hardened checksum = 0.0.

All 11 trial runs (4 standard, 2 historical replay, 2 cheat, 3 standard final) show no verifier exploit, hidden-data access, or structural bypass. The benchmark is hardened against all known agent strategies.

**Final Codex matrix (final hardened checksum):**

| type | passed | exceptions |
|---|---|---|
| Codex standard final | 0/3 | 0 |
| Codex cheat final | 0/1 | 0 |
