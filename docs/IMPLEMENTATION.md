# Implementation coverage

The project now provides a runnable simulation/viewer/training vertical slice. [Amendment 1](AMENDMENT-1.md) supersedes the original blocking research gates while preserving the original document and measurements. Working mechanisms do not imply that all proposed experimental results have been obtained.

| Area | Implemented | Remaining |
|---|---|---|
| Core | C++20, fixed-point fields, 64-bit aggregates, aligned SoA, immutable validated config, PCG/stateless cell RNG, persistent row workers, canonical XXH3 hashes | TSan/MSan, MSVC/macOS measurements, general transcendental LUTs |
| Forest/terrain | Integer terrain, moisture BFS, forest warmup and CA, causal fire merging, soil recovery/depletion, smoke | Exact prescribed stone-deposit distribution; isolated-reference and multi-seed criticality studies |
| Population/territory | Strong Allee growth, stable diffusion/directional movement, supply/food carrying capacity, claims, area/perimeter/components | Full Fisher/Allee wave-speed experiment, calibrated attractiveness model |
| Economy/logistics | Harvest, finite stone mining, farming, consumption, taxes, superlinear spatial administration, indexed-heap supply, roads/depots | Calibrated sustainable-size optimum and full local production modifiers |
| Structures/cascade | 13 catalogue types with costs/prerequisites, construction, repair, upkeep/fire/siege damage, greatest-fixed-point pruning, periodic recovery, graph-component collapse | Incremental CSR, complete type-specific bonuses, phase diagrams and hysteresis sweeps |
| Disease/war/refugees | Spatial infection/recovery/immunity, market coupling, quarantine/trade controls, recruitment and directional military fields, combat/attrition, scatter, carried genes/infection, absorption and founding | Measured epidemic/Lanchester laws; all five balanced death causes; observed founding rate; full fortification/ruin-capture mechanics |
| Policies/diplomacy | Random, Greedy, Sustainer, Expansionist, TitForTat; genome-dependent weights; proposals/acceptance/breaks, quotas, monitoring and reputation | Calibrated monitoring experiments, coalition mechanics, non-transitive tournament results |
| Viewer | Isometric layers, population/structure/refugee overlays, dirty chunks, adaptive raster resolution, frame budgets, camera, kingdom/forest dashboards, dependency inspector | Strict cross-chunk occlusion, all specified dashboards, lag-free accelerated display and hardware performance targets |
| Persistence | V2 scripted seed/config/checkpoint replays; verified Python action-session save/load | Binary snapshots/LZ4, random-access replay, action-log playback in viewer |
| Python/RL | C ABI/ctypes, two-scale masked observations, macro actions, independent VecWorlds, shared-actor PPO/central critic, checkpoints/resume, manual curricula, checkpoint-pool utility | PettingZoo/Gymnasium adapters, fully observed critic tensor, automated league/curriculum, potential-based reward proof, exploitability/convergence and ablation results |

## Practical boundaries

- Config keys correspond to implemented controls; unsupported design parameters are rejected. `simulation.enabled=0` isolates the forest. `simulation.policy` is 0 Random, 1 Greedy, 2 Sustainer, 3 Expansionist, 4 TitForTat, or 5 to mix policies by kingdom ID.
- Structures use their catalogue dependencies, but not every advertised bonus is complete. Capitals/granaries/depots supply, roads lower travel cost, farms improve production, markets earn trade income and transmit infection, and barracks enable recruitment. Other industrial types currently matter principally as dependencies. Do not treat this as calibrated economic balance.
- Supply is recomputed every four ticks; cascades use the most recent supply field. Recovery explicitly restarts from eligible candidates, avoiding a failed mutual-dependency cycle that could never recover under bottom-up restoration alone.
- Population diffusion and directional flux are conservative up to fixed-point rounding; reaction, famine, fire, disease, recruitment, and combat change mass deliberately. Refugee genomes travel as population-weighted concentrations. Research-grade event/mass attribution still needs broader validation.
- Collapse pressure attribution uses exponentially decayed contributions with a roughly 500-tick timescale, not a literal 500-tick rectangular history window.
- V2 changes intentionally invalidate original digests. Archived v1 fixtures and original forest data remain available. The Python bridge rejects invalid actions before changing any state.
- The trainer is an executable baseline. Successful smoke updates demonstrate that the bridge, distributions, rollout/GAE, optimization, and checkpoint paths run. They do not demonstrate learning a successful strategy or fulfilling the original M7/M8 acceptance criteria.
