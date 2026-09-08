import argparse
from pathlib import Path
import sys
import tempfile
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'python'))
from ashfall_env import AshfallEnv, VecWorld
parser=argparse.ArgumentParser();parser.add_argument('--library',required=True);args=parser.parse_args()
config='[world]\nsize=64\nforest_warmup=10\n'
with AshfallEnv(config, seed=9, library=args.library) as a, AshfallEnv(config, seed=9, threads=4, library=args.library) as b:
    obs=a.observe();assert obs.shape==(4,92224);assert np.isfinite(obs).all()
    actions=np.ones((4,27),np.float32);actions[:,21]=8;actions[:,22:24]=-1;actions[:,24]=5;actions[:,25]=0
    for _ in range(3):
        x=a.step(actions);y=b.step(actions);assert x[4]['digest']==y[4]['digest'];np.testing.assert_array_equal(x[0],y[0])
    first=a.reset(9)[1]['digest'];assert first==b.reset(9)[1]['digest']
    with tempfile.TemporaryDirectory() as directory:
        path=Path(directory)/'session.json'
        a.step(actions)
        a.save(path)
        with AshfallEnv.load(path,library=args.library) as loaded:
            assert loaded.digest()==a.digest()
            np.testing.assert_array_equal(loaded.observe(),a.observe())
    saved=a.digest();actions[0,0]=np.nan
    try:a.step(actions)
    except ValueError:pass
    else:raise AssertionError('NaN accepted')
    assert a.digest()==saved
print('bridge reset, action validation, observations and thread determinism passed')
