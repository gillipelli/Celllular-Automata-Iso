from __future__ import annotations
import hashlib
import html
import json
import os
from pathlib import Path
import subprocess
import time
from .planning import evaluate_candidates
import numpy as np
from .memory import StrategyMemory
from .provider import FixtureClient
from .runtime import AgentController
from .schemas import KingdomAction

def write_json(path, data):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, indent=2) + '\n')
    os.replace(temporary, path)

def simulator_fingerprint(env):
    wrapper=Path(__file__).resolve().parents[1]/'ashfall_env.py'
    result={'wrapper':hashlib.sha256(wrapper.read_bytes()).hexdigest()}
    if hasattr(env,'lib'):
        result['library']=hashlib.sha256(Path(env.lib._name).read_bytes()).hexdigest()
    return result

def load_checkpoint(run_dir):
    run_dir = Path(run_dir)
    pointer = json.loads((run_dir/'checkpoint.json').read_text())
    if 'directory' in pointer:
        directory = run_dir/pointer['directory']
        return json.loads((directory/'state.json').read_text()), directory/'session.json'
    return pointer, run_dir/'session.json'

def render_report(run_dir):
    run_dir = Path(run_dir)
    result = json.loads((run_dir/'result.json').read_text())
    events = [json.loads(line) for line in (run_dir/'events.jsonl').read_text().splitlines()]
    rows = ''.join('<details><summary>' + html.escape(str(e.get('kind'))) + '</summary><pre>' +
        html.escape(json.dumps(e, indent=2)) + '</pre></details>' for e in events)
    (run_dir/'report.html').write_text('<!doctype html><meta charset="utf-8"><title>Ashfall agent trace</title>'
        '<style>body{max-width:1000px;margin:40px auto;font:16px system-ui}pre{white-space:pre-wrap;overflow-wrap:anywhere}details{padding:8px;border-bottom:1px solid #ccc}</style>'
        '<h1>Ashfall strategy run</h1><pre>' + html.escape(json.dumps(result,indent=2)) + '</pre>' + rows)
    return run_dir/'report.html'

