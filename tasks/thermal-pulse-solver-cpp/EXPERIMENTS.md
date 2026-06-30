# Experiments

This file records gate and sweep measurements before full task implementation.
Harbor standard/cheat runs belong in `TRIAL_RESULTS.md` and `docs/harbor-runs.md`.

The earliest gate measurements were taken before the first commit. Newer
implementation smoke checks were taken on a dirty worktree based on commit
`b36e9fb`, so rows use `pending commit` until the current task tree is committed
and checksummed.

```text
task checksum: not stamped; compute from committed task tree before Harbor runs
```

Suggested checksum command once the task tree is ready:

```bash
find tasks/thermal-pulse-solver-cpp -type f \
  ! -path '*/.build/*' \
  ! -path '*/__pycache__/*' \
  -print0 | sort -z | xargs -0 shasum -a 256 | shasum -a 256
```

## Run Log

| job name | agent | model | reasoning config | task checksum | git commit | standard vs cheat | reward | exceptions | runtime | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| overresolve-gate-initial-local | Codex | GPT-5 Codex | default | not stamped | none | sweep | n/a | none | brute 149.494s | fail | Initial local preview: reference passed in 4.222s, coarse silently wrong, explicit unstable, but `400^2,Nt=65536` brute passed under 180s. |
| overresolve-gate-initial-docker | Codex | GPT-5 Codex | default | not stamped | none | sweep | n/a | none | brute 161.954s | fail | Docker/Colima confirmed the leak: `400^2,Nt=65536` brute passed under 180s with low error. |
| overresolve-gate-retuned-docker | Codex | GPT-5 Codex | default | not stamped | none | sweep | n/a | none | 180.000s timeout | weak pass | `480^2,Nt=65536` timed out, but this was later rejected as insufficient because brute grid was larger than reference grid. |
| fixed-grid-brute-nt-docker | Codex | GPT-5 Codex | default | not stamped | none | sweep | n/a | none | crossover at 180.000s | fail | At `Nx=Ny=320`, blind `Nt=65536` and `Nt=98304` both passed under 180s. Fixed-grid brute can still win. |
| fixed-grid-f2-docker | Codex | GPT-5 Codex | default | not stamped | none | sweep | n/a | none | crossover at 180.000s | fail | Doubling frequency to `f*=192` did not close the fixed-grid leak; blind high-`Nt` still passed under budget. |
| multi-instance-shared-budget-docker | Codex | GPT-5 Codex | default | not stamped | none | sweep | n/a | none | reference 19.378s; brute timeout 180.000s | conditional pass | Two deterministic instances close the runtime gap only under one shared 180s verifier budget. Not a gate closer for per-instance budgets. |
| tb3-budget-pattern-inspection | Codex | GPT-5 Codex | default | not stamped | none | design check | n/a | none | n/a | pass | Local TB3-style tasks confirm one `test.sh`/verifier process can loop over multiple private cases under one verifier timeout. |
| verifier-nop-docker | Codex | GPT-5 Codex | default | not stamped | none | verifier smoke | 0.0 | expected nonzero verifier exit | <1s | pass | Mounted starter app emits zeros; verifier reports instance 0 relative error 1 and reward 0.0. |
| verifier-reference-docker-invalid | Codex | GPT-5 Codex | default | not stamped | none | verifier smoke | 1.0 | none | 0.458s | invalid | Rejected: reference artifact evaluated the manufactured formula directly and matched verifier truth to machine precision. |
| verifier-reference-docker-numerical | Codex | GPT-5 Codex | default | not stamped | pending commit | verifier smoke | 1.0 | none | 18.840s | pass | Numerical ADI/CN reference uses only public oracle calls; nonzero discretization errors `5.95e-4`, `6.22e-4`. |
| multi-instance-real-solver-table-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | sweep | reference 1.0; brute/coarse/explicit 0.0 | none | brute timeout 180.000s | pass | Real numerical reference fits; blind high-`Nt` brute times out under shared budget; coarse and explicit fail. |
| single-instance-brute-margins-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | sweep | n/a | none | 99.996s-101.589s | leak-risk | Each private instance passes individually with blind `Nt=65536`, so the design relies on one shared verifier budget rather than per-instance timeouts. |
| three-instance-real-solver-table-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | sweep | reference 1.0; brute/coarse/explicit 0.0 | none | reference 25.039s; brute timeout 180.000s | pass | Three deterministic private instances preserve reference margin and make the shared-budget brute timeout robust enough for the final candidate. |

## Key Tables

### Initial Docker Gate, sharp=18

| case | Nx | Ny | Nt | wall-clock | rel-error | status | result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| brute_overresolve | 400 | 400 | 65536 | 161.954s | 2.39745e-05 | ok | pass |

Classification: fail. Brute force fit under the budget.

### Retuned Docker Gate, sharp=36

