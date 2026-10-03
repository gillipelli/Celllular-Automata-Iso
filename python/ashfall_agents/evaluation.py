"""Variant summaries and paired comparisons clustered by independent world seed."""
from collections import defaultdict
import numpy as np

METRICS = ('population', 'survival', 'original_generation_survival', 'current_slot_alive',
           'functional_nodes', 'grain', 'fallbacks', 'validation_failures', 'decision_seconds',
           'input_tokens', 'output_tokens', 'estimated_cost', 'completion')


def episode_metrics(episode):
    result = episode.get('result', {})
    observations = result.get('final_observations', [])
    own = observations[episode['position']] if len(observations) > episode['position'] else {}
    completed = episode.get('status', result.get('status', 'completed')) == 'completed'
    # A cancelled/failed checkpoint is not the declared fixed-horizon outcome.
    if not completed:
        own = {}
    original = result.get('original_generation_survival') if completed else None
    survival = float(original[episode['position']]) if original is not None else None
    slots = result.get('current_slot_alive') if completed else None
    slot = float(slots[episode['position']]) if slots is not None else (float(own.get('alive', False)) if own else None)
    usage = result.get('usage', [])
    return dict(population=float(own['population']) if 'population' in own else None,
        survival=survival, original_generation_survival=survival, current_slot_alive=slot,
        functional_nodes=float(own.get('functional_nodes', 0)) if own else None,
        grain=float(own.get('stock', {}).get('grain', 0)) if own else None,
        fallbacks=result.get('fallbacks'), validation_failures=result.get('validation_failures'),
        decision_seconds=result.get('decision_seconds'),
        input_tokens=sum(u.get('input_tokens', 0) for u in usage),
        output_tokens=sum(u.get('output_tokens', 0) for u in usage),
        estimated_cost=result.get('estimated_cost'),
        completion=float(episode.get('status', result.get('status', 'completed')) == 'completed'))


def seed_interval(values):
    means = np.array([np.mean(rows) for rows in values.values()])
    entry = {'mean': float(means.mean()) if len(means) else None, 'independent_seeds': len(means),
             'seed_bootstrap_95_percent_interval': None}
    if len(means) >= 2:
        draws = np.random.default_rng(2026).choice(means, size=(2000, len(means)), replace=True).mean(axis=1)
        entry['seed_bootstrap_95_percent_interval'] = np.quantile(draws, [.025, .975]).tolist()
    return entry


def summarize(episodes):
    grouped = defaultdict(list)
    for episode in episodes:
        grouped[episode.get('variant', episode['controller'])].append(episode)
    output = {}
    for variant, rows in grouped.items():
        attempted = [row for row in rows if row.get('status') not in ('pending', 'running')]
        stats = {}
        for metric in METRICS:
            values = defaultdict(list)
            for row in attempted:
                value = episode_metrics(row)[metric]
                if value is not None:
                    values[row['seed']].append(value)
            stats[metric] = seed_interval(values)
            stats[metric]['missing_episodes'] = sum(episode_metrics(row)[metric] is None for row in attempted)
        durations = [duration for row in attempted for duration in row.get('result', {}).get('decision_latencies', [])]
        output[variant] = dict(episodes=len(rows), attempted_episodes=len(attempted),
            failed_episodes=sum(row.get('status', row.get('result', {}).get('status')) in ('failed', 'cancelled') for row in rows),
            metrics=stats, decision_latency_seconds={
                'count': len(durations), 'p50': float(np.quantile(durations, .5)) if durations else None,
                'p95': float(np.quantile(durations, .95)) if durations else None})
    return output


def paired_comparisons(episodes, reference=None):
    """Match scenario/seed/position/repeat first, then bootstrap seed clusters.

    Incomplete or failed pairs remain counted, but truncated strategic outcomes
    cannot be compared with a completed fixed-horizon outcome.
    """
    grouped = defaultdict(dict)
    for row in episodes:
        key = (row.get('scenario', 'default'), row['seed'], row['position'], row.get('repeat', 0))
        variant = row.get('variant', row['controller'])
        if key in grouped[variant]:
            raise ValueError('duplicate variant trial identity')
        grouped[variant][key] = row
    if reference is None or reference not in grouped:
        return {}
    output = {}
    for variant, rows in grouped.items():
        if variant == reference:
            continue
        keys = rows.keys() & grouped[reference].keys()
        usable = [key for key in keys if all(
            row.get('status', row.get('result', {}).get('status', 'completed')) == 'completed'
            for row in (rows[key], grouped[reference][key]))]
        metrics = {}
        for metric in METRICS:
            values = defaultdict(list)
            for key in usable:
                left, right = episode_metrics(rows[key])[metric], episode_metrics(grouped[reference][key])[metric]
                if left is not None and right is not None:
                    values[key[1]].append(left - right)
            metrics[metric] = seed_interval(values)
        output[f'{variant}-minus-{reference}'] = dict(planned_pairs=len(keys), completed_pairs=len(usable),
            excluded_incomplete_pairs=len(keys)-len(usable), metrics=metrics,
            direction='variant minus reference; positive favors variant only for benefit metrics')
    return output
