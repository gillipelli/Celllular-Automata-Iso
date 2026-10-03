import json
from pathlib import Path
import numpy as np
import pytest
from ashfall_agents.schemas import KingdomAction
from ashfall_agents.memory import StrategyMemory
from ashfall_agents.provider import FixtureClient, ModelTurn, AnthropicClient
from ashfall_agents.runtime import AgentController, tool_definitions
from ashfall_agents.runner import run_episode
from ashfall_agents.planning import evaluate_candidates

class FakeEnv:
    num_agents=2
    config='fixture'
    seed=42
    def __init__(self): self.tick=0; self.actions=[]
    def describe_environment(self):
        return dict(tick=self.tick,episode_ticks=100,action_catalog={})
    def observe_summary(self,k):
        return dict(tick=self.tick,generation=1,kingdom_id=k,population=100+self.tick,reputation=[0,0],treaties=[])
    def digest(self): return self.tick+123
    def step_mixed(self,actions,mask):
        self.actions.append((actions.copy(),mask.copy())); self.tick+=1
        return None,np.ones(2),np.zeros(2,dtype=bool),False,{'digest':self.digest()}
    def save(self,path): Path(path).write_text(json.dumps({'tick':self.tick}))
    @classmethod
    def load(cls,path):
        obj=cls(); obj.tick=json.loads(Path(path).read_text())['tick']; return obj
    def __enter__(self): return self
    def __exit__(self,*args): pass

@pytest.mark.parametrize('update',[
    {'allocation':[0]*8},{'allocation':[float('nan')]*8},
    {'trade':2},{'expansion_direction':1.5},{'construction':[-1]+[1]*12},
    {'unexpected':3}])
def test_invalid_actions(update):
    with pytest.raises(ValueError): KingdomAction.model_validate({**KingdomAction.balanced().model_dump(),**update})

def test_target_and_normalization():
    action=KingdomAction.balanced(); action.allocation=[1e300]*8
    assert np.isfinite(action.vector(2)).all()
    action.treaty_partner=0
    with pytest.raises(ValueError): action.vector(2,0)

def test_generation_memory():
    memory=StrategyMemory([dict(episode='e',kingdom=0,generation=1,intent='old'),
                           dict(episode='e',kingdom=0,generation=2,intent='new')])
    assert memory.recall('e',0,2)[0]['intent']=='new'
    assert memory.recall('other',0,2)==[]
    assert memory.recall('e',0,2,limit=0)==[]

def test_fixture_executes_tools_without_mutation():
    env=FakeEnv(); events=[]
    agent=AgentController(FixtureClient(),StrategyMemory(),'e')
    action,intent,stats=agent.decide(env,0,env.observe_summary(0),events.append)
    assert stats['tool_calls']==2 and not stats['fallback']
    assert env.tick==0
    assert [e['name'] for e in events if e['kind']=='tool']==['inspect_kingdom','submit_action']

class BadClient:
    def complete(self,messages,tools):
        return ModelTurn([dict(type='tool_use',id='bad',name='simulate_candidates',input={})])

def test_partial_cannot_plan():
    assert 'simulate_candidates' not in [t['name'] for t in tool_definitions('partial')]
    env=FakeEnv(); events=[]
    _,_,stats=AgentController(BadClient(),StrategyMemory(),'e').decide(env,0,env.observe_summary(0),events.append)
    assert stats['fallback'] and stats['failures']>=1 and env.tick==0

def test_runner_checkpoint_resume(tmp_path):
    env=FakeEnv()
    first=run_episode(env,tmp_path,['fixture','native'],steps=2,decision_interval=1)
    assert first['steps']==2 and first['decisions']==2
    assert (tmp_path/'report.html').exists()
    resumed=FakeEnv.load(tmp_path/'session.json')
    final=run_episode(resumed,tmp_path,['fixture','native'],steps=4,decision_interval=1,resume=True)
    assert final['steps']==4 and final['decisions']==4
    assert all(mask.tolist()==[True,False] for _,mask in resumed.actions)

def test_planning_does_not_change_live_state():
    env=FakeEnv(); before=env.digest()
    result=evaluate_candidates(env,0,[KingdomAction.balanced().model_dump()],horizon=2)
    assert env.digest()==before and result['results'][0]['steps']==2
    assert result['track']=='privileged'

def test_live_requires_explicit_budget():
    with pytest.raises(ValueError): AnthropicClient('test',0,1,1)

def test_no_live_client_mislabeled(tmp_path):
    with pytest.raises(ValueError): run_episode(FakeEnv(),tmp_path,['llm','native'])

def test_spatial_tool_bounds_and_observed_values():
    from ashfall_agents.observations import inspect_region
    env=FakeEnv()
    env.observe=lambda: np.zeros((2,92224),dtype=np.float32)
    assert inspect_region(env,0)['channels']['fire']['mean']==0
    with pytest.raises(ValueError): inspect_region(env,0,x=-1)
    with pytest.raises(ValueError): inspect_region(env,0,width=17)

class MalformedClient:
    def complete(self,messages,tools):
        return ModelTurn([None,dict(type='tool_use',name='submit_action',input={})])