| case | Nx | Ny | Nt | wall-clock | rel-error | status | result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| reference | 320 | 320 | 4096 | 6.429s | 0.000509878 | ok | pass |
| coarse_dt | 320 | 320 | 128 | 0.203s | 0.978019 | ok | fail |
| explicit | 320 | 320 | 4096 | 0.008s | n/a | unstable | fail |
| brute_overresolve | 480 | 480 | 65536 | 180.000s | n/a | timeout | timeout |

Classification: weak pass only. Later fixed-grid sweep showed this did not close the real leak.

### Fixed-Grid Brute Nt Sweep, f*=96, Nx=Ny=320

| Nt | wall-clock | rel-error | result |
| --- | --- | --- | --- |
| 4096 | 6.534s | 0.000509878 | pass |
| 65536 | 103.163s | 6.57278e-05 | pass |
| 98304 | 154.199s | 6.59897e-05 | pass |
| 131072 | 180.000s | n/a | timeout |

Classification: fail. Blind high-`Nt` ADI at the reference grid passes under budget.

### Fixed-Grid Brute Nt Sweep, f*=192, Nx=Ny=320

| Nt | wall-clock | rel-error | result |
| --- | --- | --- | --- |
| 4096 | 6.450s | 0.00238011 | pass |
| 8192 | 12.925s | 0.000526978 | pass |
| 16384 | 25.859s | 0.000214958 | pass |
| 32768 | 52.050s | 7.59195e-05 | pass |
| 65536 | 103.683s | 7.41729e-05 | pass |
| 98304 | 154.252s | 7.36856e-05 | pass |
| 131072 | 180.000s | n/a | timeout |

Classification: fail. The f*=2 discriminator did not close the gate cleanly.

### Multi-Instance Shared-Budget Sweep

Budget model: one shared 180s wall-clock across both instances.

| sequence | instance | Nx | Ny | Nt | wall-clock | rel-error | result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| reference | f*=96 | 320 | 320 | 4096 | 6.550s | 0.000509878 | pass |
| reference | f*=192 | 320 | 320 | 8192 | 12.829s | 0.000526978 | pass |
| brute | f*=96 | 320 | 320 | 65536 | 104.319s | 6.57278e-05 | pass |
| brute | f*=192 | 320 | 320 | 65536 | 75.680s remaining | n/a | timeout |

Classification: conditional pass. Valid only if the final verifier uses one shared budget across all private instances.

### Invalid Exact Reference Check

An earlier reference smoke returned machine-precision error and was rejected.
The artifact contained the manufactured spatial/temporal formula directly and
selected the hidden instance frequency through `THERMAL_INSTANCE`. That path was
not a numerical solver and is no longer treated as evidence.

### Verifier Smoke Checks

The implemented verifier follows the shared-budget local task pattern:

- one `tests/test.sh` invocation;
- one Python verifier phase;
- three deterministic private instances;
- one shared 180s verifier budget passed through `THERMAL_SHARED_BUDGET_SEC`;
- trusted oracle header restored into a private build directory before
  compiling `/app/solution.cpp`.

Docker results:

```text
starter /app solution -> reward 0.0, instance 0 relative error 1
numerical reference artifact -> reward 1.0
  instance 0 rel-error 0.000594920
  instance 1 rel-error 0.000621576
  instance 2 rel-error 0.00117984
  total runtime 25.039s in the mode sweep
```

### Single-Instance Brute Margins

Docker/Colima, one private instance per run, `Nx=Ny=320`, `Nt=65536`.

| instance | private frequency | wall-clock | rel-error | status |
| --- | --- | --- | --- | --- |
| 0 | 96 | 101.589s | 6.36695e-05 | pass |
| 1 | 192 | 99.996s | 6.71011e-05 | pass |
| 2 | 128 | 101.105s | 7.54524e-05 | pass |

Classification: leak-risk for any per-instance 180s budget. The final
candidate therefore requires the documented shared verifier budget across all
private instances.

### Real Three-Instance Solver Table

Docker/Colima, one shared 180s budget, `Nx=Ny=320`.

| solver | instances | grid | Nt | total wall-clock | per-instance error | status | reward |
| --- | --- | --- | --- | --- | --- | --- | --- |
| reference | 3 | 320x320 | adaptive | 25.039s | i0:0.00059492, i1:0.000621576, i2:0.00117984 | pass | 1.0 |
| brute_overresolve | 3 | 320x320 | 65536 | 180.000s | i0:6.36695e-05, i1:timeout | fail | 0.0 |
| coarse_dt | 3 | 320x320 | 128 | 0.658s | i0:1.14476, i1:0.530429, i2:11.015 | fail | 0.0 |
| explicit | 3 | 320x320 | 4096 | 0.064s | i0:error, i1:error, i2:error | fail | 0.0 |
