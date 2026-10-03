import json,time
from pathlib import Path
import numpy as np
from ashfall_env import AshfallEnv
from ashfall_agents.schemas import KingdomAction
config=Path('config/presets/agent-demo.toml').read_text()
rows=[]
for seed in [100,101]:
 for position in [0,1]:
  for controller in ['native','scripted']:
   start=time.monotonic()
   with AshfallEnv(config,seed) as env:
    initial=[env.observe_summary(k)['generation'] for k in range(4)]
    actions=np.tile(KingdomAction.balanced().vector(4),(4,1))
    mask=np.zeros(4,dtype=bool);mask[position]=controller=='scripted'
    for step in range(1,501):
     env.step_mixed(actions,mask)
     if step in [64,128,256,500]:
      own=env.observe_summary(position)
      rows.append(dict(seed=seed,position=position,controller=controller,steps=step,
        tick=own['tick'],population=own['population'],grain=own.get('stock',{}).get('grain'),
        original_survival=bool(own['alive'] and own['generation']==initial[position]),
        functional_nodes=own.get('functional_nodes'),seconds=time.monotonic()-start))
   print(json.dumps(rows[-4:]),flush=True)
out=Path('runs/agents/systematic-calibration');out.mkdir(parents=True,exist_ok=True)
(out/'horizons.json').write_text(json.dumps(rows,indent=2))
