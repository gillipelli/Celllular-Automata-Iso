import argparse
import json
from pathlib import Path
from ashfall_env import AshfallEnv
from .provider import AnthropicClient
from .tournament import run_tournament
from .runner import run_episode, render_report, write_json, load_checkpoint

def main():
    parser=argparse.ArgumentParser(description='Reproducible Ashfall strategy agents')
    sub=parser.add_subparsers(dest='command',required=True)
    play=sub.add_parser('play')
    play.add_argument('--config',default='config/presets/agent-demo.toml')
    play.add_argument('--seed',type=int,default=42)
    play.add_argument('--controllers',default='fixture,native,native,native')
    play.add_argument('--track',choices=['partial','privileged'],default='partial')
    play.add_argument('--steps',type=int,default=16)
    play.add_argument('--decision-interval',type=int,default=4)
    play.add_argument('--run-dir',required=True)
    play.add_argument('--no-memory',action='store_true')
    play.add_argument('--resume',action='store_true')
    play.add_argument('--provider',choices=['anthropic','zai'],default='zai')
    play.add_argument('--model')
    play.add_argument('--spend-cap',type=float,default=0)
    play.add_argument('--input-per-million',type=float,default=0)
    play.add_argument('--output-per-million',type=float,default=0)
    replay=sub.add_parser('replay'); replay.add_argument('--run',required=True)
    tournament=sub.add_parser('tournament')
    tournament.add_argument('--suite',required=True); tournament.add_argument('--run-dir',required=True)
    tournament.add_argument('--resume',action='store_true')
    tournament.add_argument('--provider',choices=['anthropic','zai'],default='zai')
    for flag in ('model','spend-cap','input-per-million','output-per-million'):
        tournament.add_argument('--'+flag,type=str if flag=='model' else float,default=None if flag=='model' else 0)
    for target in (play,tournament):
        target.add_argument('--reasoning-effort',choices=['low','high','max'],default='low')
        target.add_argument('--max-output-tokens',type=int,default=4096)
    args=parser.parse_args()
    def make_client():
        cls=AnthropicClient
        if args.provider=='zai':
            from .zai_provider import ZaiClient
            cls=ZaiClient
        options={'max_tokens':args.max_output_tokens}
        if args.provider=='zai':
            options['reasoning_effort']=args.reasoning_effort
        model=args.model or ('glm-5.3' if args.provider=='zai' else None)
        return cls(model,args.spend_cap,args.input_per_million,args.output_per_million,**options)
    if args.command=='play':
        controllers=args.controllers.split(',')
        client=make_client() if 'llm' in controllers else None
        env=AshfallEnv.load(load_checkpoint(args.run_dir)[1]) if args.resume else AshfallEnv(Path(args.config).read_text(),args.seed)
        with env:
            result=run_episode(env,args.run_dir,controllers,client,args.track,args.steps,args.decision_interval,not args.no_memory,args.resume)
        print(json.dumps(result,indent=2))
    elif args.command=='replay':
        with AshfallEnv.load(Path(args.run)/'session.json') as env:
            expected=json.loads((Path(args.run)/'result.json').read_text())['digest']
            if env.digest()!=expected:
                raise ValueError('result digest mismatch')
            print(json.dumps({'verified_digest':env.digest(),'report':str(render_report(args.run))}))
    else:
        suite=json.loads(Path(args.suite).read_text())
        result=run_tournament(suite,args.run_dir,make_client,resume=args.resume)
        print(json.dumps({'episodes':len(result['episodes']),'output':str(Path(args.run_dir)/'tournament.json')}))

if __name__=='__main__': main()
