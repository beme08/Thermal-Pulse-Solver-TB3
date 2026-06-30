# Trial Results

No Harbor standard or cheat trials have been run yet. The multi-instance
verifier smoke checks have run locally/Docker, but they are not Harbor agent
trials.

Each future trial must record these fields:

| job name | agent | model | reasoning config | task checksum | git commit | standard vs cheat | reward | exceptions | runtime | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| pending | pending | pending | pending | pending | pending | standard | pending | pending | pending | pending | pending |
| pending | pending | pending | pending | pending | pending | cheat | pending | pending | pending | pending | pending |

## Local Verifier Smoke Checks

| job name | agent | model | reasoning config | task checksum | git commit | standard vs cheat | reward | exceptions | runtime | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| verifier-nop-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | smoke | 0.0 | expected verifier failure | <1s | legitimate-fail | Starter app emitted zeros; verifier failed on instance 0 with relative error 1. |
| verifier-reference-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | smoke | 1.0 | none | 0.458s | legitimate-pass | Reference artifact produced exact manufactured temperatures for both deterministic private instances. |

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
