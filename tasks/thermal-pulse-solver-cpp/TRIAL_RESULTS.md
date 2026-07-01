# Trial Results

One Harbor standard Codex trial has run and passed legitimately. No Harbor
cheat trials have been run yet. The multi-instance verifier smoke checks below
are local/Docker checks, not Harbor agent trials.

Each future trial must record these fields:

| job name | agent | model | reasoning config | task checksum | git commit | standard vs cheat | reward | exceptions | runtime | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| gatekeeper-codex-thermalpulse-1 | Codex | openai/gpt-5.5 | reasoning_effort=xhigh | 02cae49195483ec8c40d9596bb39b9d0a0fd39c4ff152968014114a9370e5e6f | d531257 | standard | 1.0 | 0 | 12m08s | legitimate solve | Implemented adaptive oracle-sampling ADI/finite-volume solver. No hidden constants, verifier paths, tests, private truth, or brute-force strategy observed. |
| pending | pending | pending | pending | pending | pending | cheat | pending | pending | pending | pending | pending |

## Local Verifier Smoke Checks

| job name | agent | model | reasoning config | task checksum | git commit | standard vs cheat | reward | exceptions | runtime | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| verifier-nop-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | smoke | 0.0 | expected verifier failure | <1s | legitimate-fail | Starter app emitted zeros; verifier failed on instance 0 with relative error 1. |
| verifier-reference-docker-invalid | Codex | GPT-5 Codex | default | not stamped | pending commit | smoke | 1.0 | none | 0.458s | harness-error | Rejected as invalid evidence: reference artifact evaluated the manufactured formula directly instead of solving numerically. |
| verifier-reference-docker-numerical | Codex | GPT-5 Codex | default | not stamped | pending commit | smoke | 1.0 | none | 18.840s | legitimate-pass | Numerical ADI/CN reference uses public oracle calls only; errors are nonzero and below threshold. |
| multi-instance-real-solver-table-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | sweep | reference 1.0; brute/coarse/explicit 0.0 | none | brute timeout 180.000s | legitimate-pass | Real reference fits shared budget; brute high-`Nt` times out during instance 1; coarse silently wrong; explicit unstable. |
| single-instance-brute-margins-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | sweep | n/a | none | 99.996s-101.589s | brute-force-pass | Each private instance passes individually with blind `Nt=65536`; confirms per-instance budgets would leak. |
| three-instance-real-solver-table-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | sweep | reference 1.0; brute/coarse/explicit 0.0 | none | reference 25.039s; brute timeout 180.000s | legitimate-pass | Final candidate uses three deterministic instances under one shared verifier budget; reference remains under 60s, brute times out, coarse and explicit fail. |
| codex_pass_1-replay-local | Codex | openai/gpt-5.5 artifact | n/a | 02cae49195483ec8c40d9596bb39b9d0a0fd39c4ff152968014114a9370e5e6f | d531257 | replay baseline | 1.0 | none | 8.876s | legitimate-pass | Saved Harbor artifact replayed from `tests/baselines/codex_pass_1`; errors `0.00112895`, `0.00108784`, `0.00100115`. |
| codex_pass_1-replay-docker | Codex | openai/gpt-5.5 artifact | n/a | 02cae49195483ec8c40d9596bb39b9d0a0fd39c4ff152968014114a9370e5e6f | d531257 | replay baseline | 1.0 | none | 6.664s | legitimate-pass | Docker/Colima replay confirms current verifier still accepts the saved Codex artifact; errors `0.00112895`, `0.00108784`, `0.00100115`. |
| codex-pass-spatial-hardening-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | replay baseline | reference 1.0; codex_pass_1 0.0 | none | reference 25.069s; codex replay 6.692s | legitimate-fail | Candidate retunes one smooth private spatial packet; `codex_pass_1` fails on instance 2 at `0.00635706` while reference remains below threshold. |

## Trial Classification Rules

Classification rules follow the benchmark FAQ.

A Harbor run is counted only if:
- Trials = 1
- Exceptions = 0

Invalid runs (do not count):
- DNS/package-manager setup failure
- OAuth/token setup failure
- API/provider rate limit
- agent harness crash before task attempt

For valid runs:
- reward 0.0 = clean failure only after trajectory review
- reward 1.0 = inspect trajectory before any task change

First-failure taxonomy:
- infra/provider
- auth/setup
- task misunderstanding
- numerical method failure
- budget/timeout
- schema/output failure
- verifier exploit
- legitimate solve

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
- Classification from the taxonomy above.
