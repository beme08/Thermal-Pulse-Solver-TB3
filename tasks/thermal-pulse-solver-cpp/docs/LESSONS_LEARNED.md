# Lessons Learned — Thermal Pulse Solver Hardening

## Benchmark Design

1. **Numerical difficulty is the only gate.** No structural bypass, verifier exploit, or hidden-data leakage was discovered across 3 hardened standard trials + 1 cheat trial. The benchmark's hardness comes from requiring adequate spatial resolution and adaptive time-stepping.

2. **Shared-budget multi-instance verifier closes brute-force.** A single verifier timeout across multiple private instances prevents blind high-Nt strategies. Per-instance budgets would leak.

3. **Set a replay baseline before hardening.** Saving `codex_pass_1` before any verifier change let us confirm the hardening closed the known path without breaking the reference.

4. **Scorecard before Harbor.** Verify reference=1.0, nop/coarse/explicit/brute=0.0, and codex_pass_1 replay=0.0 before launching fresh agent trials.

## Trial Methodology

5. **Stamp every trial.** Record git commit, task checksum (not just job name), cost, and trajectory notes for every Harbor run.

6. **Run cheat trials even if no bypass is expected.** The hack-trial prompt may be ignored by agents; this itself is a finding worth documenting.

7. **Inspect trajectory, not just reward.** A reward 1.0 cheat run may still be a clean solve (classification A) — inspect solution.cpp, verifier logs, and trajectory.json before acting on it.

8. **Checksum mismatches are normal.** Each Harbor run generates a unique trial ID/checksum even for the same task tree. Diff the git tree to confirm identical task content.

## Hack-Trial Prompt Design

9. **The "stop and report" instruction was ignored.** Codex with xhigh reasoning did not follow the rule "if no credible bypass exists, stop and report" — it fell through to a legitimate solve. This suggests hack-trial prompts may need stronger enforcement or a separate harness check.

10. **Document cheat trial classification immediately.** Use the A/B/C/D taxonomy so the distinction between "found no bypass" (A) and "found and exploited a bypass" (B/C) is clear in the permanent record.
