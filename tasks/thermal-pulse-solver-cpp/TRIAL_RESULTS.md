# Trial Results

Eight Harbor Codex trials have been completed: one legitimate standard pass,
three clean hardened numerical failures, one historical cheat trial (classified
A, solved legitimately), one final cheat-hardened trial (reward 0.0, timeout on
instance 2), and three final standard gatekeeper trials (reward 0.0, clean
numerical failures). Codex hardened standard trials: 6 valid runs, 0 exceptions,
all reward 0.0. The multi-instance verifier smoke checks below are local/Docker
checks, not Harbor agent trials.

Each future trial must record these fields:

| date | job name | agent | model | reasoning config | task checksum | git commit | mode | reward | exceptions | runtime | cost | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-06-29 | gatekeeper-codex-thermalpulse-1 | Codex | openai/gpt-5.5 | xhigh | 02cae49195483ec8c40d9596bb39b9d0a0fd39c4ff152968014114a9370e5e6f | d531257 | standard | 1.0 | 0 | 12m08s | - | legitimate solve | Implemented adaptive oracle-sampling ADI/finite-volume solver. No hidden constants, verifier paths, tests, private truth, or brute-force strategy observed. |
| 2026-07-01 | gatekeeper-codex-thermalpulse-hardened-1 | Codex | openai/gpt-5.5 | xhigh | 6666f89c620c4a50e5af6395cf9efe74bf4d1527a0a0c04c47b3746b2fb64eb2 | 86ec8d4 | standard | 0.0 | 0 | 12m50s | - | clean numerical failure | Failed instance 1 with relative error 0.00564189 > 0.005. No verifier/test leakage observed; grep hits were generic frequency/omega estimation and grid cap constants. |
| 2026-07-01 | gatekeeper-codex-thermalpulse-hardened-2 | Codex | openai/gpt-5.5 | xhigh | 6666f89c620c4a50e5af6395cf9efe74bf4d1527a0a0c04c47b3746b2fb64eb2 | 137e548 | standard | 0.0 | 0 | 17m29s | - | clean numerical failure | Failed instance 2 with relative error 0.00574273 > 0.005. No verifier/test leakage observed; grep hits `N = 96` and `N = 128` appear to be grid-size constants, not hidden-frequency leakage. |
| 2026-07-01 | gatekeeper-codex-thermalpulse-hardened-3 | Codex | openai/gpt-5.5 | xhigh | 6666f89c620c4a50e5af6395cf9efe74bf4d1527a0a0c04c47b3746b2fb64eb2 | 137e548 | standard | 0.0 | 0 | 25m47s | $2.700929 | clean numerical failure | Failed instance 0 with relative error 0.0115036 > 0.005. No verifier/test leakage observed; grep hits `intervals = 96`, `even_clamp(..., 28, 96)`, and `std::max({96, ...})` are generic interval/grid constants, not hidden-frequency leakage. |
| 2026-07-01 | cheat-codex-thermalpulse-hardened-1 | Codex | openai/gpt-5.5 | xhigh | 5a4130d9a23a21ddcaa262940ae7140a3dea95680bc63856f74a8d076df7ab62 | dd9ae88 | cheat | 1.0 | 0 | 15m13s | $1.747216 | legitimate solve (A) | Red-team prompt instructed exploitation; none found; fell through to legitimate ADI solver. All 3 instances passed under threshold (rel_errors 0.00133/0.00126/0.00237). No verifier exploit, hidden-data access, system/popen/fork, or hardcoded constants. |
| 2026-07-01 | cheat-codex-thermalpulse-cheathardened-1 | Codex | openai/gpt-5.5 | xhigh | c6e6808e5b220c484575844a3659cad59011007557b3f5c20c8940aff22fdc64 | 80a0626 | cheat | 0.0 | 0 | 14m38s | $1.489593 | clean adversarial failure by timeout | Agent searched for bypass, found none, attempted solver with k_intervals=128 and exact_output_times; instance 2 timed out. No hidden/test/verifier/reward/shell/proc/env exploit indicators. |
| 2026-07-01 | gatekeeper-codex-thermalpulse-final-1 | Codex | openai/gpt-5.5 | xhigh | c6e6808e5b220c484575844a3659cad59011007557b3f5c20c8940aff22fdc64 | dbbda94 | standard | 0.0 | 0 | 15m57s | $1.960097 | clean numerical failure | Failed instance 0 with relative error 0.0501037 > 0.005. Grep hits `128`, `fine_n = 96`, and grid candidate arrays are benign solver constants. No hidden/test/verifier/reward/shell/proc/env exploit indicators. |
| 2026-07-01 | gatekeeper-codex-thermalpulse-final-2 | Codex | openai/gpt-5.5 | xhigh | 3e3854de934d64823e085f79d4c3644eb3dfcc0b9ff34cd13fbf2291ff921dd5 | 4acae7f | standard | 0.0 | 0 | 16m17s | $2.178620 | clean numerical failure | Failed instance 2 with relative error 0.00912555 > 0.005. Grep hits `grids = {24, 40, 64, 96, 128}` and `spatial_tol = (grid < 96)` are benign solver grid/tolerance constants. No hidden/test/verifier/reward/shell/proc/env exploit indicators. |
| 2026-07-01 | gatekeeper-codex-thermalpulse-final-3 | Codex | openai/gpt-5.5 | xhigh | e45b0a2fbd805a6fd1d549065c226b5fcde1d329576be3d8f950f399f53d4ceb | 5b4ed0a | standard | 0.0 | 0 | 11m22s | $1.502488 | clean numerical failure | Failed instance 0 with relative error 0.0181674 > 0.005. Grep hit `0.633974596` is a coordinate constant substring match, not hidden-frequency leakage. No hidden/test/verifier/reward/shell/proc/env exploit indicators. |

