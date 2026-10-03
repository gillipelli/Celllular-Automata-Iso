"""Privileged exact-state planning in a worker terminated at its deadline.

The deadline includes process startup, full session reconstruction, and rollouts.
Only completed candidate horizons are returned; the live environment is never
passed to the worker. Partial-observation controllers never receive this tool.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
import math
import multiprocessing as mp
import time
import numpy as np
from .schemas import KingdomAction


def _planning_worker(connection, env_type, path, library, expected_digest,
                     kingdom_id, candidates, horizon):
    try:
        for candidate in candidates:
            action = KingdomAction.model_validate(candidate)
            kwargs = {'library': library} if library else {}
            with env_type.load(path, **kwargs) as branch:
                if branch.digest() != expected_digest:
                    raise ValueError('branch reconstruction digest mismatch')
                rewards, steps = 0., 0
                for _ in range(horizon):
                    spec = branch.describe_environment()
                    if spec['tick'] >= spec['episode_ticks']:
                        break
                    actions = np.tile(KingdomAction.balanced().vector(branch.num_agents), (branch.num_agents, 1))
                    actions[kingdom_id] = action.vector(branch.num_agents, kingdom_id)
                    mask = np.zeros(branch.num_agents, dtype=bool)
                    mask[kingdom_id] = True
                    _, reward, _, done, _ = branch.step_mixed(actions, mask)
                    rewards += float(reward[kingdom_id])
                    steps += 1
                    if done:
                        break
                # Incomplete branches are not usable in a like-for-like ranking.
                if steps == horizon:
                    connection.send(('result', dict(action=action.model_dump(), cumulative_reward=rewards,
                        steps=steps, outcome=branch.observe_summary(kingdom_id),
                        assumption='Exact-state privileged oracle; all other kingdoms use configured native policy.')))
        connection.send(('completed', None))
    except BaseException as error:
        connection.send(('error', f'{type(error).__name__}: {error}'))
    finally:
        connection.close()


def evaluate_candidates(env, kingdom_id, candidates, horizon=8, max_seconds=30):
    if not isinstance(candidates, list) or not 1 <= len(candidates) <= 3:
        raise ValueError('one to three candidates required')
    if type(horizon) is not int or not 1 <= horizon <= 8:
        raise ValueError('horizon must be an integer between one and eight')
    if type(kingdom_id) is not int or not 0 <= kingdom_id < env.num_agents:
        raise ValueError('invalid kingdom id')
    if not isinstance(max_seconds, (int, float)) or not math.isfinite(max_seconds) or max_seconds <= 0:
        raise ValueError('max_seconds must be finite and positive')
    spec = env.describe_environment()
    interval = spec.get('macro_interval', 1)
    remaining = (spec['episode_ticks'] - spec['tick'] + interval - 1) // interval
    horizon = min(horizon, remaining)
    if horizon <= 0:
        raise ValueError('episode finished')
    parsed = [KingdomAction.model_validate(c) for c in candidates]
    for action in parsed:
        action.vector(env.num_agents, kingdom_id)
    live_digest, start = env.digest(), time.monotonic()
    deadline = start + max_seconds
    results, completed, timed_out = [], False, False
    with TemporaryDirectory(prefix='ashfall-branch-') as tmp:
        path = Path(tmp) / 'session.json'
        env.save(path)
        context = mp.get_context('spawn')
        receiver, sender = context.Pipe(duplex=False)
        library = str(Path(env.lib._name).resolve()) if hasattr(env, 'lib') else None
        worker = context.Process(target=_planning_worker, args=(sender, type(env), path, library,
            live_digest, kingdom_id, [action.model_dump() for action in parsed], horizon), daemon=True)
        try:
            if time.monotonic() < deadline:
                worker.start()
                sender.close()
                while True:
                    remaining_seconds = deadline - time.monotonic()
                    if remaining_seconds <= 0 or not receiver.poll(remaining_seconds):
                        timed_out = True
                        break
                    try:
                        kind, payload = receiver.recv()
                    except EOFError:
                        raise RuntimeError('planning worker exited without a result') from None
                    if kind == 'result':
                        results.append(payload)
                    elif kind == 'completed':
                        completed = True
                        break
                    elif kind == 'error':
                        raise RuntimeError(f'planning worker failed: {payload}')
                    else:
                        raise RuntimeError('invalid planning worker message')
            else:
                timed_out = True
        finally:
            sender.close()
            receiver.close()
            if worker.pid is not None:
                if worker.is_alive():
                    worker.terminate()
                worker.join(timeout=.1)
                if worker.is_alive():
                    worker.kill()
                    worker.join(timeout=.1)
                worker.close()
    if env.digest() != live_digest:
        raise RuntimeError('planning changed live environment')
    return {'track': 'privileged', 'effective_horizon': horizon, 'results': results,
            'elapsed_seconds': time.monotonic() - start, 'timed_out': timed_out,
            'incomplete': not completed or len(results) != len(parsed)}
