# Ashfall live agent evaluation — October 2, 2026

Real GLM-5.3 API evaluations were completed against the C++ simulation. The frozen main cohort contains 20 independent seeds, one preregistered balanced kingdom position per seed, and three policy variants: 60 completed episodes. Each episode runs up to 256 macrosteps (1,024 simulation ticks), with an LLM decision cadence of 16 macrosteps. All **60 recorded-action replays matched** their original state digests.

## Main results

| Policy | Original kingdom survived | Episodes |
|---|---:|---:|
| LLM with memory available | 15 (75%) | 20 |
| LLM without memory | 15 (75%) | 20 |
| Scripted baseline | 8 (40%) | 20 |

For either LLM variant, the paired survival difference against the scripted baseline is +35 percentage points; the seed-bootstrap 95% interval is [+15, +55] percentage points. These results apply to this scenario, seed cohort, horizon, and scripted comparator. They do not establish superiority over trained MAPPO, strong search policies, or arbitrary opponents. Opponent native policies have privileged simulator access; the evaluated LLM uses its permitted observations.

Of 621 LLM-policy decision opportunities, 619 completed autonomously and two used fallback actions (one provider timeout and one bounded-decision failure). There were no invalid action proposals in the main cohort. The cohort consumed 1,820,571 provider-reported tokens plus a conservative 17,825-token unresolved reservation. Tokens are not independently verified bundle debits, and estimated dollar fields are not additional cash charges.

Five memory recall attempts were observed across the live cohort, with **zero successful nonempty recalls**. Equal survival rates therefore do not establish either the usefulness or uselessness of functioning memory retrieval. No planning-benefit claim is supported by this cohort. Dedicated capability probes are separate and were not included in these results.

## Development results

The earlier five-seed development cohort completed 15 episodes, including ten live LLM episodes. All 15 recorded-action replays matched. The ten live runs made 160 committed decisions and consumed 466,921 reported tokens. One invalid proposal was rejected and repaired before mutation; no fallback occurred. All ten original LLM kingdoms survived, compared with one of five scripted kingdoms. These are development results used during implementation, not additional independent held-out evidence.

## Evidence and limitations

- [Main aggregate](systematic-holdout-v1/aggregate.json): per-episode outcomes, configuration, summaries, and paired seed-bootstrap comparisons.
- [Main trace audit](systematic-holdout-v1/trace-audit.json): decision, proposal, fallback, tool-use, and memory-use counts.
- [Main replay receipts](systematic-holdout-v1/replay.log): 60 successful saved-action replay checks.
- [Development aggregate](systematic-development-v2/aggregate.json) and [audit](systematic-development-v2/trace-audit.json).
- [Publication manifest](publication-manifest.json): source and published hashes. Local absolute paths are normalized in public copies.

The runtime and preregistered main configurations were frozen during the cohort. Repeating saved actions verifies deterministic simulation execution; it does not imply that new LLM calls reproduce the same choices. Bootstrap units are seeds, not individual decisions. Original-generation survival is distinct from survival of a successor kingdom occupying the same slot.

The original reinforcement-learning implementation remains available but was not retrained or evaluated as a comparator here. Additional live memory, planning, and mixed-controller capability probes remain separate follow-up work. The full local traces and checkpoints are retained; this initial publication contains aggregate evidence, per-episode audits, and replay receipts rather than all generated databases or checkpoints.

To run your own evaluation, follow [the agent guide](../../../docs/AGENT_GUIDE.md) and [evaluation protocol](../../../docs/AGENT_EVALUATION_PROTOCOL.md), using the checked-in frozen suite definitions and your own API credential and budget. No credential is included in this publication.
