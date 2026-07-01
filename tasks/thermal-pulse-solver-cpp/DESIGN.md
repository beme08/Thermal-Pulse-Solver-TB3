# thermal-pulse-solver-cpp design checkpoint

This task is gated by the Section 6.0 over-resolve measurement before any final task polish.

The manufactured problem is a 2D transient heat equation on the unit square:

```text
rho_cp * T_t = d/dx(kx(x) T_x) + d/dy(ky(y) T_y) + q(x,y,t)
```

The truth used by the current gate is:

```text
T(x,y,t) = S(x,y) * D(t)
D(t) = (1 + slow*t) * (1 + a*sin(2*pi*f*t))
S(x,y) = sin(pi*x) sin(pi*y)
       * (1 + b*cos(2*pi*x)*cos(2*pi*y))
       * exp(-sharp*((x-cx)^2 + (y-cy)^2))
```

`q` is computed exactly from the manufactured solution:

```text
q = rho_cp*S*D'(t) - D(t) * (d/dx(kx*S_x) + d/dy(ky*S_y))
```

The first gate is implemented in `tests/sweeps/run_overresolve_gate.py` and
`tests/sweeps/thermal_gate.cpp`. It measures four cases:

- `reference`: ADI/CN-style solve at `Nx=Ny=320`, `Nt=4096`.
- `coarse_dt`: same stable solver at `Nx=Ny=320`, `Nt=128`.
- `explicit`: forward Euler at `Nx=Ny=320`, `Nt=4096`.
- `brute_overresolve`: ADI/CN-style solve at `Nx=Ny=480`, `Nt=65536`.

The original Section 6.0 gate is useful but not sufficient by itself. The gate
is viable only if the reference-like solve fits comfortably under the budget
while plausible blind over-resolution at the reference spatial grid also loses.

Initial measurement with `sharp=18`, reference `256^2`, and brute `400^2`
failed the gate: brute completed under 180s. The current gate is the first
retune: `sharp=36`, reference `320^2`, brute `480^2`.

Measured Docker/Colima results:

```text
sharp=18, reference 256^2, brute 400^2:
  brute_overresolve 400^2, Nt=65536 -> 161.954s, rel-error 2.39745e-05, pass

sharp=36, reference 320^2, brute 480^2:
  reference 320^2, Nt=4096 -> 6.429s, rel-error 0.000509878, pass
  coarse_dt 320^2, Nt=128 -> 0.203s, rel-error 0.978019, silent fail
  explicit 320^2, Nt=4096 -> 0.008s, unstable fail
  brute_overresolve 480^2, Nt=65536 -> 180.000s, timeout
```

The stricter fixed-reference-grid diagnostic is now the controlling result:

```text
Fixed-grid brute Nt sweep at Nx=Ny=320:
  Nt=4096   -> 6.534s,   rel-error 0.000509878, pass
  Nt=65536  -> 103.163s, rel-error 6.57278e-05, pass
  Nt=98304  -> 154.199s, rel-error 6.59897e-05, pass
  Nt=131072 -> 180.000s, timeout
```

That is a leak signal: blind high-`Nt` ADI at the same spatial grid as the
reference can pass under the 180s budget. Do not proceed to final verifier/task
implementation until temporal difficulty or tolerance is retuned and this
fixed-grid gate fails for blind over-resolution while the intended reference
still fits.

The spatial sweep at fixed `Nt=4096` shows the current `320^2` reference error
is not primarily spatial:

```text
Nx=192 -> rel-error 0.000522508
Nx=256 -> rel-error 0.000510944
Nx=320 -> rel-error 0.000509878
Nx=384 -> rel-error 0.000510444
Nx=448 -> rel-error 0.000511173
```

## f*=2 fixed-grid discriminator

One cheap temporal retune was measured with `f*=192`, `sharp=36`, and
`Nx=Ny=320`.

```text
Fixed-grid brute Nt sweep at Nx=Ny=320:
  Nt=4096   -> 6.450s,   rel-error 0.00238011,  pass
  Nt=8192   -> 12.925s,  rel-error 0.000526978, pass
  Nt=16384  -> 25.859s,  rel-error 0.000214958, pass
  Nt=32768  -> 52.050s,  rel-error 7.59195e-05, pass
  Nt=65536  -> 103.683s, rel-error 7.41729e-05, pass
  Nt=98304  -> 154.252s, rel-error 7.36856e-05, pass
  Nt=131072 -> 180.000s, timeout

First passing Nt: 4096
Fixed-grid error floor: 7.36856e-05
Crossover: Nt=131072
```

This does not cleanly close the gate. Blind high-`Nt` ADI still passes under
budget at `Nt=65536` and `Nt=98304`, and the first passing solve is still well
under the 30s reference-margin threshold. Do not keep increasing `f*`; move to
the next levers.

## Multi-instance shared-budget discriminator

