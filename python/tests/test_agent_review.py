"""Independent review regressions for malformed providers and shared budgets."""
import json
from pathlib import Path
import numpy as np
import pytest
from ashfall_agents.memory import StrategyMemory
from ashfall_agents.provider import ModelTurn, FixtureClient
from ashfall_agents.runtime import AgentController
from ashfall_agents.runner import run_episode


class Environment:
    num_agents=2
    config='review-fixture'
    seed=42
    def __init__(self):self.tick=0
    def describe_environment(self):return {'tick':self.tick,'episode_ticks':100,'action_catalog':{}}
    def observe_summary(self,k):return {'tick':self.tick,'generation':1,'kingdom_id':k,'population':100,'reputation':[0,0],'treaties':[]}
    def digest(self):return self.tick+123
    def save(self,path):Path(path).write_text(json.dumps({'tick':self.tick}))
    def step_mixed(self,actions,mask):
        self.tick+=1
        return None,np.ones(2),np.zeros(2,dtype=bool),False,{'digest':self.digest()}


@pytest.mark.parametrize('content',[None,42,{'type':'text'}])
def test_nonlist_provider_content_falls_back(content):
    class Client:
        def complete(self,messages,tools):return ModelTurn(content)
    env=Environment()
    _,_,stats=AgentController(Client(),StrategyMemory(),'e').decide(env,0,env.observe_summary(0),lambda _:None)
    assert stats['fallback'] and env.tick==0


def test_duplicate_tool_ids_not_sent_back_to_provider():
    class Client:
        calls=0
        def complete(self,messages,tools):
            self.calls+=1
            if self.calls>1:
                prior=messages[-2]['content']
                ids=[block['id'] for block in prior if isinstance(block,dict) and block.get('type')=='tool_use']
                assert len(ids)==len(set(ids)), 'duplicate tool IDs sent to provider'
                return ModelTurn([])
            return ModelTurn([{'type':'tool_use','id':'duplicate','name':'inspect_kingdom','input':{}},
                              {'type':'tool_use','id':'duplicate','name':'get_diplomacy','input':{}}])
    client=Client();env=Environment();events=[]
    _,_,stats=AgentController(client,StrategyMemory(),'e').decide(env,0,env.observe_summary(0),events.append)
    assert not any(e.get('kind')=='provider_error' and 'duplicate tool IDs' in e.get('error','') for e in events)
    assert stats['fallback']


def test_episode_usage_is_not_cumulative_tournament_usage(tmp_path):
    class MeteredClient:
        model='mock'
        spent=0.
        usage=[]
        def complete(self,messages,tools):
            self.spent+=.01
            self.usage.append({'estimated_cost':.01})
            return FixtureClient().complete(messages,tools)
    client=MeteredClient()
    first=run_episode(Environment(),tmp_path/'first',['llm','native'],client,steps=1)
    second=run_episode(Environment(),tmp_path/'second',['scripted','native'],client,steps=1)
    assert first['estimated_cost']==pytest.approx(.02)
    assert second['estimated_cost']==0
    assert second['usage']==[]


def test_resume_rejects_changed_simulator_binary(tmp_path):
    from types import SimpleNamespace
    library=tmp_path/'simulator.so';library.write_bytes(b'original transition code')
    env=Environment();env.lib=SimpleNamespace(_name=str(library))
    run_episode(env,tmp_path/'run',['scripted','native'],steps=1)
    library.write_bytes(b'changed transition code with same state digest')
    with pytest.raises(ValueError,match='source|simulator|binary|implementation'):
        run_episode(env,tmp_path/'run',['scripted','native'],steps=2,resume=True)


class SlowReconstructionEnv(Environment):
    @classmethod
    def load(cls,path,**kwargs):
        import time
        time.sleep(10)
        return cls()
    def __enter__(self):return self
    def __exit__(self,*args):pass


def test_planning_deadline_interrupts_reconstruction():
    import multiprocessing
    import time
    from ashfall_agents.planning import evaluate_candidates
    from ashfall_agents.schemas import KingdomAction
    env=SlowReconstructionEnv();digest=env.digest();started=time.monotonic()
    children_before={child.pid for child in multiprocessing.active_children()}
    result=evaluate_candidates(env,0,[KingdomAction.balanced().model_dump()],max_seconds=.4)
    assert time.monotonic()-started<2
    assert result['timed_out'] and result['incomplete'] and result['results']==[]
    assert env.digest()==digest
    assert {child.pid for child in multiprocessing.active_children()}==children_before


def test_spawn_planning_real_bridge_keeps_live_state():
    from ashfall_env import AshfallEnv
    from ashfall_agents.planning import evaluate_candidates
    from ashfall_agents.schemas import KingdomAction
    library=Path(__file__).resolve().parents[2]/'build/libashfall_bridge.so'
    if not library.exists():pytest.skip('native bridge is not built')
    config='[world]\nsize=64\nforest_warmup=0\n'
    with AshfallEnv(config,seed=71,library=library) as env:
        digest=env.digest()
        result=evaluate_candidates(env,0,[KingdomAction.balanced().model_dump()],horizon=2,max_seconds=10)
        assert not result['incomplete'] and not result['timed_out']
        assert result['results'][0]['steps']==2
        assert env.digest()==digest and env.history==[]
