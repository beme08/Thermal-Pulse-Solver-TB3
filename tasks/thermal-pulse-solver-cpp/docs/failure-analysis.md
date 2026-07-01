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