def test_malformed_provider_block_falls_back():
    env=FakeEnv()
    _,_,stats=AgentController(MalformedClient(),StrategyMemory(),'e').decide(env,0,env.observe_summary(0),lambda e:None)
    assert stats['fallback'] and env.tick==0

def test_model_facing_trace_excludes_seed_digest_joint_actions():
    events=[]; env=FakeEnv()
    AgentController(FixtureClient(),StrategyMemory(),'e').decide(env,0,env.observe_summary(0),events.append)
    request=next(e for e in events if e['kind']=='model_request')
    text=json.dumps(request['messages'])
    assert 'seed' not in text and 'digest' not in text and 'joint' not in text

def test_initial_checkpoint_survives_failed_first_step(tmp_path):
    from ashfall_agents.runner import load_checkpoint
    env=FakeEnv()
    def fail(*args): raise RuntimeError('failed step')
    env.step_mixed=fail
    with pytest.raises(RuntimeError): run_episode(env,tmp_path,['fixture','native'],steps=1)
    state,path=load_checkpoint(tmp_path)
    assert state['step']==0 and FakeEnv.load(path).digest()==env.digest()
    assert json.loads((tmp_path/'result.json').read_text())['status']=='failed'

def test_live_budget_finite():
    for value in (float('nan'),float('inf')):
        with pytest.raises(ValueError): AnthropicClient('test',value,1,1)

def test_budget_reservation_persists_before_failure(tmp_path):
    # Construct adapter without SDK or API key; simulated request failure is not billable.
    from types import SimpleNamespace
    client=object.__new__(AnthropicClient)
    client.model='fake'; client.cap=1.;client.input_rate=1.;client.output_rate=1.
    client.max_tokens=100;client.spent=0.;client.calls=0;client.usage=[];client.ledger=None
    def fail(**kwargs): raise RuntimeError('ambiguous network failure')
    client.client=SimpleNamespace(messages=SimpleNamespace(create=fail))
    client.bind_ledger(tmp_path/'budget.json')
    with pytest.raises(RuntimeError): client.complete([],[])
    saved=json.loads((tmp_path/'budget.json').read_text())
    assert saved['spent']>0
    client.spent=0;client.bind_ledger(tmp_path/'budget.json')
    assert client.spent==saved['spent']

def test_aggregate_bootstraps_independent_seeds():
    from ashfall_agents.evaluation import summarize
    rows=[dict(seed=seed,position=0,controller='fixture',result=dict(
        final_observations=[dict(population=10,alive=True)],fallbacks=0,validation_failures=0,decision_seconds=0))
        for seed in [1,1,2]]
    result=summarize(rows)['fixture']
    assert result['episodes']==3 and result['metrics']['population']['independent_seeds']==2


def test_dead_kingdom_skips_provider_and_successor_gets_fresh_decision(tmp_path):
    class SuccessionEnv(FakeEnv):
        def observe_summary(self,k):
            result=super().observe_summary(k)
            result['alive']=not (k==0 and self.tick==1)
            result['generation']=2 if k==0 and self.tick>=2 else 1
            return result
    class CountingClient:
        model='counting-fixture'
        calls=0
        def complete(self,messages,tools):
            self.calls+=1
            return FixtureClient().complete(messages,tools)
    env=SuccessionEnv();client=CountingClient()
    result=run_episode(env,tmp_path,['llm','native'],client,steps=4,decision_interval=4)
    # Two tools for original generation, none while dead, two for successor.
    assert client.calls==4 and result['decisions']==2
    assert result['initial_generations']==[1,1]
    assert result['original_generation_survival']==[False,True]
    assert result['current_slot_alive']==[True,True]
    checkpoint=json.loads((tmp_path/'checkpoints/000002/state.json').read_text())
    assert '0' not in checkpoint['held']
    assert all(mask.tolist()==[True,False] for _,mask in env.actions)


def test_interruption_after_world_mutation_reports_only_committed_checkpoint(tmp_path):
    class InterruptedEnv(FakeEnv):
        def step_mixed(self,actions,mask):
            super().step_mixed(actions,mask)
            raise KeyboardInterrupt()
    env=InterruptedEnv()
    result=run_episode(env,tmp_path,['fixture','native'],steps=2)
    assert result['status']=='cancelled'
    assert env.tick==1 and result['steps']==0
    assert result['digest']==FakeEnv.load(tmp_path/'session.json').digest()==123
    assert all(o['tick']==0 for o in result['final_observations'])
    assert result['decisions']==0 and result['attempted_decisions']==1


def test_evaluation_distinguishes_original_survival_from_successor():
    from ashfall_agents.evaluation import summarize
    result=dict(final_observations=[dict(population=10,alive=True,generation=2)],
        original_generation_survival=[False],current_slot_alive=[True],
        fallbacks=0,validation_failures=0,decision_seconds=0)
    metrics=summarize([dict(seed=1,position=0,controller='fixture',result=result)])['fixture']['metrics']
    assert metrics['original_generation_survival']['mean']==0
    assert metrics['survival']['mean']==0
    assert metrics['current_slot_alive']['mean']==1