## Local Verifier Smoke Checks

| date | job name | agent | model | reasoning config | task checksum | git commit | mode | reward | exceptions | runtime | cost | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| - | verifier-nop-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | smoke | 0.0 | expected verifier failure | <1s | - | legitimate-fail | Starter app emitted zeros; verifier failed on instance 0 with relative error 1. |
| - | verifier-reference-docker-invalid | Codex | GPT-5 Codex | default | not stamped | pending commit | smoke | 1.0 | none | 0.458s | - | harness-error | Rejected as invalid evidence: reference artifact evaluated the manufactured formula directly instead of solving numerically. |
| - | verifier-reference-docker-numerical | Codex | GPT-5 Codex | default | not stamped | pending commit | smoke | 1.0 | none | 18.840s | - | legitimate-pass | Numerical ADI/CN reference uses public oracle calls only; errors are nonzero and below threshold. |
| - | multi-instance-real-solver-table-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | sweep | reference 1.0; brute/coarse/explicit 0.0 | none | brute timeout 180.000s | - | legitimate-pass | Real reference fits shared budget; brute high-`Nt` times out during instance 1; coarse silently wrong; explicit unstable. |
| - | single-instance-brute-margins-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | sweep | n/a | none | 99.996s-101.589s | - | brute-force-pass | Each private instance passes individually with blind `Nt=65536`; confirms per-instance budgets would leak. |
| - | three-instance-real-solver-table-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | sweep | reference 1.0; brute/coarse/explicit 0.0 | none | reference 25.039s; brute timeout 180.000s | - | legitimate-pass | Final candidate uses three deterministic instances under one shared verifier budget; reference remains under 60s, brute times out, coarse and explicit fail. |
| - | codex_pass_1-replay-local | Codex | openai/gpt-5.5 artifact | n/a | 02cae49195483ec8c40d9596bb39b9d0a0fd39c4ff152968014114a9370e5e6f | d531257 | replay baseline | 1.0 | none | 8.876s | - | legitimate-pass | Saved Harbor artifact replayed from `tests/baselines/codex_pass_1`; errors `0.00112895`, `0.00108784`, `0.00100115`. |
| - | codex_pass_1-replay-docker | Codex | openai/gpt-5.5 artifact | n/a | 02cae49195483ec8c40d9596bb39b9d0a0fd39c4ff152968014114a9370e5e6f | d531257 | replay baseline | 1.0 | none | 6.664s | - | legitimate-pass | Docker/Colima replay confirms current verifier still accepts the saved Codex artifact; errors `0.00112895`, `0.00108784`, `0.00100115`. |
| - | codex-pass-spatial-hardening-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | replay baseline | reference 1.0; codex_pass_1 0.0 | none | reference 25.069s; codex replay 6.692s | - | legitimate-fail | Candidate retunes one smooth private spatial packet; `codex_pass_1` fails on instance 2 at `0.00635706` while reference remains below threshold. |
| - | cheat_codex_pass_1-replay-before-hardening | Codex | openai/gpt-5.5 artifact | n/a | 5a4130d9a23a21ddcaa262940ae7140a3dea95680bc63856f74a8d076df7ab62 | dd9ae88 | replay baseline | 1.0 | none | 37.447s | - | legitimate-pass | Saved cheat artifact reproduced the Harbor pass against the current verifier; errors `0.00132861`, `0.00125662`, `0.00236865`. |
| - | cheat-codex-spatial-hardening-docker | Codex | GPT-5 Codex | default | not stamped | pending commit | replay baseline | reference 1.0; cheat_codex_pass_1 0.0 | none | reference 36.608s; cheat replay 35.568s | - | legitimate-fail | Candidate keeps threshold `0.005`, sharpens one private smooth spatial packet, raises reference grid to `384^2`, and makes `cheat_codex_pass_1` fail on instance 2 at `0.00610366`. |
| - | claude_pass_1-replay-before-hardening-local | Claude | claude_pass_1 artifact | n/a | not stamped | pending commit | replay baseline | 1.0 | none | 32.851s | - | legitimate-pass | Saved Claude artifact passed the `sharp=640` verifier with errors `0.000556352`, `0.00229326`, `0.00451386`; no exploit or hidden-data dependency observed. |
| - | claude-replay-spatial-hardening-local | Codex | GPT-5 Codex | default | not stamped | pending commit | replay baseline | reference 1.0; codex_pass_1/cheat_codex_pass_1/claude_pass_1 0.0 | none | reference 35.030s; brute timeout 180.000s | - | legitimate-fail | Candidate keeps threshold/schema fixed and changes instance 2 from `sharp=640` to `sharp=760`; Claude replay fails instance 2 at `0.00529913`, cheat replay at `0.00721098`, Codex replay at `0.0126047`. |
| - | reference-margin-strengthening-local | Codex | GPT-5 Codex | default | not stamped | pending commit | reference audit | reference 1.0; codex_pass_1/cheat_codex_pass_1/claude_pass_1 0.0 | none | Docker reference 72.981s/73.456s; mode matrix reference 68.707s | - | legitimate-pass | Instance-2 diagnostic showed temporal dominance, so the trusted reference keeps `384^2` and doubles detected reference Nt. Reference i2 improves from `0.00390874` to `0.00113210`; replay baseline failures are unchanged. |

## Hardened Scorecard

| baseline | reward | status |
|---|---|---|
| reference | 1.0 | confirmed with strengthened margin |
| nop/starter | 0.0 | confirmed |
| coarse_dt | 0.0 | confirmed |
| explicit | 0.0 | confirmed |
| brute_overresolve | 0.0 | confirmed (timeout) |
| codex_pass_1 replay | 0.0 | confirmed |
| cheat_codex_pass_1 replay | 0.0 | confirmed locally/Docker on hardening branch |
| claude_pass_1 replay | 0.0 | confirmed locally after bounded replay hardening |
| Codex hardened #1 | 0.0 | clean numerical failure |
| Codex hardened #2 | 0.0 | clean numerical failure |
| Codex hardened #3 | 0.0 | clean numerical failure |
| Codex cheat (no bypass found) | 1.0 | historical Harbor result before cheat replay hardening |
| Codex cheat on hardened checksum | 0.0 | clean adversarial failure by timeout; all bypass attempts failed |
| Codex final standard #1 | 0.0 | clean numerical failure |
| Codex final standard #2 | 0.0 | clean numerical failure |
| Codex final standard #3 | 0.0 | clean numerical failure |

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
