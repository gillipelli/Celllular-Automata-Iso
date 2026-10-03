# Strategy agent guide

Ashfall's optional agent layer connects a language model to validated simulation tools. It supports kingdom inspection, spatial observations, action validation, diplomacy inspection, generation-scoped memory, and one submitted macro action per decision. The coordinator gathers each kingdom's decision from the same world tick before committing a joint step.

The domain tools do not execute arbitrary model-generated code. Typed action fields become the existing twenty-seven-element simulator action. The C++ bridge is authoritative for mutations. A malformed response, exhausted decision budget, or provider failure is recorded and uses the documented balanced fallback.

## Installation

From the repository root, build the C++ bridge using the README. Create a Python environment and install `python/requirements-agents.txt`. The live Z.ai adapter uses standard-library HTTP; the Anthropic SDK is included for the optional alternate provider. PyTorch is required only for the existing PPO trainer, not for this agent layer.

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -r python/requirements-agents.txt pytest
export PYTHONPATH=python
export ASHFALL_LIBRARY="$PWD/build/libashfall_bridge.so"
```

## Offline demonstration

```sh
python -m ashfall_agents play \
  --config config/presets/agent-demo.toml --seed 42 \
  --controllers fixture,native,native,native --track partial \
  --steps 4 --run-dir runs/agents/demo
python -m ashfall_agents replay --run runs/agents/demo
```

Use a new run directory for a new experiment. `fixture` performs a scripted inspect/submit tool round trip; it is not a model and its outcomes must not be labeled LLM performance. `scripted` uses a fixed balanced action. `native` leaves the kingdom to the simulator's configured policy. The native policies have access to internal state, unlike observation-matched baselines.

The run contains a manifest, append-only event trace, memory/checkpoints, recorded session, machine-readable result, and `report.html`. The HTML report escapes model content and can be read without a running server. Evaluator-only seed, digest, and full joint history remain outside partial-controller prompts and tools.

## Live GLM configuration

Set `ZAI_API_KEY` in the shell environment using your secret manager or a non-echoing local prompt. Do not put it in code, committed configuration, or traces. The adapter uses Z.ai's general prepaid Chat Completions endpoint, not a coding-subscription endpoint. Account access must permit the requested model.

Example of a deliberately bounded live run:

```sh
python -m ashfall_agents play \
  --config config/presets/agent-demo.toml \
  --controllers llm,native,native,native --track partial \
  --provider zai --model glm-5.3 --reasoning-effort low \
  --max-output-tokens 4096 --spend-cap 1 \
  --input-per-million 1.4 --output-per-million 4.4 \
  --steps 16 --run-dir runs/agents/glm-demo
```

The example prices are the published uncached input/output USD-per-million rates checked on 2026-10-02; verify [Z.ai pricing](https://docs.z.ai/guides/overview/pricing) before using them. Cached inputs are conservatively charged at the uncached rate by this local estimator. Provider billing remains authoritative. GLM-5.3 requires thinking enabled; the adapter preserves reasoning content across tool interactions. The output cap includes the provider's generated tokens, so a too-small limit can truncate a tool response. See [GLM-5.3 documentation](https://docs.z.ai/guides/llm/glm-5.3).

Each decision has bounded model turns and tool calls. Cost is reserved in a persistent ledger before dispatch. An ambiguous network failure retains its reservation, and retries are not hidden inside the HTTP client. The same ledger is shared across live tournament episodes. A provider failure remains visible even when a fallback permits the simulation to finish.

To use Anthropic, choose `--provider anthropic`, an explicit `--model`, that model's current input/output rates, and `ANTHROPIC_API_KEY`. There is no automatic paid fallback between providers.

## Resume and replay

```sh
python -m ashfall_agents play \
  --config config/presets/agent-demo.toml \
  --controllers fixture,native,native,native --track partial \
  --steps 8 --resume --run-dir runs/agents/demo
