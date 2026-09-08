# Validation log

## 2026-09-07 — M0 foundation and initial M1 forest

**M1 does not pass the design's acceptance gate.** This implementation stops before kingdoms and RL, following §27. The original design document remains unchanged.

### Engineering checks

- GCC 13.3 and Clang 18.1 Release builds pass all 11 C++ test cases, Python analysis tests, and CLI/replay integration checks.
- Identical 5,000-tick digests for all ten committed 64² golden replays under GCC and Clang. Each includes 3,000 ticks of warmup. Initial M0 runs also matched over 10,000 empty ticks.
- Forest layers and event accounting agree at every tick for 1/2/4/8 workers and reversed traversal under frequent lightning. Tests exercise deterministic merging and completed-fire accounting, not only an empty world.
- An allocation hook counts zero allocations across 200 active four-worker ticks. All worker threads and buffers are created before ticking.
- Exact ignition/burn/ash timing, zero-lightning behavior, ash fertility gains, fixed boundaries, fixed-point edge cases, config rejection/canonical round trips, sine accuracy, and projection round trips pass.
- Truncated, malformed, and corrupted replay files are rejected. A modified checkpoint reports the first mismatch at tick 80.
- Debug ASan + UBSan + LeakSanitizer suite passes. The first sandboxed attempt could not complete LeakSanitizer under ptrace; rerunning outside the sandbox passed all four CTest groups (87.73 s).
- Headless dynamic linkage contains only standard runtime libraries; no raylib, OpenGL, X11, or Wayland. CI also defines a build in a minimal GCC container with graphics disabled. Remote CI has not been observed yet.
- The raylib viewer built and ran at 256². The committed screenshot was inspected. The paused view showed about 54 FPS on Mesa llvmpipe software rendering; a subsequent 128² frequent-fire test advanced 480 ticks at 4× and displayed approximately 6 FPS on the same software renderer. Live cache updates, smoke, CCDF and events worked. Large-fire 60 FPS acceptance is **not established**; rendering performance needs work.

### Forest measurement

Baseline config, seed 1, one worker, mandatory 3,000-tick warmup, then 20,000 measured ticks per size. Events begun before warmup ends and events still active at episode end are excluded from fits. This right-censoring can affect the largest events and should be assessed in longer runs.

| Quantity | 128² | 256² |
|---|---:|---:|
| Completed post-warmup samples | 4,332 | 7,472 |
| Maximum ignition count per event | 2,484 | 2,813 |
| KS-selected xmin | 8 | 17 |
| Tail events | 1,868 | 2,059 |
| Discrete MLE τ | 1.6340 | 1.6842 |
| KS distance | 0.03552 | 0.03654 |
| Tail span, log10(max/xmin) | 2.492 | 2.219 |
| Required τ ∈ [1.0, 1.4] | **FAIL** | **FAIL** |

The exponent difference is 0.0502, within the specified 0.1 size-stability bound for this seed. Both selected tails span two decades. Neither fact repairs the failed exponent criterion, and neither establishes that a power law is the best model.

The analyzer performs discrete likelihood optimization using Hurwitz zeta and selects `xmin` by minimizing KS distance with at least 100 tail observations. Its zeta routine is checked against ζ(2), ζ(4), and the Hurwitz recurrence; a synthetic tail test checks exponent recovery. No third-party Python dependencies are required. Bootstrap/model comparisons remain outstanding.

Committed artifacts:

- [`forest-baseline.json`](forest-baseline.json): machine-readable fits.
- [`forest-run-metadata.json`](forest-run-metadata.json): seed/settings, raw CSV checksums, and early/late coverage averages.
- [`data/`](data/): gzip-compressed, unmodified completed-event CSVs from both runs.
- [`forest-viewer.png`](forest-viewer.png): observed viewer output.
- [`../tests/golden/`](../tests/golden/): ten versioned replay fixtures. Tests never regenerate them automatically.

Reproduce with the commands in the [README](../README.md). Baseline measured throughput was approximately 2,213 ticks/s at 128² and 611 ticks/s at 256², including CSV output but excluding warmup; these are preliminary wall-clock observations on a shared machine, not isolated benchmarks or full-simulation budget acceptance.

