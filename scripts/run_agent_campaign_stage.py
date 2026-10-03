"""Run independent shards with separate USD ledgers and one global token ledger.

Suites are frozen before launch. This script never chooses or tunes configurations
from held-out outcomes. Replay and aggregate are local and make no model calls.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from ashfall_agents.evaluation import paired_comparisons, summarize
from ashfall_agents.runner import write_json


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--stage',choices=['development','holdout','privileged'],required=True)
    parser.add_argument('--run-root',required=True)
    parser.add_argument('--resume',action='store_true')
    parser.add_argument('--workers',type=int,choices=range(1,5),default=1)
    args=parser.parse_args()
    source=Path(__file__).resolve().parents[1]
    suites=sorted((source/'evaluations'/'systematic').glob(args.stage+'-shard-*.json'))
    if not suites:
        raise ValueError('no frozen suites')
    root=Path(args.run_root);root.mkdir(parents=True,exist_ok=True)
    manifest={'stage':args.stage,'suites':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in suites},
              'model':'glm-5.3','reasoning_effort':'low','max_output_tokens':4096,
              'global_ledger':os.environ.get('ZAI_CAMPAIGN_LEDGER'),
              'note':'Each shard has its own USD ledger; campaign token ceiling is shared.'}
    target=root/'stage_manifest.json'
    if target.exists():
        if not args.resume or json.loads(target.read_text())!=manifest:
            raise ValueError('stage already exists or changed')
    else:
        write_json(target,manifest)
    stop_file=root/'STOP'
    def run(suite):
        if stop_file.exists():
            return {'suite':suite.name,'returncode':130,'directory':str(root/suite.stem)}
        run_dir=root/suite.stem
        command=[sys.executable,'-m','ashfall_agents','tournament','--suite',str(suite),'--run-dir',str(run_dir),
                 '--provider','zai','--model','glm-5.3','--reasoning-effort','low','--max-output-tokens','4096',
                 '--spend-cap','30','--input-per-million','1.4','--output-per-million','4.4']
        if args.resume and (run_dir/'tournament_manifest.json').exists():command.append('--resume')
        with (root/(suite.stem+'.log')).open('a') as log:
            env=dict(os.environ, ZAI_CAMPAIGN_STOP_FILE=str(stop_file.resolve()))
            proc=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=False,env=env)
        return {'suite':suite.name,'returncode':proc.returncode,'directory':str(run_dir)}
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        completed=list(pool.map(run,suites))
    episodes=[]
    replay_failures=[]
    for item in completed:
        directory=Path(item['directory'])
        if not (directory/'tournament.json').exists():continue
        rows=json.loads((directory/'tournament.json').read_text())['episodes']
        for row in rows:
            if row['status']=='completed':
                with (root/'replay.log').open('a') as log:
                    result=subprocess.run([sys.executable,'-m','ashfall_agents','replay','--run',str(directory/row['id'])],stdout=log,stderr=subprocess.STDOUT)
                if result.returncode:replay_failures.append(row['id'])
        episodes.extend(rows)
    live=[row for row in episodes if row['controller']=='llm']
    decisions=sum(row.get('result',{}).get('decisions',0) for row in live)
    fallbacks=sum(row.get('result',{}).get('fallbacks',0) for row in live)
    gates={'all_shards_successful':all(row['returncode']==0 for row in completed),
           'all_episodes_completed':all(row['status']=='completed' for row in episodes),
           'all_replays_match':not replay_failures,
           'autonomous_valid_decision_rate':1-fallbacks/decisions if decisions else None,
           'decisions':decisions,'fallbacks':fallbacks,'replay_failures':replay_failures}
    write_json(root/'aggregate.json',{'manifest':manifest,'shards':completed,'gates':gates,'episodes':episodes,
        'summary':summarize(episodes),'paired_vs_scripted':paired_comparisons(episodes,'scripted'),
        'paired_memory_ablation':paired_comparisons(episodes,'llm-no-memory')})
    print(json.dumps(gates),flush=True)
    if not all(gates[key] for key in ('all_shards_successful','all_episodes_completed','all_replays_match')):
        raise SystemExit(1)

if __name__=='__main__':main()