def run_episode(env, run_dir, controllers=None, client=None, track='partial', steps=16,
                decision_interval=4, use_memory=True, resume=False, budget_ledger=None, planning_budget=None):
    if type(steps) is not int or steps < 1 or not 1 <= decision_interval <= 16:
        raise ValueError('positive steps and decision interval in [1,16] required')
    if track not in ('partial','privileged'):
        raise ValueError('track must be partial or privileged')
    if planning_budget is not None and planning_budget != {'candidates':3, 'horizon':8}:
        raise ValueError('matched planning budget must use three candidates and eight macro steps')
    run_dir = Path(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)
    controllers = controllers or ['fixture'] + ['native'] * (env.num_agents-1)
    if len(controllers) != env.num_agents or any(c not in ('native','scripted','fixture','llm','search') for c in controllers):
        raise ValueError('one of native, scripted, fixture, llm, search per kingdom')
    if 'search' in controllers and track != 'privileged':
        raise ValueError('search baseline requires privileged track')
    if 'llm' in controllers and (client is None or isinstance(client,FixtureClient)):
        raise ValueError('llm controller requires a live model client')
    if (run_dir/'manifest.json').exists() and not resume:
        raise ValueError('run directory already exists; choose a fresh directory or resume')
    memory = StrategyMemory()
    held, metrics, start_step = {}, [], 0
    if resume:
        saved, _ = load_checkpoint(run_dir)
        if saved['digest'] != env.digest():
            raise ValueError('resume environment does not match checkpoint')
        manifest = json.loads((run_dir/'manifest.json').read_text())
        current_hash = hashlib.sha256(b''.join(p.read_bytes() for p in sorted(Path(__file__).parent.glob('*.py')))).hexdigest()
        if manifest.get('simulator') != simulator_fingerprint(env):
            raise ValueError('simulator implementation changed since checkpoint')
        if manifest['source_hash'] != current_hash or manifest['config'] != env.config or manifest['model'] != getattr(client,'model',None):
            raise ValueError('resume source, model or config changed')
        if manifest['controllers'] != controllers or manifest['track'] != track or manifest['decision_interval'] != decision_interval or manifest['use_memory'] != use_memory or manifest.get('planning_budget') != planning_budget:
            raise ValueError('resume controller configuration changed')
        memory.records, metrics, start_step = saved['memory'], saved['metrics'], saved['step']
        held = {int(k):(KingdomAction.model_validate(v[0]),v[1],v[2]) for k,v in saved['held'].items()}
        if client is not None and hasattr(client,'spent'):
            client.spent = max(client.spent, saved.get('reserved_spend',0))
    else:
        try:
            revision = subprocess.check_output(['git','rev-parse','HEAD'],cwd=Path(__file__).resolve().parents[2],text=True).strip()
        except (OSError,subprocess.CalledProcessError):
            revision = 'unknown'
        sources = sorted(Path(__file__).parent.glob('*.py'))
        initial_observations = [env.observe_summary(k) for k in range(env.num_agents)]
        manifest = dict(initial_generations=[o['generation'] for o in initial_observations],
            initial_alive=[o.get('alive',True) for o in initial_observations],format='ASHFALL-AGENT-RUN-1',controllers=controllers,track=track,
            seed=env.seed,config=env.config,decision_interval=decision_interval,use_memory=use_memory,planning_budget=planning_budget,
            simulator=simulator_fingerprint(env),model=getattr(client,'model',None),provider_settings=client.settings() if client is not None and hasattr(client,'settings') else None,code_revision=revision,
            source_hash=hashlib.sha256(b''.join(p.read_bytes() for p in sources)).hexdigest(),
            fixture_disclaimer='fixture is scripted tool execution, not an LLM result',
            native_policy_information='Native opponents can inspect internal state; not observation-matched baselines.')
        write_json(run_dir/'manifest.json',manifest)
    if client is not None and hasattr(client,'bind_ledger'):
        client.bind_ledger(budget_ledger or run_dir/'budget.json')
    if not resume:
        manifest['starting_spend'] = getattr(client,'spent',0)
        manifest['starting_usage'] = len(getattr(client,'usage',[]))
        write_json(run_dir/'manifest.json',manifest)
    initial_spend = manifest.get('starting_spend',0)
    initial_usage = manifest.get('starting_usage',0)
    episode = str(run_dir.resolve())
    agents = {k:AgentController(FixtureClient() if kind=='fixture' else client,memory,episode,track,use_memory,decision_interval,steps,planning_budget)
              for k,kind in enumerate(controllers) if kind in ('fixture','llm')}
    def checkpoint(step):
        directory = run_dir/'checkpoints'/f'{step:06d}'
        directory.mkdir(parents=True, exist_ok=True)
        env.save(directory/'session.json')
        state = dict(step=step,digest=env.digest(),memory=memory.records,metrics=metrics,
            final_observations=[env.observe_summary(k) for k in range(env.num_agents)],
            held={str(k):[v[0].model_dump(),v[1],v[2]] for k,v in held.items()},
            reserved_spend=getattr(client,'spent',0))
        write_json(directory/'state.json',state)
        write_json(run_dir/'checkpoint.json',{'directory':str(directory.relative_to(run_dir))})
    if not resume:
        checkpoint(0)
    event_path = run_dir/'events.jsonl' 
    with event_path.open('a' if resume else 'w') as events:
        def trace(event):
            events.write(json.dumps(event)+'\n'); events.flush()
        status, executed = 'completed', start_step
        try:
            for step in range(start_step,steps):
                spec = env.describe_environment()
                if spec['tick'] >= spec['episode_ticks']:
                    break
                # Snapshot all permitted observations before any controller decides.
                observations = [env.observe_summary(k) for k in range(env.num_agents)]
                actions = np.tile(KingdomAction.balanced().vector(env.num_agents), (env.num_agents,1))
                mask = np.array([c!='native' for c in controllers])
                intents = {}
                for k,kind in enumerate(controllers):
                    if not observations[k].get('alive',True):
                        held.pop(k,None)
                        # Keep the explicit external mask so a successor founded during
                        # this macro step cannot silently inherit native control.
                        continue
                    if kind == 'native':
                        continue
                    if kind == 'search':
                        generation = observations[k]['generation']
                        if k not in held or step % decision_interval == 0 or held[k][2] != generation:
                            started = time.monotonic()
                            candidates = [KingdomAction.balanced() for _ in range(3)]
                            candidates[1].allocation=[1,1,6,2,1,1,1,1]
                            candidates[2].allocation=[1,1,3,5,1,1,1,1]
                            rollout=evaluate_candidates(env,k,[a.model_dump() for a in candidates])
                            trace(dict(kind='search_baseline',kingdom=k,result=rollout))
                            complete=[r for r in rollout['results'] if r['steps']==rollout['effective_horizon']]
                            best=max(complete,key=lambda r:r['cumulative_reward']) if complete else None
                            action=KingdomAction.model_validate(best['action']) if best else candidates[0]
                            intent='Privileged fixed candidate search with native-policy opponents'
                            held[k]=action,intent,generation
                            metrics.append(dict(step=step,kingdom=k,fallback=best is None,tool_calls=1,failures=0,
                                                seconds=time.monotonic()-started))
                        else:
                            action,intent,_=held[k]
                    elif kind == 'scripted':
                        action,intent = KingdomAction.balanced(),'Observation-matched fixed balanced baseline'
                    else:
                        generation = observations[k]['generation']
                        if k not in held or step % decision_interval == 0 or held[k][2] != generation:
                            action,intent,metric = agents[k].decide(env,k,observations[k],trace)
                            metrics.append(dict(step=step,kingdom=k,**metric))
                            held[k] = action,intent,generation
                        else:
                            action,intent,_ = held[k]
                    actions[k] = action.vector(env.num_agents,k)
                    intents[k] = intent
                _,rewards,terminated,truncated,info = env.step_mixed(actions,mask)
                after = [env.observe_summary(k) for k in range(env.num_agents)]
                for k in intents:
                    memory.record(episode,k,observations[k],after[k],actions[k].tolist(),intents[k])
                trace(dict(kind='step',step=step,digest=env.digest(),rewards=rewards.tolist(),
                    terminated=terminated.tolist(),truncated=truncated,observations=after))
                executed = step+1
                checkpoint(executed)
                if truncated:
                    break
        except KeyboardInterrupt:
            status='cancelled'
            trace(dict(kind='cancelled'))
        except Exception as error:
            status='failed'
            trace(dict(kind='error',error=str(error)))
            raise
        finally:
            # Public replay artifact reflects the last successfully checkpointed step.
            saved, session_path = load_checkpoint(run_dir)
            (run_dir/'session.json').write_bytes(session_path.read_bytes())
            final_observations = saved.get('final_observations')
            if final_observations is None:
                # Compatibility with older checkpoints that omitted observations.
                kwargs={'library':env.lib._name} if hasattr(env,'lib') else {}
                with type(env).load(session_path,**kwargs) as restored:
                    final_observations=[restored.observe_summary(k) for k in range(restored.num_agents)]
            committed_metrics=saved['metrics']
            original_generations=manifest.get('initial_generations')
            original_survival=([bool(o.get('alive',True) and o['generation']==original_generations[k]
                                     and manifest.get('initial_alive',[True]*len(final_observations))[k])
                                for k,o in enumerate(final_observations)] if original_generations is not None else None)
            result = dict(status=status,steps=saved['step'],digest=saved['digest'],track=track,
                controllers=controllers,decisions=len(committed_metrics),attempted_decisions=len(metrics),
                fallbacks=sum(m['fallback'] for m in committed_metrics),
                validation_failures=sum(m['failures'] for m in committed_metrics),
                decision_seconds=sum(m['seconds'] for m in committed_metrics),
                decision_latencies=[m['seconds'] for m in committed_metrics],
                tool_calls=sum(m['tool_calls'] for m in committed_metrics),
                final_observations=final_observations,initial_generations=original_generations,
                original_generation_survival=original_survival,
                current_slot_alive=[bool(o.get('alive',True)) for o in final_observations],
                estimated_cost=getattr(client,'spent',0)-initial_spend,
                cumulative_reserved_spend=getattr(client,'spent',0),usage=getattr(client,'usage',[])[initial_usage:])
            write_json(run_dir/'result.json',result)
    render_report(run_dir)
    return result
