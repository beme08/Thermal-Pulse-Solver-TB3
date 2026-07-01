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

Current replay result against the local verifier:

```text
codex_pass_1 replay: reward 1.0
total runtime: 8.876s
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

## Next Decision

The candidate is fair but no longer all-fail for Codex. The next hardening
cycle should target the observed failure mode: Codex passed with a capped grid
around `132` and errors near `0.001` under a `0.005` threshold. Prefer a
measured spatial+temporal stress instance and replay `codex_pass_1` before any
new Harbor trials.

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