python -m ashfall_agents replay --run runs/agents/demo
```

`--steps` is the desired total macro-step count, not additional steps. Resume with matching controller, provider, model, memory, cadence, and budget settings. Resume rejects changed agent code, wrapper, or simulator binary. A new build may therefore require a new experiment even when the source changes appear small.

Atomic checkpoint directories preserve the session and controller state together. Checkpoints occur after completed macro steps. A crash during a model decision may repeat that decision on resume; prior request reservations remain charged to the local ceiling. Recorded-action replay verifies the simulation digest without making model calls. It does not assert that a new model invocation would choose identical actions.

## Privileged planning

The `privileged` track exposes `simulate_candidates` to the model. The `search` controller is a non-LLM fixed-candidate planning baseline available in that track. Branches reconstruct the same recorded session and library, verify their starting digest, then test bounded candidate actions against configured native-policy opponents.

```sh
python -m ashfall_agents play \
  --config config/presets/agent-demo.toml \
  --controllers search,native,native,native --track privileged \
  --steps 2 --run-dir runs/agents/search-demo
```

The parent process enforces a wall-time deadline for reconstruction and branch execution in an isolated worker. It terminates overdue workers and records incomplete results. Only completed candidates are eligible for ranking. Branches never advance the live session. Outcomes depend on the declared native-opponent assumption, and exact-state rollouts are privileged even when their returned observations are masked.

## Evaluation

```sh
python -m ashfall_agents tournament \
  --suite evaluations/strategy_smoke.json --run-dir runs/agents/smoke
```

The smoke suite runs eight offline scripted/fixture episodes. Development and held-out suites provide separate seed schedules and kingdom rotations. Their checked-in controller choices are offline by default. For a live study, copy a suite, replace `fixture` with `llm`, select `use_memory`, and supply explicit provider/model/pricing/spend arguments to `tournament`. Use `search` only with the privileged track. Keep matched seeds and compute budgets when comparing memory or planning variants.

Generated evaluation summaries include measured outcomes, fallback counts, runtime, and cost, with uncertainty summaries when applicable. A small demonstration is not the planned twenty-seed live benchmark. Follow the [evaluation protocol](AGENT_EVALUATION_PROTOCOL.md), retain failures in the denominator, and do not infer successful RL training from these agent tests.

## Validation status

The initial implementation was checked with the C++/bridge suite, offline controller tests, mocked provider round trips, malformed-response and budget-resume regressions, CLI play/resume/replay, and an eight-episode scripted tournament. Real isolated branch execution and forced timeout behavior were also tested. No live GLM or Anthropic performance result is claimed by these checks; see the dated validation log for counts and execution details.

Local credentials: set `ZAI_API_KEY` in the project-root `.env` file (see `.env.example`) or export it in the process environment. The environment takes precedence, including an empty value. The `.env` file is ignored by Git; keep it readable only by your user (`chmod 600 .env`). Agent commands load this file automatically; the ENSO dashboard does too.

## Systematic campaign runner

`evaluations/systematic/frozen-design.json` records the fixed development and held-out design. The systematic suites use 256 macro steps (1,024 simulation ticks) and a decision every 16 macro steps. The held-out sample uses twenty independent seeds and one balanced, rotated position per seed; this is explicitly smaller than four positions per seed.

```sh
PYTHONPATH=python python scripts/run_agent_campaign_stage.py \
  --stage development --workers 1 \
  --run-root runs/agents/systematic-development-v2
```

Set `ZAI_CAMPAIGN_LEDGER` and `ZAI_CAMPAIGN_PROJECT=ashfall` to join the shared token campaign. Each shard keeps a separate local USD ledger. Use one worker until provider account concurrency has been established. The campaign script writes an immutable stage manifest, incremental per-trial status, local replay checks, latency summaries, and paired seed-cluster comparisons. `--resume` skips completed and failed trials and resumes interrupted or pending trials only when implementation, model settings and suite fingerprints match.

To stop cooperatively, create a file named `STOP` in the stage run directory. The current request finishes; the next request is prevented and the episode is checkpointed as cancelled. Remove the file only when intentionally resuming. Interrupted outcomes are retained but are not presented as fixed-horizon survival or population measurements.

Agents receive the public action catalog, current permitted observation, survival objective and decision duration upfront. Allocation values are normalized effort weights, not currency. Construction prerequisites, resource costs, and maintenance semantics come from simulator rules. The `privileged` suite fixes candidate batches to three actions and eight macro steps, matching the search baseline's maximum branch budget; it records whether the model actually uses planning. Structure-type inventory is not exposed by the current public observation, and remains an information limitation.
