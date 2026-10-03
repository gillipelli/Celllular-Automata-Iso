"""Explicitly prompted live tool-capability checks, separate from strategy trials."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from ashfall_env import AshfallEnv
from ashfall_agents.memory import StrategyMemory
from ashfall_agents.runtime import AgentController
from ashfall_agents.schemas import KingdomAction
from ashfall_agents.zai_provider import ZaiClient


class RequiredCapability:
    def __init__(self, client, instruction, trace):
        self.client=client
        self.instruction=instruction
        self.first=True
        self.trace=trace

    def complete(self,messages,definitions):
        if self.first:
            payload=json.loads(messages[0]['content'])
            payload['capability_test']=self.instruction
            messages[0]['content']=json.dumps(payload)
            self.first=False
        self.trace({'kind':'capability_request','messages':messages,'tools':definitions})
        return self.client.complete(messages,definitions)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--capability',choices=['memory','planning'],required=True)
    parser.add_argument('--run-dir',required=True)
    args=parser.parse_args()
    directory=Path(args.run_dir)
    directory.mkdir(parents=True,exist_ok=False)
    config=Path('config/presets/agent-demo.toml').read_text()
    instruction=(
        'This is a tool integration check, not a strategy benchmark. Before submitting, call recall_outcomes with an empty query and limit 1. Then refer to an actual observed before/after change in your intent.'
        if args.capability=='memory' else
        'This is a tool integration check, not a strategy benchmark. Before submitting, call simulate_candidates with exactly three distinct legal actions and horizon 8. Choose an action using the returned outcomes and mention the comparison in your intent.')
    manifest={'label':'Explicitly prompted live capability probe; not evidence of spontaneous tool use or strategic benefit.',
        'capability':args.capability,'instruction':instruction,'seed':100,'model':'glm-5.3',
        'reasoning_effort':'low','max_output_tokens':4096,
        'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'source_sha256':hashlib.sha256(b''.join(p.read_bytes() for p in sorted(Path('python/ashfall_agents').glob('*.py')))).hexdigest()}
    (directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    client=ZaiClient(model='glm-5.3',spend_cap=1,input_per_million=1.4,output_per_million=4.4,max_tokens=4096,reasoning_effort='low')
    client.bind_ledger(directory/'budget.json')
    events=[]
    with (directory/'events.jsonl').open('w') as stream:
        def trace(event):
            events.append(event);stream.write(json.dumps(event)+'\n');stream.flush()
        with AshfallEnv(config,100) as env:
            before=env.observe_summary(0)
            action=KingdomAction.balanced()
            actions=np.tile(action.vector(env.num_agents),(env.num_agents,1))
            mask=np.zeros(env.num_agents,dtype=bool);mask[0]=True
            env.step_mixed(actions,mask)
            after=env.observe_summary(0)
            memory=StrategyMemory()
            memory.record(str(directory.resolve()),0,before,after,action.vector(env.num_agents,0).tolist(),'Maintain balanced production for one observed macro step.')
            track='partial' if args.capability=='memory' else 'privileged'
            controller=AgentController(RequiredCapability(client,instruction,trace),memory,str(directory.resolve()),track,
                decision_interval=16,max_steps=256,planning_budget={'candidates':3,'horizon':8} if track=='privileged' else None)
            selected,intent,metrics=controller.decide(env,0,after,trace)
            actions[0]=selected.vector(env.num_agents,0)
            env.step_mixed(actions,mask)
            env.save(directory/'session.json')
            digest=env.digest()
    name='recall_outcomes' if args.capability=='memory' else 'simulate_candidates'
    uses=[event for event in events if event['kind']=='tool' and event['name']==name and event['status']=='ok']
    if args.capability=='memory':
        exercised=any(event['result'] for event in uses)
        private_leak=any('episode' in row for event in uses for row in event['result'])
    else:
        exercised=any(len(event['result']['results'])==3 and event['result']['effective_horizon']==8 for event in uses)
        private_leak=False
    with AshfallEnv.load(directory/'session.json') as replay:
        replay_ok=replay.digest()==digest
    result={'capability':args.capability,'passed':bool(exercised and not private_leak and not metrics['fallback'] and replay_ok),
        'exercised':bool(exercised),'private_metadata_leak':private_leak,'replay_matches':replay_ok,
        'digest':digest,'intent':intent,'metrics':metrics,'usage':client.usage,
        'limitation':manifest['label']}
    (directory/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    if not result['passed']:raise SystemExit(1)

if __name__=='__main__':main()
