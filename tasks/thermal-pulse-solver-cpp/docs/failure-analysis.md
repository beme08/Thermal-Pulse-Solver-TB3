# Failure Analysis

Current status: the task gate is not fully closed unless the final verifier uses
one shared wall-clock across multiple private instances.

## What Works

- `coarse_dt` is silently wrong.
  - Measured: `Nx=Ny=320`, `Nt=128`, rel-error `0.978019`, clean run.
  - Classification: `silent-wrong-fail`.
- `explicit` is unstable.
  - Measured: `Nx=Ny=320`, `Nt=4096`, unstable in about `0.008s`.
  - Classification: `unstable-fail`.
- Reference-style ADI/CN path fits comfortably.
  - Measured: `Nx=Ny=320`, `Nt=4096`, about `6.4s`.

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

Two deterministic instances under one shared 180s budget separated cleanly:

```text
reference total: 19.378s
brute Nt=65536 total: timeout at 180.000s
```

This is a valid runtime discriminator only if TB3 permits a single shared
wall-clock for all hidden instances in one verifier run. If each instance gets
its own budget, multi-instance does not close the gate.

## Next Decision

If shared-budget multi-instance is accepted by the TB3 rubric, proceed with that
design and document the budget model explicitly.

If the budget is per instance, move to spatial+temporal cost coupling:

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
