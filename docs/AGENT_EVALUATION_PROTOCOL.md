# Agent evaluation protocol

This protocol separates functional correctness, model behavior, and strategy performance. A passing offline test verifies the surrounding software; it does not demonstrate that an LLM chose a successful strategy. Reports must identify their provider and controller types, including scripted or fixture controllers.

## Information and planning tracks

The **partial** track permits a kingdom's own state, the environmental information exposed by the simulation's observation rules, covered rival observations, and its own diplomatic information. It must not expose the true world seed, other controllers' current decisions, complete joint action history, or hidden rival state to the model. Seed and digest belong in evaluator artifacts, not model-facing observations. Memory is scoped to episode, kingdom, and generation.

The **privileged** track additionally permits bounded exact-state simulation branches. Masking the observations returned by a branch does not make its results partially observed: the outcome itself depends on hidden state. Reports must label this advantage. Comparing privileged planning with a partial controller measures the combined effect of information and planning, not planning alone.

The existing native policies use simulator internals and are practical opponents, not information-matched partial-observation baselines. For a controlled comparison, use an observation-only scripted controller. Compare privileged LLM planning with a non-LLM candidate-search controller that receives the same branch access and compute budget.

## Recorded experiments

Each experiment records the source revision or source hash, environment configuration, world seed, controlled kingdom and generation, controller settings, prompt/tool hashes, exact model identifier when used, decision cadence, maximum steps, and budget. Preserve failures and exhausted budgets in the denominator. Artifact reports should be readable without an API key.

Choose seeds and kingdom rotations before examining results. Use separate development and held-out seed lists; do not repeatedly tune on the held-out set. A useful first live study uses five development seeds and twenty held-out seeds with controlled positions rotated, followed by repeated model trials on a subset. Reduce the study size explicitly when the available API budget cannot support it; do not describe a smoke demonstration as that full study.

All controllers in a joint step receive information from the same tick. Commit their decisions together after validation. A timeout or invalid proposal uses the documented fallback and remains visible in the trace. Actions for inactive kingdoms and newly founded successors require explicit coordinator handling.

## Measures

Report survival of the original kingdom generation separately from survival or population of a successor. Also report final population, stock levels, functional infrastructure, and observed treaty outcomes. State the measurement horizon. Population and resource outcomes should be computed from simulation data, not estimated by a language model.

Record model calls, input/output tokens, estimated spend, tool failures, invalid proposals, repair attempts, fallbacks, and latency. For planning, include reconstruction time, candidate count, branch horizon, and opponent assumptions. Compare methods at matched world conditions and show paired per-seed results. Report uncertainty when enough trials exist; a small sample should be identified as such.

Memory and planning ablations must preserve the other settings. A memory comparison uses the same observation rules, prompts except for memory access, model, action budget, cadence, and seed schedule. A planning comparison uses the same candidate and rollout budgets where applicable.

## Regression gates

- Invalid external actions fail before the world changes.
- All-native mixed stepping matches the existing native simulation path.
- All-external mixed stepping matches the existing action-driven path.
- Saved mixed sessions restore controller masks and reproduce recorded digests.
- Branch reconstruction verifies its starting digest and never mutates the live world.
- Partial observations and memory do not contain evaluator-only seed or digest information.
- Reusing a kingdom slot for a new generation does not reuse the predecessor's memory.
- Report rendering escapes model-generated text.
- Provider failures, budget exhaustion, and cancellation produce explicit records.

## Claims supported by evidence

Passing replay tests supports deterministic simulation and recorded-action reproducibility. It does not imply that rerunning a hosted model produces the same decisions. Passing a provider fixture supports protocol handling; a live call is needed to demonstrate provider integration in this environment. Neither proves strategic superiority.

When reporting a live result, retain the complete evaluated sample and identify whether the controller was partial or privileged. Do not infer general intelligence, calibrated economic behavior, successful PPO training, or universal improvement from a tournament result.