The current local `task.toml` has a single `run_seconds = 180`. Multi-instance
separation is only a valid gate lever if the final verifier evaluates all
private instances inside one shared task timeout. It is not a gate closer if the
final harness gives every instance a fresh 180s budget.

Preliminary Docker/Colima result with two deterministic instances under one
shared 180s budget:

```text
Reference multi-instance sequence:
  f*=96,  Nx=320, Nt=4096 -> 6.550s,  rel-error 0.000509878, pass
  f*=192, Nx=320, Nt=8192 -> 12.829s, rel-error 0.000526978, pass
  total -> 19.378s, pass

Blind brute multi-instance sequence at Nt=65536:
  f*=96,  Nx=320, Nt=65536 -> 104.319s, rel-error 6.57278e-05, pass
  f*=192, Nx=320, Nt=65536 -> timeout after remaining 75.680s
  total -> 180.000s, timeout
```

Shared-budget multi-instance separation passes as a runtime discriminator.
Local TB3-style task patterns confirm this is legitimate: `hidden-resonance-sysid`
uses one `test.sh` invocation, one unittest process, and loops over three
private instances inside that verifier process under `[verifier].timeout_sec`.
`hybrid-retrieval-fusion` similarly evaluates generated hidden banks inside a
single verifier invocation. The thermal verifier now follows that pattern:
one `tests/test.sh`, one verifier phase, multiple deterministic private
instances, and one shared 180s wall-clock.

The final candidate uses three deterministic private instances because the
three-instance robustness check preserves reference margin while increasing
blind over-resolution cost.

Single-instance brute-overresolve margins at `Nx=Ny=320`, `Nt=65536`:

| instance | private frequency | wall-clock | rel-error | status |
| --- | --- | --- | --- | --- |
| 0 | 96 | 101.589s | 6.36695e-05 | pass |
| 1 | 192 | 99.996s | 6.71011e-05 | pass |
| 2 | 128 | 101.105s | 7.54524e-05 | pass |

This documents the budget-model dependency: a per-instance 180s budget would
leak because each blind high-`Nt` solve passes individually.

If a later external rubric review rejects shared-budget multi-instance
evaluation, skip this as a gate lever and move to spatial+temporal cost
coupling.

## Numerical reference validation

An interim reference artifact was rejected because it evaluated the
manufactured temperature formula directly and matched verifier truth to
machine precision. The current reference artifact is a numerical ADI/CN-style
solver. It uses only public oracle calls:

- `heat_source`
- `initial_temp`
- `boundary_value`
- `conductivity_x`
- `conductivity_y`
- `rho_cp`
- `domain_T`

It does not call a verifier truth function or contain the hidden instance
frequencies. The source term is reconstructed from public `heat_source` samples
using the observed low-rank time structure, then advanced on a `320^2` grid.

Docker/Colima final three-instance table under one shared 180s verifier budget:

| solver | instances | grid | Nt | total wall-clock | per-instance error | status | reward |
| --- | --- | --- | --- | --- | --- | --- | --- |
| reference | 3 | 320x320 | adaptive | 25.039s | i0:0.00059492, i1:0.000621576, i2:0.00117984 | pass | 1.0 |
| brute_overresolve | 3 | 320x320 | 65536 | 180.000s | i0:6.36695e-05, i1:timeout | fail | 0.0 |
| coarse_dt | 3 | 320x320 | 128 | 0.658s | i0:1.14476, i1:0.530429, i2:11.015 | fail | 0.0 |
| explicit | 3 | 320x320 | 4096 | 0.064s | i0:error, i1:error, i2:error | fail | 0.0 |

## Codex pass hardening candidate

`codex_pass_1` was a legitimate adaptive oracle-sampling ADI/finite-volume
solve that capped its grid near `132^2` and passed the previous verifier with
errors around `0.001`. The hardening candidate retunes one private deterministic
instance within the same smooth manufactured family so that this coarse spatial
grid underresolves the packet. The threshold remains `0.005`.

Docker/Colima hardening matrix:

| solver | instances | grid | Nt | total wall-clock | per-instance error | status | reward |
| --- | --- | --- | --- | --- | --- | --- | --- |
| reference | 3 | 320x320 | adaptive | 25.069s | i0:0.00059492, i1:0.000621576, i2:0.00185831 | pass | 1.0 |
| nop/starter | 3 | n/a | n/a | <1s | i0:1.0 | fail | 0.0 |
| coarse_dt | 3 | 320x320 | 128 | 0.653s | i0:1.14476, i1:0.530429, i2:5.56799 | fail | 0.0 |
| explicit | 3 | 320x320 | 4096 | 0.067s | i0:error, i1:error, i2:error | fail | 0.0 |
| brute_overresolve | 3 | 320x320 | 65536 | 180.000s | i0:6.36695e-05, i1:timeout | fail | 0.0 |
| codex_pass_1 replay | 3 | adaptive <=132-ish | adaptive | 6.692s | i0:0.00112895, i1:0.00108784, i2:0.00635706 | fail | 0.0 |
