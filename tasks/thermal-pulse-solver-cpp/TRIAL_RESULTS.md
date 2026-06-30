# Trial Results

No Harbor standard or cheat trials have been run yet. Do not add final task
claims here until the gate is closed and the verifier is implemented.

Each future trial must record these fields:

| job name | agent | model | reasoning config | task checksum | git commit | standard vs cheat | reward | exceptions | runtime | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| pending | pending | pending | pending | pending | pending | standard | pending | pending | pending | pending | pending |
| pending | pending | pending | pending | pending | pending | cheat | pending | pending | pending | pending | pending |

## Classification Labels

- `legitimate-pass`: solved by intended discovery plus stable efficient solve.
- `legitimate-fail`: failed without verifier exploit.
- `brute-force-pass`: passed by blind over-resolution under budget.
- `timeout-fail`: exceeded wall-clock.
- `silent-wrong-fail`: emitted plausible but inaccurate values.
- `unstable-fail`: numerical blowup, NaN, inf, or invalid output.
- `exploit-pass`: passed by leaking truth, hidden points, private params, or verifier state.
- `harness-error`: task infrastructure failed or result is not interpretable.

## Required Notes Per Trial

For every trial, include:

- Whether the trial was standard or cheat.
- The exact Harbor job name and run URL if available.
- Agent and model.
- Reasoning config, budget, and any harness flags.
- Git commit and task checksum.
- Reward and pass/fail status.
- Runtime and timeout boundary.
- Exceptions, stderr, invalid output, or verifier errors.
- Short trajectory notes: how the agent appeared to choose `Nt`, `Nx`, solver type, and whether it sampled the oracle.
- Classification from the labels above.