## 2026-09-07 — Amendment 1 and coupled simulation

The earlier “stop before kingdoms” status is historical. The user authorized necessary changes, and [Amendment 1](AMENDMENT-1.md) records the scientific and engineering corrections. Original measurements remain unchanged.

### Coupled behavior and regression checks

- GCC Release passes 15 C++ cases, Python analysis tests, CLI integration, and the revised recorded-sample check (5 CTest groups).
- Clang Release passes the expanded tests and all ten v2 golden replays. Eight isolate the forest; two include kingdoms, each for 5,000 ticks. V1 fixtures were archived before regeneration.
- Action validation, deterministic macro steps at 1/4 threads, fixed-point pruning/recovery, nonnegative stocks/refugees, disease compartment bounds, and collapse under forced shortage are tested. Tick-allocation checks now include civilization activity.
- A real farming bug was corrected: depletion now multiplies remaining fertility rather than subtracting a constant amount until soil is exhausted.
- Seed 1, 64², 100 warmup ticks, 2,000 episode ticks: three kingdoms collapsed while kingdom 1 retained approximately 3,540 population, 52 functional structures, and 5,005 grain. This is a behavioral example, not a balanced-cause or tournament study.
- The ctypes/NumPy interface passed deterministic reset, matching observations/digests across thread counts, atomic rejection of NaN actions, and saved-action-session reconstruction.
- A four-agent, one-environment PPO smoke update completed with finite loss 0.02363. A one-agent C0 update and checkpoint-resume update also completed with finite losses around 0.02496/0.02493. These are smoke checks, not evidence of convergence. Locally tested with CPU PyTorch 2.14.0 and NumPy 2.5.2 in a temporary environment.

### Viewer measurement

A 128² live diagnostic initially spent roughly 82.6 ms/frame in simulation and 153.2 ms/frame refreshing chunks, excluding presentation. Resolution-aware textures alone were insufficient on llvmpipe. Bounded simulation work, oldest-first dirty-chunk refresh, and four-tick logistics cadence reduced the measured 120-frame run to 4.526 seconds (about 26.5 FPS), with 119 simulation ticks advanced. Mean measured phases were 8.61 ms simulation, 13.26 ms cache refresh, and 15.10 ms presentation. Startup work is included, and this was a shared-machine software-rendering test.

The kingdom dashboard and rendered fields were visually inspected; see [`kingdom-viewer.png`](kingdom-viewer.png). Speed is a requested simulation rate, not a promise that a slow machine executes it. Cached terrain can lag current simulation state while refresh work is budgeted. The original 60 FPS large-fire target is still a performance objective, not a claimed result.

### Research still outstanding

Universal forest criticality, the proposed discontinuous dependency transition, calibrated front speeds, five-way death-cause balance, successful refugee founding rates, strategic non-transitivity, monitoring advantages, trained-policy exploitability and all headline RL experiments remain unestablished. They are reported as research work, not converted into passing tests by changing expected data.

The expanded Debug ASan/UBSan/LeakSanitizer run passed all four CTest groups in 493.25 seconds. Final review then found and corrected recruitment bookkeeping: recruiting civilians must proportionally remove their infected/immune compartments as well as population. That correction intentionally changes a coupled golden checkpoint starting at tick 640; coupled fixtures were regenerated for the corrected behavior. Final optimized-sanitizer verification is recorded below.

Final verification: all 15 C++ cases and CLI/analysis groups pass on GCC and Clang; Clang verifies all ten final v2 goldens. ASan/UBSan/LeakSanitizer with RelWithDebInfo passes all four groups in 114.20 seconds. Nondefault epidemic recovery and capital-grace configuration round trips are explicitly tested. Claim ownership includes the configured floor and resolves equal claims by lowest ID. Final Python saved-session/determinism checks and PPO smoke update pass (loss 0.02362674). Formatting and whitespace checks pass. Remote CI, TSan/MSan, cross-OS tests, and research experiments are still not claimed.
