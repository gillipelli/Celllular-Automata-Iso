"""NumPy multi-agent environment backed by the deterministic C++ library.

Set ASHFALL_LIBRARY or pass library= to choose the built shared library.
Observations: [agents, 92224], comprising [2,20,48,48] spatial + 64 scalars.
Actions: [agents,27]: 8 allocation weights, 13 build weights, four integer
categories, quarantine, trade. Weights are normalized by the bridge.
"""
import ctypes as C
from concurrent.futures import ThreadPoolExecutor
import os
import json
from pathlib import Path
import numpy as np


class AshfallEnv:
    def __init__(self, config='', seed=1, threads=1, library=None):
        root = Path(__file__).resolve().parents[1]
        candidates = [root/'build/libashfall_bridge.so', root/'build-online/libashfall_bridge.so',
                      root/'build/libashfall_bridge.dylib', root/'build/Release/ashfall_bridge.dll']
        path = library or os.environ.get('ASHFALL_LIBRARY') or next((p for p in candidates if p.exists()), None)
        if path is None:
            raise FileNotFoundError('Build ashfall_bridge or set ASHFALL_LIBRARY')
        self.lib = C.CDLL(str(path))
        self.lib.ashfall_create.argtypes = [C.c_char_p, C.c_uint64, C.c_int]
        self.lib.ashfall_create.restype = C.c_void_p
        for name in ('ashfall_agents', 'ashfall_digest', 'ashfall_destroy'):
            getattr(self.lib, name).argtypes = [C.c_void_p]
        self.lib.ashfall_digest.restype = C.c_uint64
        self.lib.ashfall_destroy.restype = None
        self.lib.ashfall_error.restype = C.c_char_p
        self.lib.ashfall_reset.argtypes = [C.c_void_p, C.c_uint64]
        self.lib.ashfall_observe.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
        self.lib.ashfall_step.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p]
        self.handle = self.lib.ashfall_create(config.encode(), seed, threads)
        if not self.handle:
            raise ValueError(self.lib.ashfall_error().decode())
        self.num_agents = self.lib.ashfall_agents(self.handle)
        self.observation_size = self.lib.ashfall_observation_size()
        self.action_size = self.lib.ashfall_action_size()
        self._obs = np.empty((self.num_agents, self.observation_size), dtype=np.float32)
        self.closed = False
        self.config=config
        self.seed=seed
        self.history=[]

    def _check(self, result):
        if not result:
            raise ValueError(self.lib.ashfall_error().decode())

    def observe(self):
        if self.closed:
            raise RuntimeError('environment is closed')
        self._check(self.lib.ashfall_observe(self.handle, self._obs.ctypes.data, self._obs.size))
        return self._obs.copy()

    def reset(self, seed=1):
        if self.closed:
            raise RuntimeError('environment is closed')
        self._check(self.lib.ashfall_reset(self.handle, seed))
        self.seed=seed
        self.history=[]
        return self.observe(), {'digest': self.digest()}

    def step(self, actions):
        if self.closed:
            raise RuntimeError('environment is closed')
        actions = np.ascontiguousarray(actions, dtype=np.float32)
        if actions.shape != (self.num_agents, self.action_size):
            raise ValueError(f'expected action shape {(self.num_agents, self.action_size)}')
        rewards = np.empty(self.num_agents, dtype=np.float64)
        done = np.empty(self.num_agents+1, dtype=np.uint8)
        self._check(self.lib.ashfall_step(self.handle, actions.ctypes.data, actions.size,
                                        rewards.ctypes.data, done.ctypes.data))
        self.history.append(actions.tolist())
        return self.observe(), rewards, done[:-1].astype(bool), bool(done[-1]), {'digest': self.digest()}

    def save(self, path):
        payload={'format':'ASHFALL-ACTIONS-2','config':self.config,'seed':self.seed,
                 'actions':self.history,'digest':self.digest()}
        Path(path).write_text(json.dumps(payload)+'\n')

    @classmethod
    def load(cls, path, **kwargs):
        payload=json.loads(Path(path).read_text())
        if payload.get('format')!='ASHFALL-ACTIONS-2':
            raise ValueError('unsupported saved session')
        env=cls(payload['config'],payload['seed'],**kwargs)
        try:
            for actions in payload['actions']:
                env.step(actions)
            if env.digest()!=payload['digest']:
                raise ValueError('saved-session digest mismatch')
            return env
        except Exception:
            env.close()
            raise

    def digest(self):
        if self.closed:
            raise RuntimeError('environment is closed')
        return self.lib.ashfall_digest(self.handle)

    def close(self):
        if not getattr(self, 'closed', True):
            self.lib.ashfall_destroy(self.handle)
            self.closed = True

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        self.close()


class VecWorld:
    """Independent worlds execute concurrently; ctypes releases the GIL."""
    def __init__(self, config='', num_envs=4, seed=1, **kwargs):
        if num_envs < 1:
            raise ValueError('num_envs must be positive')
        self.envs = [AshfallEnv(config, seed+i, **kwargs) for i in range(num_envs)]
        self.pool = ThreadPoolExecutor(max_workers=num_envs)

    def reset(self, seed=1):
        return np.stack([env.reset(seed+i)[0] for i, env in enumerate(self.envs)])

    def step(self, actions):
        if len(actions) != len(self.envs):
            raise ValueError('one action batch required per environment')
        results = list(self.pool.map(lambda pair: pair[0].step(pair[1]), zip(self.envs, actions)))
        return tuple(np.stack(values) if index<4 else list(values)
                     for index, values in enumerate(zip(*results)))

    def close(self):
        self.pool.shutdown(wait=True)
        for env in self.envs:
            env.close()
