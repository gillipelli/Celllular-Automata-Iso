import json
from pathlib import Path
import pytest
from ashfall_agents.tournament import build_trials, run_tournament
from ashfall_agents.evaluation import paired_comparisons, summarize


def suite(tmp_path):
    config = tmp_path/'config.toml'
    config.write_text('fixture')
    return dict(config=str(config), seeds=[1,2], positions=[0], kingdoms=2, steps=8,
                decision_interval=8, variants=[{'id':'memory','controller':'fixture'},
                {'id':'no-memory','controller':'fixture','use_memory':False}], repeats=2,
                comparison_reference='no-memory')


def test_trial_identity_and_cadence(tmp_path):
    trials=build_trials(suite(tmp_path))
    assert len(trials)==8 and len({row['id'] for row in trials})==8
    assert all(row['decision_interval']==8 for row in trials)
    assert all(row['use_memory'] is False for row in trials if row['variant']=='no-memory')


def test_incremental_failure_and_resume_preserve_denominator(tmp_path,monkeypatch):
    import ashfall_agents.tournament as module
    from test_agents import FakeEnv
    class Environment(FakeEnv):
        def __init__(self,config='',seed=0):
            super().__init__(); self.seed=seed
    monkeypatch.setattr(module,'AshfallEnv',Environment)
    calls=[]
    def episode(env,path,*args,**kwargs):
        calls.append((path,kwargs))
        if len(calls)==1:
            raise ValueError('injected failure')
        return dict(status='completed',final_observations=[dict(population=5),dict(population=3)],
                    fallbacks=0,validation_failures=0,decision_seconds=0)
    monkeypatch.setattr(module,'run_episode',episode)
    config=suite(tmp_path)
    result=run_tournament(config,tmp_path/'run')
    assert len(calls)==8 and result['episodes'][0]['status']=='failed'
    assert all(options['decision_interval']==8 for _,options in calls)
    run_tournament(config,tmp_path/'run',resume=True)
    assert len(calls)==8
    assert result['summary']['memory']['episodes']==4
    assert result['summary']['memory']['failed_episodes']==1
    config['steps']=16
    with pytest.raises(ValueError,match='changed'):
        run_tournament(config,tmp_path/'run',resume=True)


def test_resume_pending_index(tmp_path,monkeypatch):
    import ashfall_agents.tournament as module
    from test_agents import FakeEnv
    class Environment(FakeEnv):
        def __init__(self,*args): super().__init__()
    monkeypatch.setattr(module,'AshfallEnv',Environment)
    calls=[]
    def episode(*args,**kwargs):
        calls.append(1)
        if len(calls)==1:
            raise KeyboardInterrupt
        return dict(status='completed',final_observations=[dict(population=5)],fallbacks=0,validation_failures=0,decision_seconds=0)
    monkeypatch.setattr(module,'run_episode',episode)
    config=suite(tmp_path)
    first=run_tournament(config,tmp_path/'run')
    assert first['episodes'][0]['status']=='cancelled'
    assert sum(row['status']=='pending' for row in first['episodes'])==7
    final=run_tournament(config,tmp_path/'run',resume=True)
    assert all(row['status']=='completed' for row in final['episodes'])


def test_paired_stats_use_seeds_not_positions_and_report_missing():
    rows=[]
    for seed in [1,2]:
        for position in [0,1]:
            for variant,population in [('baseline',10),('agent',13)]:
                rows.append(dict(seed=seed,position=position,controller='llm',variant=variant,status='completed',
                    result=dict(final_observations=[dict(population=population)]*2,fallbacks=0,validation_failures=0,decision_seconds=0)))
    pair=paired_comparisons(rows,'baseline')['agent-minus-baseline']
    assert pair['completed_pairs']==4
    assert pair['metrics']['population']['mean']==3
    assert pair['metrics']['population']['independent_seeds']==2
    rows[0]['status']='failed'
    pair=paired_comparisons(rows,'baseline')['agent-minus-baseline']
    assert pair['excluded_incomplete_pairs']==1
    assert set(summarize(rows))=={'baseline','agent'}


@pytest.mark.parametrize('patch',[{'decision_interval':0},{'repeats':0},{'seeds':[1,1]},{'positions':[2]}])
def test_bad_suite_rejected(tmp_path,patch):
    spec=suite(tmp_path); spec.update(patch)
    with pytest.raises(ValueError): build_trials(spec)


def test_rotated_positions_preserve_independent_seed_count(tmp_path):
    spec=suite(tmp_path);spec['positions_by_seed']={'1':[0],'2':[1]}
    trials=build_trials(spec)
    assert {(row['seed'],row['position']) for row in trials}=={(1,0),(2,1)}


def test_cancelled_checkpoint_is_not_fixed_horizon_survival():
    row=dict(seed=1,position=0,controller='llm',status='cancelled',result=dict(
        final_observations=[dict(population=100,alive=True)],original_generation_survival=[True],
        current_slot_alive=[True],fallbacks=0,validation_failures=0,decision_seconds=1))
    summary=summarize([row])['llm']
    assert summary['metrics']['survival']['mean'] is None
    assert summary['metrics']['population']['missing_episodes']==1
    assert summary['metrics']['completion']['mean']==0
    assert summary['failed_episodes']==1
