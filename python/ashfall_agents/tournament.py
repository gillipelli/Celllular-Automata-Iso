"""Incremental, resumable, preregistered experiment execution."""
from __future__ import annotations

import hashlib
import json
import os
import re
from pathlib import Path

from ashfall_env import AshfallEnv
from .evaluation import paired_comparisons, summarize
from .runner import load_checkpoint, run_episode, simulator_fingerprint, write_json


def build_trials(suite):
    variants = suite.get("variants") or [
        {"id": name, "controller": name} for name in suite.get("controllers", ["scripted", "fixture"])
    ]
    names = [variant["id"] for variant in variants]
    if len(set(names)) != len(names) or any(not re.fullmatch(r"[A-Za-z0-9_-]+", name) for name in names):
        raise ValueError("variant IDs must be unique safe path components")
    repeats = suite.get("repeats", 1)
    if type(repeats) is not int or repeats < 1:
        raise ValueError("repeats must be positive")
    seeds = suite["seeds"]
    positions = suite.get("positions", [0])
    if not seeds or len(set(seeds)) != len(seeds) or any(type(seed) is not int or seed < 0 for seed in seeds):
        raise ValueError("seeds must be unique nonnegative integers")
    kingdoms = suite.get("kingdoms", 4)
    if not positions or len(set(positions)) != len(positions) or any(type(p) is not int or not 0 <= p < kingdoms for p in positions):
        raise ValueError("positions must be unique valid kingdom indices")
    trials = []
    positions_by_seed = suite.get("positions_by_seed", {})
    if set(positions_by_seed) - {str(seed) for seed in seeds}:
        raise ValueError("positions_by_seed contains unknown seed")
    for seed in seeds:
        seed_positions = positions_by_seed.get(str(seed), positions)
        if not seed_positions or len(set(seed_positions)) != len(seed_positions) or any(type(p) is not int or not 0 <= p < kingdoms for p in seed_positions):
            raise ValueError("invalid per-seed positions")
        for position in seed_positions:
            for repeat in range(repeats):
                for variant in variants:
                    controller = variant["controller"]
                    track = variant.get("track", suite.get("track", "partial"))
                    cadence = variant.get("decision_interval", suite.get("decision_interval", 4))
                    steps = variant.get("steps", suite.get("steps", 16))
                    if controller not in ("native", "scripted", "fixture", "search", "llm"):
                        raise ValueError("unknown tournament controller")
                    if track not in ("partial", "privileged") or (controller == "search" and track != "privileged"):
                        raise ValueError("search requires privileged track")
                    if type(cadence) is not int or not 1 <= cadence <= 16 or type(steps) is not int or steps < 1:
                        raise ValueError("invalid cadence or steps")
                    trials.append(dict(id=f"{seed}-{position}-{variant['id']}-r{repeat}",
                        seed=seed, position=position, repeat=repeat, variant=variant["id"], controller=controller,
                        scenario=suite.get("scenario", "default"), track=track, steps=steps,
                        decision_interval=cadence, planning_budget=variant.get("planning_budget", suite.get("planning_budget")), use_memory=variant.get("use_memory", suite.get("use_memory", True))))
    return trials


def run_tournament(suite, run_dir, client_factory=None, resume=False):
    trials = build_trials(suite)
    directory = Path(run_dir)
    directory.mkdir(parents=True, exist_ok=True)
    live = any(trial["controller"] == "llm" for trial in trials)
    client = client_factory() if live and client_factory else None
    if live and client is None:
        raise ValueError("live tournament requires a provider factory")
    config = Path(suite["config"]).read_text()
    with AshfallEnv(config, trials[0]["seed"]) as identity_env:
        simulator = simulator_fingerprint(identity_env)
    identity = dict(simulator=simulator, suite=suite, config_hash=hashlib.sha256(config.encode()).hexdigest(),
        source_hash=hashlib.sha256(b"".join(p.read_bytes() for p in sorted(Path(__file__).parent.glob("*.py")))).hexdigest(),
        provider_settings=client.settings() if client is not None and hasattr(client, "settings") else None)
    manifest_path = directory / "tournament_manifest.json"
    index_path = directory / "tournament.json"
    if manifest_path.exists():
        if not resume:
            raise ValueError("tournament exists; use --resume or a fresh directory")
        if json.loads(manifest_path.read_text()) != identity:
            raise ValueError("tournament source, suite, config, or provider settings changed")
        records = json.loads(index_path.read_text())["episodes"]
    else:
        if resume:
            raise ValueError("no tournament manifest to resume")
        records = [dict(trial, status="pending") for trial in trials]
        write_json(manifest_path, identity)
    if client is not None:
        client.bind_ledger(directory / "budget.json")

    def persist():
        write_json(index_path, dict(label="live model evaluation" if live else "offline fixture/baseline evaluation; no model efficacy claim",
            planned_episodes=len(trials), episodes=records, summary=summarize(records),
            paired_comparisons=paired_comparisons(records, suite.get("comparison_reference"))))

    persist()
    for record in records:
        if record["status"] in ("completed", "failed"):
            continue  # Failed trials remain in the denominator; no silent replacements.
        episode_dir = directory / record["id"]
        recover = (episode_dir / "checkpoint.json").exists()
        record["status"] = "running"
        persist()
        names = ["native"] * suite.get("kingdoms", 4)
        names[record["position"]] = record["controller"]
        previous_campaign_run = os.environ.get("ZAI_CAMPAIGN_RUN")
        os.environ["ZAI_CAMPAIGN_RUN"] = f"{directory.name}/{record['id']}"
        try:
            env = AshfallEnv.load(load_checkpoint(episode_dir)[1]) if recover else AshfallEnv(config, record["seed"])
            with env:
                if env.num_agents != len(names):
                    raise ValueError("suite kingdom count differs from environment")
                result = run_episode(env, episode_dir, names, client if record["controller"] == "llm" else None,
                    track=record["track"], steps=record["steps"], decision_interval=record["decision_interval"],
                    use_memory=record["use_memory"], resume=recover, planning_budget=record.get("planning_budget"),
                    budget_ledger=directory / "budget.json" if client else None)
            record.update(result=result, status=result["status"])
        except KeyboardInterrupt:
            record["status"] = "cancelled"
        except Exception as error:
            record.update(status="failed", error=f"{type(error).__name__}: {error}")
            if (episode_dir / "result.json").exists():
                record["result"] = json.loads((episode_dir / "result.json").read_text())
        finally:
            if previous_campaign_run is None:
                os.environ.pop("ZAI_CAMPAIGN_RUN", None)
            else:
                os.environ["ZAI_CAMPAIGN_RUN"] = previous_campaign_run
        persist()
        if record["status"] == "cancelled":
            break
    return json.loads(index_path.read_text())
