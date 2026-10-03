"""Read-only trace audit of action validity, actual memory use and planning cost."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import numpy as np


def audit(root):
    rows=[]
    for index in sorted(Path(root).glob('*/tournament.json')):
        for record in json.loads(index.read_text())['episodes']:
            if record['status'] not in ('completed','failed','cancelled'):
                continue
            directory=index.parent/record['id']
            path=directory/'events.jsonl'
            events=[json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []
            tools=[event for event in events if event['kind']=='tool']
            proposals=[event for event in tools if event['name'] in ('validate_action','submit_action')]
            invalid=[event for event in proposals if event['status']!='ok']
            recalls=[event for event in tools if event['name']=='recall_outcomes']
            planning=[event['result'] for event in events if event['kind']=='search_baseline']
            planning.extend(event['result'] for event in tools if event['name']=='simulate_candidates' and event['status']=='ok')
            result=record.get('result',{})
            usage=result.get('usage',[])
            tokens=sum(u.get('input_tokens',0)+u.get('output_tokens',0) for u in usage)
            rows.append(dict(id=record['id'],variant=record['variant'],status=record['status'],
                decisions=result.get('decisions',0),fallbacks=result.get('fallbacks',0),
                validation_failures=result.get('validation_failures',0),action_proposals=len(proposals),
                invalid_action_proposals=len(invalid),recall_calls=len(recalls),
                nonempty_successful_recalls=sum(event['status']=='ok' and bool(event['result']) for event in recalls),
                successful_model_calls=len(usage),successful_response_tokens=tokens,
                provider_errors=sum(event['kind']=='provider_error' for event in events),
                planning_calls=len(planning),planning_seconds=sum(p.get('elapsed_seconds',0) for p in planning),
                planning_completed_candidates=sum(len(p.get('results',[])) for p in planning),
                planning_timeouts=sum(p.get('timed_out',False) for p in planning),
                planning_horizons=dict(Counter(p.get('effective_horizon') for p in planning))))
    grouped=defaultdict(list)
    for row in rows:grouped[row['variant']].append(row)
    totals={}
    for variant,group in grouped.items():
        sums={key:sum(row[key] for row in group) for key in (
            'decisions','fallbacks','validation_failures','action_proposals','invalid_action_proposals',
            'recall_calls','nonempty_successful_recalls','successful_model_calls','successful_response_tokens',
            'provider_errors','planning_calls','planning_seconds','planning_completed_candidates','planning_timeouts')}
        sums['invalid_action_proposal_rate']=sums['invalid_action_proposals']/sums['action_proposals'] if sums['action_proposals'] else None
        sums['fallback_rate']=sums['fallbacks']/sums['decisions'] if sums['decisions'] else None
        costs=[row['successful_response_tokens']/row['decisions'] for row in group if row['decisions']]
        sums['episode_mean_tokens_per_decision_p95']=float(np.quantile(costs,.95)) if costs else None
        sums['episodes']=len(group)
        totals[variant]=sums
    return dict(episodes=rows,variants=totals,
        token_caveat='Successful response tokens only. Shared campaign ledger is authoritative for failed or uncertain requests.',
        memory_caveat='Enabled memory without nonempty recall does not establish memory use or a memory benefit.')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('stage_root');args=parser.parse_args()
    result=audit(args.stage_root)
    (Path(args.stage_root)/'trace-audit.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result['variants'],indent=2))
