# Harbor Runs

One Harbor standard Codex run has been completed. No Harbor cheat runs have
been launched yet.

Use this file for job-level Harbor metadata. Detailed result interpretation
belongs in `../TRIAL_RESULTS.md`; gate and sweep evidence belongs in
`../EXPERIMENTS.md`.

## Run Template

```text
job name:
agent:
model:
reasoning config:
task checksum:
git commit:
standard vs cheat:
reward:
exceptions:
runtime:
classification:
trajectory notes:
harbor URL:
started at:
completed at:
command:
```

## Run Index

| date | job name | agent | model | reasoning config | task checksum | git commit | mode | reward | exceptions | runtime | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-06-29 | gatekeeper-codex-thermalpulse-1 | Codex | openai/gpt-5.5 | xhigh | 02cae49195483ec8c40d9596bb39b9d0a0fd39c4ff152968014114a9370e5e6f | d531257 | standard | 1.0 | 0 | 12m08s | legitimate solve | Adaptive oracle-sampling ADI/finite-volume solver passed all three private instances. Artifact saved as `../tests/baselines/codex_pass_1/solution.cpp`; Docker replay also returns reward 1.0 in 6.664s. |
| 2026-07-01 | gatekeeper-codex-thermalpulse-hardened-1 | Codex | openai/gpt-5.5 | xhigh | 6666f89c620c4a50e5af6395cf9efe74bf4d1527a0a0c04c47b3746b2fb64eb2 | 86ec8d4 | standard | 0.0 | 0 | 12m50s | clean numerical failure | Adaptive solver with frequency/omega estimation and grid selection failed instance 1 at relative error `0.00564189 > 0.005`. No verifier/test leakage observed. |

## Run Detail

### gatekeeper-codex-thermalpulse-hardened-1

- Date: 2026-07-01
- Agent/model: Codex / openai/gpt-5.5
- Mode: standard
- Reasoning: xhigh
- Task checksum: `6666f89c620c4a50e5af6395cf9efe74bf4d1527a0a0c04c47b3746b2fb64eb2`
- Git commit: `86ec8d4`
- Result: reward `0.0`, exceptions `0`
- Runtime: 12m50s
- Classification: clean numerical failure

Verifier failed on instance 1 with relative error `0.00564189`, above the threshold `0.005`. The submitted solution attempted an adaptive frequency/omega-estimating numerical method, but underresolved the hardened private case. No verifier/test leakage was observed.

## Preconditions Before Next Harbor Run

- Gate is closed through the shared-budget multi-instance design.
- `tests/test.sh` exists and runs in Docker.
- Trusted oracle restore is implemented.
- Reference/oracle path gets full reward with margin.
- `nop`, `coarse_dt`, `explicit`, and brute baselines produce expected failures.
- Anti-cheat checks pass.
- Task checksum and git commit are stamped in `TRIAL_RESULTS.md`.
