# Amendment 1 — Correctness, empirical claims, and a working vertical slice

Authorized by the user's instruction “Do what you need to do to make things work” after the initial M1 result. Original measurements and original v1 golden fixtures are retained.

## Theoretical corrections

The heterogeneous, age-structured, probabilistic-spread, seasonally forced forest implementation is not the unmodified Drossel–Schwabl model. A particular fitted exponent cannot serve as a universal implementation-correctness oracle. Even the reference model has documented broken scaling and finite-size subtleties: [Pruessner and Jensen, 2002](https://arxiv.org/abs/cond-mat/0201306). Therefore the original 1.0–1.4 interval remains a recorded hypothesis, not a release-blocking assertion. Reproducibility, transition rules, event accounting, bounded state, and meaningful numerical analysis remain required. No claim of SOC has been established.

The blanket claim that k=2 necessarily gives a discontinuous k-core transition is also incorrect. For random networks, k=1 and k=2 transitions can be continuous, with the hybrid transition occurring at k≥3: [Zhu and Chen, 2017](https://arxiv.org/abs/1710.02959). A directed, typed dependency graph is not automatically in the same class as an undirected random-network k-core. The implementation retains type-specific dependency thresholds and validates greatest-fixed-point pruning, monotonicity, and delayed recovery. Its phase diagram must be measured for its actual topology.

The stated Allee reaction multiplies logistic growth by (n/A−1), which changes the effective growth scale as A varies. The original assertion that it always travels more slowly than the Fisher front with the same r does not follow simply from calling the front “pushed.” Transport/reaction correctness and later front-speed measurements must use the implemented equation and coefficient convention.

The original milestone rule is superseded: empirical research hypotheses no longer prevent implementing downstream systems. Functional tests and measured limitations remain explicit; experimental success is never fabricated.

## Engineering amendments

- A deterministic C++ civilization layer now couples population, claims, supply, structures, economy, disease, military, refugees, collapse, and treaties. Recovery periodically recomputes the greatest eligible fixed point. The initial graph representation uses bounded dependency arrays and full pruning rather than incremental CSR; this favors inspectability while the topology is still being validated.
- Logistics uses an indexed binary-heap Dijkstra every four ticks. Integer costs and deterministic tie breaking are preserved. Array storage is preallocated.
- Population transport is split from reaction and uses a bounded conservative directional flux. Scalar density is Q16.16; aggregate stocks and population totals use 64-bit Q48.16 to avoid world-total overflow.
- Farming depletion now multiplies remaining fertility, matching the original equation. The prior constant-depletion implementation was a bug.
- Render texture resolution follows screen scale. Simulation time is measured independently of frame count, with bounded per-frame work. Dirty chunks refresh oldest-first with a time budget so controls remain responsive when software rendering or accelerated simulation cannot keep up. At high speeds the display can lag; no unrestricted 60 FPS guarantee is claimed.
- Python uses a small C ABI and ctypes, avoiding an additional pybind11 build dependency. The interface still uses the C++ simulation, independent vectorized environments, two-scale masked observations, validated macro actions, and deterministic reset/step. NumPy/PyTorch are optional Python dependencies.
- C++ CLI replays are version 2 seed/config/checkpoint streams for scripted policies. Python action-driven sessions save input history and a final digest, and reconstruct/verify on load. Constant-time binary snapshots are not implemented.
- A shared-actor PPO baseline with centralized joint-observation critic, macro-step discounting, GAE, clipping, entropy, gradient clipping, optimizer checkpoints, curriculum presets, and a checkpoint-pool utility is runnable. Training convergence, exploitability reduction, and the scientific ablations remain experiments, not completed deliverables.

## Replay compatibility

Simulation/configuration changes intentionally invalidate v1 hashes. V1 goldens are archived under `docs/archive/golden-v1/`; v2 goldens are regenerated and checked under GCC and Clang. Original forest data remain unchanged and can still be analyzed independently.
