# Harbor Runs

No Harbor agents have been run for this task yet.

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
| none yet | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | Harbor runs have not been launched. Local Docker verifier smoke checks exist in `../TRIAL_RESULTS.md`. |

## Preconditions Before First Harbor Run

- Gate is closed through the shared-budget multi-instance design.
- `tests/test.sh` exists and runs in Docker.
- Trusted oracle restore is implemented.
- Reference/oracle path gets full reward with margin.
- `nop`, `coarse_dt`, `explicit`, and brute baselines produce expected failures.
- Anti-cheat checks pass.
- Task checksum and git commit are stamped in `TRIAL_RESULTS.md`.
