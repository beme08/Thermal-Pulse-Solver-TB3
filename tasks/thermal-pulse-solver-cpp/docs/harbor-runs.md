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

| job name | agent | model | reasoning config | task checksum | git commit | standard vs cheat | reward | exceptions | runtime | classification | trajectory notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| gatekeeper-codex-thermalpulse-1 | Codex | openai/gpt-5.5 | reasoning_effort=xhigh | 02cae49195483ec8c40d9596bb39b9d0a0fd39c4ff152968014114a9370e5e6f | d531257 | standard | 1.0 | 0 | 12m08s | legitimate solve | Adaptive oracle-sampling ADI/finite-volume solver passed all three private instances. Artifact saved as `../tests/baselines/codex_pass_1/solution.cpp`. |

## Preconditions Before Next Harbor Run

- Gate is closed through the shared-budget multi-instance design.
- `tests/test.sh` exists and runs in Docker.
- Trusted oracle restore is implemented.
- Reference/oracle path gets full reward with margin.
- `nop`, `coarse_dt`, `explicit`, and brute baselines produce expected failures.
- Anti-cheat checks pass.
- Task checksum and git commit are stamped in `TRIAL_RESULTS.md`.
