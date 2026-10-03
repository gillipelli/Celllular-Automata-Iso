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

# Mixed control is reversible per macro step and persists in replay history.
import json
with AshfallEnv(config, seed=19, library=args.library) as a, AshfallEnv(config, seed=19, library=args.library) as b:
    actions=np.ones((4,27),np.float32);actions[:,21]=8;actions[:,22:24]=-1;actions[:,24]=5;actions[:,25]=0
    meta=a.describe_environment()
    assert meta['tick']==0 and meta['schema_version']==1
    assert meta['action_catalog']['directions'][0]=='east'
    summary=a.observe_summary(0)
    assert summary['kingdom_id']==0 and summary['generation']==1
    assert set(summary['stock'])=={'wood','stone','grain','treasury'}
    for mask in ([1,0,1,0],[0,1,0,1],[0,0,0,0]):
        a.step_mixed(actions,mask);b.step_mixed(actions,mask)
        assert a.digest()==b.digest()
    assert a.describe_environment()['tick']==3*meta['macro_interval']
    digest=a.digest()
    for mask in ([1,0,2,0],[1,0], [1,0,float('nan'),0]):
        try:a.step_mixed(actions,mask)
        except ValueError:pass
        else:raise AssertionError('invalid mask accepted')
        assert a.digest()==digest
    invalid=actions.copy();invalid[3,25]=2
    try:a.step_mixed(invalid,[1,1,1,1])
    except ValueError:pass
    else:raise AssertionError('invalid last action accepted')
    assert a.digest()==digest
    with tempfile.TemporaryDirectory() as directory:
        path=Path(directory)/'mixed.json';a.save(path)
        with AshfallEnv.load(path,library=args.library) as restored:
            assert restored.digest()==digest
            assert restored.control_history==a.control_history
        payload=json.loads(path.read_text());payload['external_masks'].pop()
        path.write_text(json.dumps(payload))
        try:AshfallEnv.load(path,library=args.library)
        except ValueError:pass
        else:raise AssertionError('missing masks accepted')
    a.reset(19);a.step(actions)
    with tempfile.TemporaryDirectory() as directory:
        path=Path(directory)/'old.json';a.save(path)
        payload=json.loads(path.read_text());payload['format']='ASHFALL-ACTIONS-2';payload.pop('external_masks')
        path.write_text(json.dumps(payload))
        with AshfallEnv.load(path,library=args.library) as restored:
            assert restored.digest()==a.digest()
print('mixed control, summaries, atomic validation, v2/v3 replay passed')

vector=VecWorld(config,num_envs=2,seed=19,library=args.library)
try:
    result=vector.step(np.stack([actions,actions]))
    assert result[0].shape==(2,4,92224)
finally:
    vector.close()
print('vector environment compatibility passed')
