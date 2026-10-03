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
        self.lib.ashfall_step_mixed.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t,
                                               C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p]
        self.lib.ashfall_metadata.argtypes = [C.c_void_p, C.c_int]
        self.lib.ashfall_metadata.restype = C.c_char_p
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
        self.control_history=[]

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
        self.control_history=[]
        return self.observe(), {'digest': self.digest()}

    def step(self, actions):
        return self.step_mixed(actions, np.ones(self.num_agents, dtype=np.uint8))

    def step_mixed(self, actions, external_mask):
        if self.closed:
            raise RuntimeError('environment is closed')
        actions = np.ascontiguousarray(actions, dtype=np.float32)
        if actions.shape != (self.num_agents, self.action_size):
            raise ValueError(f'expected action shape {(self.num_agents, self.action_size)}')
        mask = np.asarray(external_mask)
        if mask.shape != (self.num_agents,) or not np.isin(mask, [0, 1]).all():
            raise ValueError('external_mask must contain one boolean per kingdom')
        mask = np.ascontiguousarray(mask, dtype=np.uint8)
        # Native-policy rows are ignored and canonicalized for portable strict JSON.
        actions = actions.copy()
        actions[mask == 0] = 0
        rewards = np.empty(self.num_agents, dtype=np.float64)
        done = np.empty(self.num_agents+1, dtype=np.uint8)
        self._check(self.lib.ashfall_step_mixed(self.handle, actions.ctypes.data, actions.size,
                                              mask.ctypes.data, mask.size,
                                        rewards.ctypes.data, done.ctypes.data))
        self.history.append(actions.tolist())
        self.control_history.append(mask.tolist())
        return self.observe(), rewards, done[:-1].astype(bool), bool(done[-1]), {'digest': self.digest()}

    def save(self, path):
        payload={'format':'ASHFALL-ACTIONS-3','config':self.config,'seed':self.seed,
                 'actions':self.history,'external_masks':self.control_history,'digest':self.digest()}
        Path(path).write_text(json.dumps(payload)+'\n')

    @classmethod
    def load(cls, path, **kwargs):
        payload=json.loads(Path(path).read_text())
        if payload.get('format') not in ('ASHFALL-ACTIONS-2', 'ASHFALL-ACTIONS-3'):
            raise ValueError('unsupported saved session')
        env=cls(payload['config'],payload['seed'],**kwargs)
        try:
            masks = payload.get('external_masks') if payload['format'] == 'ASHFALL-ACTIONS-3' else [[1] * env.num_agents] * len(payload['actions'])
            if not isinstance(masks, list) or len(masks) != len(payload['actions']):
                raise ValueError('saved-session control masks mismatch')
            for actions, mask in zip(payload['actions'], masks):
                env.step_mixed(actions, mask)
            if env.digest()!=payload['digest']:
                raise ValueError('saved-session digest mismatch')
            return env
        except Exception:
            env.close()
            raise

    def _metadata(self, kingdom):
        if self.closed:
            raise RuntimeError('environment is closed')
        value = self.lib.ashfall_metadata(self.handle, kingdom)
        if value is None:
            raise ValueError(self.lib.ashfall_error().decode())
        return json.loads(value)

    def describe_environment(self):
        result = self._metadata(-1)
        result['action_catalog'] = {
            'allocation': ['wood', 'stone', 'grain', 'construction', 'military', 'monitoring', 'openness', 'reserve'],
            'construction': ['capital', 'farm', 'granary', 'woodcamp', 'quarry', 'kiln', 'smithy', 'barracks', 'wall', 'road', 'depot', 'market', 'aqueduct'],
            'directions': ['east', 'southeast', 'south', 'southwest', 'west', 'northwest', 'north', 'northeast', 'none'],
            'treaty_actions': ['propose_nonaggression', 'propose_logging_quota', 'propose_border', 'accept', 'break', 'none'],
            'weight_semantics': 'Allocation and construction are independently normalized nonnegative relative weights, NOT amounts of treasury or resources. All-zero vectors are invalid.',
            'allocation_effects': {
                'wood': 'Harvest forest into wood stock.', 'stone': 'Mine finite stone deposits.',
                'grain': 'Farm with population labor, fertility and moisture; functional farms double local grain output. Farming depletes fertility.',
                'construction': 'Advance construction and repair; share above about 5% enables new building and wood-funded repair.',
                'military': 'Recruit population into military when a functional barracks exists; troops consume food.',
                'monitoring': 'Increase treaty-violation detection; incurs treasury cost.',
                'openness': 'Absorb refugees when local carrying capacity permits.',
                'reserve': 'Increase tax revenue; this is a tax-policy weight, not stored currency.'},
            'construction_semantics': 'At each macro step one structure type is sampled from construction weights. Building requires wood, stone, supply, population and an available site; it consumes stocks automatically. Capital (index 0) is a no-new-building choice: capital cannot be rebuilt by this action. Existing construction and repair can continue. Buildings require completion, integrity, supply and functional dependencies; many unsupported buildings can cause network collapse.',
            'wood_costs': [0,20,40,25,30,40,60,50,10,15,35,45,30],
            'stone_costs': [0,0,20,5,10,30,60,40,60,10,25,25,70],
            'functional_dependencies': {
                'capital': 'none', 'farm': 'granary', 'granary': 'one of capital/depot',
                'woodcamp': 'one of depot/capital', 'quarry': 'one of depot/capital',
                'kiln': 'two of woodcamp/quarry/depot', 'smithy': 'two of kiln/quarry/granary',
                'barracks': 'two of granary/smithy/depot', 'wall': 'one of depot/capital',
                'road': 'one of road/depot/capital', 'depot': 'one of road/granary/capital',
                'market': 'two of depot/granary/road', 'aqueduct': 'another aqueduct or adjacency to water'},
            'dependency_rules': 'Dependencies must be owned, functional, within Manhattan distance 24; a structure cannot depend on itself. Completion and spatial placement still matter.',
            'maintenance': 'Completed structures spend treasury for upkeep. Damage can be repaired automatically when wood exceeds 1 unit, construction share exceeds about 5%, and the site is not burning.',
            'survival': 'Sustained loss of the functional infrastructure giant component, a nonfunctional capital, sustained starvation, or very low population can collapse the original kingdom generation. Expansion is not the primary objective.',
            'quarantine_trade': 'Quarantine 0..1 reduces movement and transmission but costs treasury. Trade 0..1 adds market revenue and can transmit infection.',
        }
        result['observation_schema'] = {
            'spatial_shape': [2, 20, 48, 48],
            'channels': ['own_population', 'own_claim', 'own_military', 'own_supply', 'own_infected_fraction', 'visible_rival_population', 'visible_rival_claim', 'visible_rival_military', 'refugees', 'canopy', 'fire', 'stone', 'fertility', 'moisture', 'elevation', 'own_node', 'visible_rival_node', 'own_functional_node', 'coverage', 'fire_risk'],
            'scalar_fields': ['wood_div_1000', 'stone_div_1000', 'grain_div_1000', 'treasury_div_1000', 'population_div_1000', 'area_fraction', 'alive', 'episode_fraction', 'functional_fraction'] + [f'genome_{i}' for i in range(8)] + [f'reputation_{i}' for i in range(self.num_agents)],
            'remaining_scalars': 'reserved_zero',
        }
        return result

    def observe_summary(self, kingdom_id):
        if not isinstance(kingdom_id, (int, np.integer)) or not 0 <= kingdom_id < self.num_agents:
            raise ValueError('invalid kingdom_id')
        return self._metadata(int(kingdom_id))

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
