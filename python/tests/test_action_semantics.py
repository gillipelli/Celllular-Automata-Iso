import json
import re
from pathlib import Path
import pytest
from ashfall_agents.runtime import AgentController
from ashfall_agents.memory import StrategyMemory
from ashfall_agents.provider import ModelTurn
from ashfall_agents.schemas import KingdomAction
from ashfall_env import AshfallEnv
from test_agents import FakeEnv


def test_public_cost_catalog_matches_simulator_constants():
    root=Path(__file__).resolve().parents[2]
    source=(root/'src/sim/kingdom.cpp').read_text()
    with AshfallEnv((root/'config/presets/agent-demo.toml').read_text(),100) as env:
        catalog=env.describe_environment()['action_catalog']
        for source_name,field in [('WOOD_COST','wood_costs'),('STONE_COST','stone_costs')]:
            literal=re.search(source_name+r' = \{([^}]+)\}',source).group(1)
            assert catalog[field]==[int(value) for value in literal.split(',')]
        assert catalog['functional_dependencies']['farm']=='granary'
        assert catalog['functional_dependencies']['granary']=='one of capital/depot'
        assert 'NOT amounts' in catalog['weight_semantics']
        assert 'no-new-building' in catalog['construction_semantics']
        # The public catalog contains rules, no evaluator state or seed-specific inventory.
        with AshfallEnv((root/'config/presets/agent-demo.toml').read_text(),101) as other:
            assert catalog==other.describe_environment()['action_catalog']


def test_supplied_observation_allows_direct_valid_submission():
    class Direct:
        calls=0
        def complete(self,messages,definitions):
            self.calls+=1
            payload=json.loads(messages[0]['content'])
            assert payload['decision_duration_ticks']==16
            assert payload['evaluation_horizon_ticks']==256
            assert payload['observation']['kingdom_id']==0
            assert 'original kingdom generation' in payload['task']
            return ModelTurn([dict(type='tool_use',id='submit',name='submit_action',input={
                'action':KingdomAction.balanced().model_dump(),'intent':'Maintain production and repair.'})])
    client=Direct();env=FakeEnv()
    agent=AgentController(client,StrategyMemory(),'e',decision_interval=16,max_steps=256)
    _,_,metrics=agent.decide(env,0,env.observe_summary(0),lambda event:None)
    assert client.calls==1 and metrics['fallback'] is False


def test_cooperative_stop_prevents_next_provider_call(tmp_path,monkeypatch):
    stop=tmp_path/'STOP';stop.touch();monkeypatch.setenv('ZAI_CAMPAIGN_STOP_FILE',str(stop))
    class Never:
        def complete(self,*args):raise AssertionError('provider must not be called')
    env=FakeEnv();events=[]
    with pytest.raises(KeyboardInterrupt,match='campaign stop'):
        AgentController(Never(),StrategyMemory(),'e').decide(env,0,env.observe_summary(0),events.append)
    assert events[-1]['kind']=='campaign_stop' and env.tick==0


def test_privileged_study_advertises_equal_branch_budget():
    from ashfall_agents.runtime import tool_definitions
    definition=tool_definitions('privileged',{'candidates':3,'horizon':8})[-1]['input_schema']
    assert definition['properties']['candidates']['minItems']==3
    assert definition['properties']['candidates']['maxItems']==3
    assert definition['properties']['horizon']['enum']==[8]
    assert set(definition['required'])=={'candidates','horizon'}


def test_memory_redacts_private_scope_before_query_and_return():
    private='/runs/seed-1000-private-evaluator-marker'
    memory=StrategyMemory([dict(episode=private,kingdom=0,generation=1,tick=4,
        intent='Preserve grain',seed=1000,digest='private-digest-marker',run_id='private-run-marker')])
    public=memory.recall(private,0,1)
    assert public==[dict(kingdom=0,generation=1,tick=4,intent='Preserve grain')]
    assert memory.records[0]['episode']==private
    assert memory.recall(private,0,1,query='private-evaluator-marker')==[]
    assert memory.recall(private,0,1,query='private-digest-marker')==[]
    assert memory.recall(private,0,1,query='Preserve grain')==public
    assert memory.recall(private,1,1)==[]
    assert memory.recall(private,0,2)==[]


def test_runtime_nonempty_recall_exposes_only_public_record():
    private='/runs/1000-private-evaluator-marker'
    env=FakeEnv()
    memory=StrategyMemory()
    own=env.observe_summary(0)
    memory.record(private,0,own,own,KingdomAction.balanced().vector(2).tolist(),'Preserve grain')
    class RecallThenSubmit:
        calls=0
        def complete(self,messages,definitions):
            self.calls+=1
            if self.calls==1:
                return ModelTurn([dict(type='tool_use',id='recall',name='recall_outcomes',input={'query':'','limit':1})])
            result=json.loads(messages[-1]['content'][0]['content'])
            assert result and result[0]['intent']=='Preserve grain'
            assert private not in json.dumps(result) and 'episode' not in result[0]
            return ModelTurn([dict(type='tool_use',id='submit',name='submit_action',input={
                'action':KingdomAction.balanced().model_dump(),'intent':'Preserve grain'})])
    client=RecallThenSubmit()
    _,_,metrics=AgentController(client,memory,private).decide(env,0,own,lambda event:None)
    assert client.calls==2 and not metrics['fallback']
