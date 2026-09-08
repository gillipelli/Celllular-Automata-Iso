# PROJECT ASHFALL — Design Document

**A multi-agent isometric cellular-automata simulation of resource competition, imperial overextension, and collapse.**

| | |
|---|---|
| Document version | 1.0 |
| Status | Baseline for implementation |
| Language | C++20 |
| Target platforms | Linux (primary), Windows, macOS |
| Training stack | Python 3.11 + PyTorch, via pybind11 |

---

> **Implementation amendment (2026-09-07):** The user authorized necessary corrections after the original forest gate failed. [Amendment 1](docs/AMENDMENT-1.md) supersedes the universal exponent/discontinuity claims and the associated blocking milestone policy. Original wording is retained below for traceability. Experimental outcomes are measured, not assumed.

## Table of Contents

1. [Vision and Scope](#1-vision-and-scope)
2. [Design Pillars](#2-design-pillars)
3. [Glossary](#3-glossary)
4. [System Architecture](#4-system-architecture)
5. [World Representation](#5-world-representation)
6. [Tick Schedule](#6-tick-schedule)
7. [Determinism and Randomness](#7-determinism-and-randomness)
8. [Subsystem: Forest and Fire](#8-subsystem-forest-and-fire)
9. [Subsystem: Terrain, Stone, and Soil](#9-subsystem-terrain-stone-and-soil)
10. [Subsystem: Kingdoms and Population](#10-subsystem-kingdoms-and-population)
11. [Subsystem: Territory and Claim](#11-subsystem-territory-and-claim)
12. [Subsystem: Economy and Logistics](#12-subsystem-economy-and-logistics)
13. [Subsystem: Structures and the Dependency Graph](#13-subsystem-structures-and-the-dependency-graph)
14. [Subsystem: Collapse](#14-subsystem-collapse)
15. [Subsystem: Epidemic](#15-subsystem-epidemic)
16. [Subsystem: Warfare](#16-subsystem-warfare)
17. [Subsystem: Refugees and Strategy Transmission](#17-subsystem-refugees-and-strategy-transmission)
18. [Game-Theoretic Layer](#18-game-theoretic-layer)
19. [Agent Interface](#19-agent-interface)
20. [Reinforcement Learning](#20-reinforcement-learning)
21. [Rendering](#21-rendering)
22. [Engineering Standards](#22-engineering-standards)
23. [Performance Budget](#23-performance-budget)
24. [Serialization, Replay, and Tooling](#24-serialization-replay-and-tooling)
25. [Testing and Validation](#25-testing-and-validation)
26. [Instrumentation and Metrics](#26-instrumentation-and-metrics)
27. [Milestones](#27-milestones)
28. [Risk Register](#28-risk-register)
29. [Appendix A: Parameter Reference](#appendix-a-parameter-reference)
30. [Appendix B: Analytical Results](#appendix-b-analytical-results)
31. [Appendix C: File Formats](#appendix-c-file-formats)
32. [Appendix D: Core API Sketch](#appendix-d-core-api-sketch)

---

## 1. Vision and Scope

### 1.1 One-paragraph summary

Ashfall is a simulation in which several **kingdoms** — represented not as unit rosters but as continuous density fields over a cellular grid — compete for wood, stone, and arable land on a landscape that is itself a living cellular automaton. Kingdoms grow, claim territory, build interdependent structures, sign and break treaties, and fight. They also **die**, and the manner of their death is the point of the project: collapse emerges from bootstrap percolation on a structure-dependency graph, driven by resource depletion, superlinear administrative cost, wildfire, epidemic, and war. When a kingdom collapses, its population converts into a diffusing refugee field that can reseed elsewhere, be absorbed by rivals, or dissipate — and refugees carry their kingdom's strategy parameters with them, making policy a heritable trait. The whole thing renders in an isometric view where the vertical axis carries real information: terrain elevation, forest canopy, structure height, smoke, and population density columns.

### 1.2 What this project is for

The simulation is a vehicle for four things, in priority order:

1. **Demonstrating measurable critical phenomena.** Every major subsystem is chosen because it has known analytic or scaling behavior we can verify: self-organized criticality in the forest-fire model, traveling-wave speeds in Fisher–KPP population spread, a discontinuous transition in *k*-core bootstrap percolation, an epidemic threshold, and a closed-form Nash/social-optimum gap in the common-pool resource game. **If a subsystem cannot be validated against theory or a measurable scaling law, it does not belong in the core.**
2. **A genuinely hard multi-agent RL problem** with non-stationarity, partial observability, sparse-but-shaped rewards, and a real social dilemma at its center.
3. **A high-performance C++ codebase** exercising SoA layout, deterministic fixed-point arithmetic, data-parallel CA updates, incremental graph algorithms, and a headless/render split.
4. **A visually legible artifact.** An observer with no context should be able to watch a run and understand that something rose and then fell.

### 1.3 Explicit non-goals

- **Not an RTS.** There is no unit selection, no micro, no click-to-command. Individual people are never represented.
- **Not a historical model.** Mechanics are named after historical phenomena for legibility, not fidelity. No claims are made about real societies.
- **Not photorealistic.** Isometric sprites and flat-shaded voxel columns. Art is functional.
- **Not a networked multiplayer game.** Human input, if any, is single-seat via the same policy interface agents use.
- **No procedural narrative, tech trees, named characters, or diplomacy dialogue.** These add scope without adding measurable behavior.

### 1.4 Target configuration

| Property | Baseline | Range supported |
|---|---|---|
| Grid size *N*×*N* | 256 × 256 | 64 – 1024 |
| Kingdoms *n* | 4 | 2 – 12 |
| Sim tick | 1 tick | — |
| Macro decision interval *K* | 16 ticks | 4 – 64 |
| Episode length | 20,000 ticks | 2,000 – 200,000 |
| Headless throughput target | ≥ 700 ticks/s/thread @ 256² | See §23 |

---

## 2. Design Pillars

These are the tie-breakers. When a design decision is contested, resolve it against this list in order.

**P1 — Emergence over scripting.** No event in the simulation may be triggered by a hand-authored condition of the form "if X then dramatic thing." Collapse, migration, alliance, and famine must all fall out of local rules. If a desired behavior cannot be produced by a rule, the behavior is cut, not scripted.

**P2 — Every mechanic has a measurable signature.** Each subsystem ships with a metric and an expected functional form (a power law, a threshold, a wave speed, a closed-form equilibrium). This is what makes the project interesting rather than merely elaborate, and it is what lets us detect regressions in a system too complex to eyeball.

**P3 — The headless core is the product; the renderer is a client.** The simulation core has zero dependencies on graphics, windowing, or input. It must be possible to run millions of ticks with the renderer never compiled in.

**P4 — Bit-exact reproducibility.** Seed plus action log reproduces a run exactly, across platforms. Non-negotiable, because the most interesting failures happen 40,000 ticks into a training run and cannot otherwise be debugged.

**P5 — Fields, not entities.** Populations, claims, resources, and refugees are all continuous fields on the grid. Structures are the only discrete objects, and there are at most a few thousand of them. This keeps the update cost predictable and the observation space natural for a convolutional encoder.

**P6 — Vertical axis carries information.** Anything drawn with height must encode a state variable. Decorative elevation is forbidden.

---

## 3. Glossary

| Term | Meaning |
|---|---|
| **Tick** | One step of the simulation core. All fields advance once. |
| **Macro step** | One agent decision, taken every *K* ticks. The unit of the RL SMDP. |
| **Kingdom** | A competing agent, identified by index *k* ∈ [0, n). Owns population, claim, stock, and structure fields. |
| **Layer** | A grid-shaped array of one scalar per cell. Layers may be per-world or per-kingdom. |
| **Claim** | Per-kingdom scalar field expressing territorial control; the argmax over kingdoms defines ownership. |
| **Stock** | A kingdom's global inventory of a resource (wood, stone, grain). |
| **Node** | A discrete structure instance in the dependency graph. |
| **Functional** | A node is functional if its supply and dependency thresholds are met this tick. |
| **Cascade** | The fixed-point iteration that determines the functional set after a perturbation. |
| **Pressure** | Any scalar that pushes the dependency graph toward cascade: depletion, upkeep deficit, fire, disease, siege. |
| **Collapse** | A cascade event in which the functional giant component of a kingdom drops below a threshold fraction. |
| **Scatter** | Conversion of a collapsed kingdom's population field into the refugee field. |
| **Genome** | A kingdom's vector of policy/behavior parameters, heritable through refugee absorption. |
| **Confluence** | The property that a field update yields identical results regardless of the traversal order within a pass. |

---

## 4. System Architecture

### 4.1 Layer diagram

```
┌──────────────────────────────────────────────────────────────┐
│  Clients                                                     │
│  ┌────────────┐ ┌────────────┐ ┌───────────┐ ┌────────────┐  │
│  │ Isometric  │ │ Headless   │ │ Replay    │ │ Python     │  │
│  │ Viewer     │ │ Runner     │ │ Inspector │ │ Gym Bridge │  │
│  └─────┬──────┘ └─────┬──────┘ └─────┬─────┘ └─────┬──────┘  │
└────────┼──────────────┼──────────────┼─────────────┼─────────┘
         │              │              │             │
┌────────▼──────────────▼──────────────▼─────────────▼─────────┐
│  ashfall_sim  (static library, no I/O, no graphics)          │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ World  — owns all state, exposes step(actions)          │  │
│  └────────────────────────────────────────────────────────┘  │
│  ┌──────────┬──────────┬──────────┬──────────┬───────────┐  │
│  │ Forest   │ Soil     │ Population│ Epidemic │ Combat    │  │
│  │ System   │ System   │ System    │ System   │ System    │  │
│  ├──────────┼──────────┼──────────┼──────────┼───────────┤  │
│  │ Claim    │ Logistics│ Structure │ Cascade  │ Refugee   │  │
│  │ System   │ System   │ System    │ System   │ System    │  │
│  ├──────────┴──────────┴──────────┴──────────┴───────────┤  │
│  │ Diplomacy System (treaties, reputation, coalitions)    │  │
│  └────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ ashfall_core — Grid, Layer<T>, Fixed, Rng, Hash, Config │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 Directory layout

```
ashfall/
  CMakeLists.txt
  cmake/                      # toolchain, sanitizer, warning presets
  config/
    default.toml              # baseline parameters
    presets/                  # tuned scenarios: sandbox, scarcity, plague, tourney
  include/ashfall/            # public headers, mirrors src/
  src/
    core/
      fixed.hpp               # Q16.16 fixed-point type
      grid.hpp                # index math, neighbor iteration, Morton (optional)
      layer.hpp               # Layer<T>, DoubleLayer<T>
      rng.hpp                 # PCG64 streams
      hash.hpp                # xxh3 state digest
      config.hpp/.cpp         # TOML load, schema validation
      profile.hpp             # scoped timers, counters
    sim/
      world.hpp/.cpp          # tick orchestration, state ownership
      forest.hpp/.cpp
      soil.hpp/.cpp
      population.hpp/.cpp
      claim.hpp/.cpp
      logistics.hpp/.cpp      # flow fields, supply reachability
      structure.hpp/.cpp      # node store, placement
      cascade.hpp/.cpp        # incremental k-core
      epidemic.hpp/.cpp
      combat.hpp/.cpp
      refugee.hpp/.cpp
      collapse.hpp/.cpp       # collapse detection + scatter
    game/
      treaty.hpp/.cpp
      reputation.hpp/.cpp
      coalition.hpp/.cpp
      cpr.hpp/.cpp            # analytic reference solutions
    agent/
      policy.hpp              # IPolicy interface
      scripted/               # random, greedy, sustainer, expansionist, tit-for-tat
      observation.hpp/.cpp    # observation encoder
      action.hpp/.cpp         # action decode + validation
    render/
      iso.hpp/.cpp            # projection, sorting, chunk cache
      camera.hpp/.cpp
      overlay.hpp/.cpp        # claim, fire-risk, supply, disease heatmaps
      hud.hpp/.cpp            # metric panels, log-log plots
      sprites.hpp/.cpp
    bridge/
      py_module.cpp           # pybind11, VecWorld
    tools/
      headless_main.cpp
      replay_main.cpp
      analyze_main.cpp        # exponent fits, transition sweeps
  tests/
    unit/                     # doctest
    property/                 # confluence, conservation
    statistical/              # long-run validation (tagged slow)
    golden/                   # replay digests
  python/
    ashfall_env.py            # Gymnasium / PettingZoo wrappers
    train_mappo.py
    league.py
    analysis/                 # notebooks, plotting
  docs/
    DESIGN.md                 # this document
    VALIDATION.md             # measured results log
```

### 4.3 Dependencies

| Dependency | Purpose | Notes |
|---|---|---|
| **CMake ≥ 3.24** | Build | FetchContent for all C++ deps |
| **raylib 5.x** | Windowing, 2D drawing, input | Renderer only; never linked into `ashfall_sim` |
| **PCG** (vendored, ~200 lines) | RNG | Header-only, trivially deterministic |
| **xxHash** | State digests | XXH3_64 |
| **toml++** | Config parsing | Header-only |
| **doctest** | Unit/property tests | Fast compile |
| **pybind11** | Python bridge | Optional target |
| **nanobench** | Microbenchmarks | Dev-only |

Deliberately absent: Eigen (the linear algebra needed is small and dense; hand-rolled beats a dependency), any ECS framework (P5 makes it unnecessary), any logging framework (a 60-line ring-buffer logger suffices), Boost.

### 4.4 Build targets

| Target | Contents | Links graphics? |
|---|---|---|
| `ashfall_core` | `src/core` | No |
| `ashfall_sim` | `src/sim`, `src/game`, `src/agent` (minus render deps) | No |
| `ashfall_headless` | CLI runner | No |
| `ashfall_viewer` | Renderer + viewer | Yes |
| `ashfall_py` | pybind11 module | No |
| `ashfall_tests` | All test tiers | No |

CI must build `ashfall_headless` with graphics libraries entirely absent from the image. This is how P3 is enforced mechanically rather than by convention.

---

## 5. World Representation

### 5.1 Grid and indexing

The world is a square lattice of side *N*, with **fixed (non-periodic) boundaries**. Boundary cells are ordinary cells whose out-of-range neighbors are treated as permanently empty and non-conductive. Periodic boundaries are rejected: they remove the coastline/frontier asymmetry that drives interesting territorial dynamics, and they make "distance to capital" ill-defined.

Row-major indexing: `idx = y * N + x`. Morton ordering is **not** used in v1. It is a documented optimization to revisit only if profiling shows L2 misses dominating the field update, and it would be introduced behind the `Grid` abstraction so no call site changes.

Neighborhoods:
- **N4** (von Neumann) for diffusion, fire spread, and supply propagation. Fire spread on N4 gives a cleaner percolation threshold than N8.
- **N8** (Moore) for structure adjacency checks and claim smoothing only.

### 5.2 Layers

```cpp
template <typename T>
class Layer {
public:
  explicit Layer(int n, T fill = T{});
  T&       at(int x, int y);
  const T& at(int x, int y) const;
  T*       data();
  int      side() const;
  size_t   size() const;              // n*n
  void     fill(T v);
  uint64_t digest() const;            // XXH3 over raw bytes
};

template <typename T>
class DoubleLayer {                    // read from front, write to back, then swap
public:
  const Layer<T>& front() const;
  Layer<T>&       back();
  void            swap();
};
```

All layers are `std::vector`-backed, 64-byte aligned, structure-of-arrays. There is no per-cell struct anywhere in the simulation core.

### 5.3 Layer inventory

**World layers** (one instance each):

| Layer | Type | Range | Description |
|---|---|---|---|
| `elevation` | `uint8` | 0–63 | Static terrain height. Affects movement cost, fire spread, farm yield. |
| `terrain` | `uint8` | enum | `WATER, PLAIN, HILL, MOUNTAIN, ROCK` |
| `forest_state` | `uint8` | enum | `EMPTY, SAPLING, TREE, BURNING, ASH` |
| `forest_age` | `uint8` | 0–255 | Ticks in current state; drives sapling→tree and ash→empty. |
| `fertility` | `fx16` | 0–1 | Soil quality; depleted by farming, recovers slowly, boosted by ash. |
| `stone` | `uint16` | 0–65535 | Non-renewable deposit remaining. |
| `moisture` | `fx16` | 0–1 | Static-ish; from distance to water. Modulates fire and yield. |
| `smoke` | `fx16` | 0–1 | Cosmetic + observation channel; advects and decays. |
| `refugee_pop` | `fx16` | ≥ 0 | Unaffiliated displaced population. |
| `refugee_genome` | `fx16 × G` | — | Population-weighted mean genome carried by refugees. |

**Per-kingdom layers** (one instance per kingdom *k*):

| Layer | Type | Description |
|---|---|---|
| `pop[k]` | `fx16` | Population density, in units of *K*_max per cell. |
| `claim[k]` | `fx16` | Territorial claim strength, 0–1. |
| `infected[k]` | `fx16` | Infectious fraction of `pop[k]`. |
| `immune[k]` | `fx16` | Recovered/immune fraction. |
| `supply[k]` | `uint16` | Distance-to-nearest-functional-depot, from flow-field BFS. `0xFFFF` = unsupplied. |
| `military[k]` | `fx16` | Deployed military strength density. |

Memory at *N* = 256, *n* = 4: 65,536 cells × (10 world layers ≈ 20 B + 4 × 6 kingdom layers ≈ 4 × 14 B) ≈ 5 MB, plus double-buffers for the ~8 layers that need them (≈ +3 MB). Comfortable. At *N* = 1024 it is roughly 130 MB, still fine for a single world but the reason vectorized training uses smaller grids.

### 5.4 Fixed-point type

```cpp
struct fx16 {                  // Q16.16 signed
  int32_t raw;
  static constexpr int SHIFT = 16;
  static constexpr int32_t ONE = 1 << SHIFT;

  static fx16 from_int(int v);
  static fx16 from_double(double v);   // construction/config only, never in tick
  double      to_double() const;       // rendering/observation only

  fx16 operator+(fx16) const;
  fx16 operator-(fx16) const;
  fx16 operator*(fx16) const;          // (int64)a.raw * b.raw >> 16
  fx16 operator/(fx16) const;          // ((int64)a.raw << 16) / b.raw
};
```

Resolution is 2⁻¹⁶ ≈ 1.5 × 10⁻⁵, range ±32768. Population densities are normalized to [0, 1] per cell, so precision is ample. Multiplication goes through `int64_t` to avoid overflow. Division by zero is a debug assertion and returns zero in release.

Where fixed-point is required:
- All state stored in layers.
- All arithmetic in the tick path.

Where floating-point is permitted:
- Config parsing and parameter derivation (done once at init, then converted).
- The renderer.
- Observation encoding for the RL bridge (the network's inputs need not be bit-exact; only the *simulation* must be).
- Offline analysis tools.

**Rationale.** IEEE floats are deterministic for a fixed binary on a fixed platform, but auto-vectorization, FMA contraction, and differing libm implementations break cross-platform reproducibility. Since P4 requires replays produced on a training cluster to be inspectable on a developer laptop, fixed-point is the honest choice. The measured cost is roughly 10–20% over float on the field update, which the budget in §23 absorbs.

Transcendentals (`exp`, `log`, `sqrt`) needed in the tick path use small lookup tables with linear interpolation, defined in `core/fixed_math.hpp`. Each is tested against `double` reference to a tolerance of 2⁻¹².

---

## 6. Tick Schedule

Update order is a load-bearing design decision, not an implementation detail. It is fixed, documented here, and asserted in tests.

```
Tick t:
   0. Input          Apply pending macro actions (only on ticks where t % K == 0)
   1. Forest         Growth, ignition, fire propagation, ash decay        [CA, double-buffered]
   2. Soil           Fertility depletion/recovery, ash deposition          [CA, double-buffered]
   3. Logistics      Flow-field BFS per kingdom → supply[k]                [graph]
   4. Structures     Construction progress, upkeep charge, damage          [entity]
   5. Cascade        Incremental k-core → functional set                   [graph, fixed point]
   6. Economy        Extraction, production, consumption, stock update     [reduce + scatter]
   7. Population     Growth (logistic + Allee), diffusion, advection       [CA, double-buffered]
   8. Epidemic       SIR update + trade-route coupling                     [CA, double-buffered]
   9. Combat         Contested-cell Lanchester attrition                   [CA, double-buffered]
  10. Claim          Claim generation, diffusion, decay, contest           [CA, double-buffered]
  11. Refugees       Diffusion, mortality, absorption, reseeding           [CA, double-buffered]
  12. Collapse       Detect collapse condition, scatter, retire kingdom    [event]
  13. Diplomacy      Treaty evaluation, violation detection, reputation    [event, every K ticks]
  14. Metrics        Sample counters, append to history ring               [read-only]
  15. Digest         Every H ticks, hash full state into replay stream     [read-only]
```

**Invariants.**

- Phases 1, 2, 7, 8, 9, 10, 11 are **confluent**: they read exclusively from front buffers and write exclusively to back buffers, so the result is independent of cell traversal order. This is what licenses parallelism and is directly unit-tested (§25.2).
- Phases 3, 5, 6, 12, 13 are sequential and order-dependent by nature.
- No phase may read a back buffer. Enforced by returning `const Layer&` from `front()` and making `back()` accessible only to the owning system during its own phase (via a pass token type).
- Cascade (5) runs **before** economy (6) so that a structure destroyed this tick produces nothing this tick.
- Collapse (12) runs **after** all field updates so a kingdom scatters from a fully-settled state.

**Rationale for placement of logistics before cascade:** supply reachability is an input to functionality, and functionality determines which depots exist. This is genuinely circular. We break the cycle by using the *previous* tick's functional set to compute flow fields, then computing the new functional set. This introduces one tick of lag, which is invisible at *K* = 16 and avoids an inner fixed-point loop that would cost far more.

---

## 7. Determinism and Randomness

### 7.1 Stream architecture

A single global RNG is unacceptable: adding one call anywhere shifts every subsequent draw, and any change in thread scheduling reorders consumption. Instead, Ashfall uses **independent PCG64 streams keyed by (subsystem, kingdom, tick-block)**.

```cpp
enum class RngDomain : uint16_t {
  ForestGrowth, ForestLightning, FireSpread, SoilNoise,
  PopulationNoise, EpidemicSeed, EpidemicSpread,
  CombatNoise, RefugeeWander, StructureFailure,
  Mutation, AgentExploration, ScenarioInit
};

class RngBank {
public:
  RngBank(uint64_t world_seed);
  // Deterministic per (domain, kingdom, tick). Independent of call order.
  Pcg64 stream(RngDomain d, int kingdom, uint64_t tick) const;
  // Per-cell draw with no sequential state — safe under any parallel schedule.
  uint32_t cell(RngDomain d, uint64_t tick, int idx) const;
};
```

`cell()` is the critical primitive. It is a **stateless hash**, not a sequence:

```cpp
uint32_t RngBank::cell(RngDomain d, uint64_t tick, int idx) const {
  uint64_t h = splitmix64(world_seed_ ^ (uint64_t(d) * 0x9E3779B97F4A7C15ull));
  h = splitmix64(h ^ tick);
  h = splitmix64(h ^ uint64_t(idx));
  return uint32_t(h >> 32);
}
```

Because each cell's draw depends only on `(seed, domain, tick, index)`, **parallel CA updates are bit-identical regardless of thread count, chunk size, or scheduling order.** This single decision eliminates the largest class of nondeterminism bugs in parallel simulations, and it is why it appears this early in the document.

Sequential streams from `stream()` are used only in genuinely sequential phases (cascade tie-breaking, treaty evaluation, scatter), where the consumption order is itself deterministic.

### 7.2 Reproducibility contract

A run is defined by the tuple `(config_hash, world_seed, action_log)`. Replaying that tuple must produce an identical state digest at every checkpoint tick, on any supported platform, at any thread count, in debug or release.

Enforcement:
- **Golden replays** in CI: fixed seeds, 5,000 ticks, digest compared byte-for-byte (§25.4).
- **Thread-count sweep**: the same replay is run at 1, 2, 4, and 8 threads and digests are compared.
- **Cross-compiler check**: GCC and Clang builds compared in CI; MSVC checked at release.
- **Digest cadence**: full-state XXH3 every `H = 64` ticks by default, every tick under `--paranoid`.

### 7.3 Forbidden constructs in the simulation core

Enumerated so they can be grepped for and caught in review:

- `float`, `double` in any tick-path code.
- `std::unordered_map` / `std::unordered_set` iteration (order is implementation-defined). Use sorted `std::vector` or a deterministic open-addressing map with fixed capacity.
- Pointer values used in comparisons or hashes.
- `std::sort` on ranges with equal keys, without a tiebreaker. Use `std::stable_sort` or make keys total.
- `std::random_device`, `std::mt19937` seeded from time, `rand()`.
- Any `#pragma omp` reduction over floats.
- Reading uninitialized memory (caught by MSan in CI).

---

## 8. Subsystem: Forest and Fire

### 8.1 Model

The forest uses the **Drossel–Schwabl forest-fire model**, chosen because it self-organizes to criticality with only two parameters and therefore produces power-law-distributed burn sizes without tuning.

Cell states and transitions, per tick:

| From | To | Condition |
|---|---|---|
| `EMPTY` | `SAPLING` | with probability *p* · *g*(cell) |
| `SAPLING` | `TREE` | after `t_mature` ticks |
| `TREE` | `BURNING` | if any N4 neighbor is `BURNING`, with probability *s*(cell); **or** spontaneously with probability *f* (lightning) |
| `SAPLING` | `BURNING` | as `TREE` but with spread probability *s*·0.6 |
| `BURNING` | `ASH` | after `t_burn` ticks (default 1) |
| `ASH` | `EMPTY` | after `t_ash` ticks; deposits fertility |

Local modifiers:

```
g(cell) = base_growth · fertility · moisture · (1 − 0.5·slope_penalty)
s(cell) = base_spread · (1 − 0.7·moisture) · (1 + 0.4·uphill) · dryness(t)
```

`dryness(t)` is a slow global oscillator (a fixed-point sine over a long period, default 2,000 ticks) producing seasonal fire risk. This is the one concession to "drama," and it is justified: it prevents the fire regime from being perfectly stationary, which makes the RL problem non-trivially time-dependent, and it is a single global scalar the agent can observe.

### 8.2 Criticality regime

SOC requires timescale separation: **f ≪ p ≪ 1/t_burn**. Defaults: *p* = 0.02, *f* = 2 × 10⁻⁵, so *f/p* = 10⁻³.

Expected behavior: the distribution of burn cluster sizes *s* follows *P*(*s*) ∝ *s*^(−τ) with a cutoff at the system size. Reported values for the 2D DS model cluster around τ ≈ 1.1–1.2, with well-documented slow convergence and finite-size effects — so the validation criterion is **not** a specific τ but:

1. A straight region on the log-log complementary CDF spanning ≥ 2 decades.
2. An MLE-fitted τ (Clauset–Shalizi–Newman method, with *s*_min selected by KS-distance minimization) in [1.0, 1.4].
3. The fitted exponent stable to within ±0.1 when *N* doubles from 128 to 256.

This is recorded in `docs/VALIDATION.md` after each change to forest parameters.

### 8.3 Implementation

Fire propagation is the one place where a naive full-grid scan is wasteful, since burning cells are a tiny fraction of the grid. Use an **active-set** approach:

```cpp
class ForestSystem {
  std::vector<int32_t> burning_;      // active burning cell indices
  std::vector<int32_t> burning_next_;
  std::vector<uint8_t> touched_;      // dedupe bitmap, cleared via touched_list_
  std::vector<int32_t> touched_list_;
};
```

- Growth and lightning are a full-grid pass (cheap, branch-light, parallelizable by row bands).
- Spread iterates only `burning_`, pushing newly ignited indices into `burning_next_`. Deduplication uses a bitmap plus an explicit clear list so no `memset` of the full grid is needed.
- Because ignition uses `RngBank::cell(FireSpread, tick, idx)` keyed on the *target* cell, and ignition is idempotent (a cell either ignites or not, regardless of how many burning neighbors it has), **the result is order-independent** even though the active set is traversed sequentially. A target ignites if *any* neighbor test passes; we evaluate `p_ignite = 1 − (1 − s)^(burning_neighbor_count)` in a single test per target cell during the full-grid pass instead of per-neighbor tests, making confluence exact and cheap.

Revised spread rule (this is the version to implement):

```
For each cell in TREE/SAPLING state:
  b = count of N4 neighbors in BURNING state       (from front buffer)
  if b > 0:
    p = 1 - pow_fx(1 - s(cell), b)                 // table-driven
    if cell_rand(FireSpread, t, idx) < p: -> BURNING
  else if cell_rand(ForestLightning, t, idx) < f: -> BURNING
```

Cluster-size accounting for the statistics uses a union-find over cells ignited within a single contiguous fire event, with events delimited by "no burning cells adjacent in time." Simpler and sufficient: tag each ignition with the `fire_id` of the igniting neighbor (lightning starts a new id), and accumulate `size[fire_id]`. Emit a `FireEvent{id, size, start_tick, end_tick, centroid}` to the metrics stream when the last cell of that id stops burning.

### 8.4 Interaction with kingdoms

- **Harvest** converts `TREE` → `EMPTY` and adds wood to the harvesting kingdom's stock. Harvest is a *field* operation driven by the kingdom's extraction allocation (§12), not a per-unit action.
- Harvesting fragments the forest, lowering connectivity and therefore burn sizes. **This is the central strategic tension in the forest system and it is not scripted** — it falls out of percolation. Clear-cutting around your capital is genuinely protective and genuinely expensive.
- Structures within a burning cell take damage (§13.5), which is a primary cascade trigger.
- `ASH` deposits `ash_fertility_bonus` into `fertility`, so burned land is better farmland afterward. This makes fire a mixed signal rather than a pure hazard.

---

## 9. Subsystem: Terrain, Stone, and Soil

### 9.1 Terrain generation

Terrain is generated once at world init and never changes. Generation is deterministic from `world_seed`.

Algorithm:
1. **Elevation**: sum of 5 octaves of value noise on a lattice, using integer hashing (not Perlin with floats). Normalize to 0–63.
2. **Water**: cells below `sea_level` (default elevation 12) become `WATER`.
3. **Terrain class**: by elevation bands — `PLAIN` (12–28), `HILL` (29–44), `MOUNTAIN` (45–58), `ROCK` (59–63).
4. **Moisture**: `moisture = clamp(1 − d_water / moisture_range, 0, 1)` where `d_water` is a multi-source BFS distance from all water cells. Computed once.
5. **Stone deposits**: Poisson-disc-sampled seed points biased toward `MOUNTAIN`/`ROCK`, each grown into a blob by randomized flood fill with size drawn from a log-normal. Deposit richness decays from the blob center.
6. **Initial fertility**: `fertility = f_base · moisture · plain_bonus(terrain)`, with low-amplitude noise.
7. **Initial forest**: cells seeded to `TREE` with probability proportional to `g(cell)`, then the forest system is run for `warmup_ticks` (default 3,000) with no kingdoms present so the forest reaches its SOC attractor before the game begins. **This warmup is mandatory** — starting from a uniform random forest gives a first fire wildly larger than the steady-state distribution and poisons early training.

### 9.2 Stone

Stone is **non-renewable**. Extraction removes it permanently. This creates:
- Permanent geographic value, so territory near deposits is worth fighting for over the whole episode rather than transiently.
- A hard scarcity clock: an episode has a finite stone budget, so late-game stone construction is impossible and kingdoms that spent early on stone structures have a durable advantage — or a durable maintenance liability.

Extraction rate: `mined = min(stone_remaining, q_stone · effort_stone · pop_local · terrain_yield)`.

### 9.3 Soil and agriculture

Fertility is a slow variable, which matters because it introduces a timescale between the fast forest (tens of ticks) and the slow imperial cycle (thousands of ticks).

```
Δfertility = −q_farm · farm_intensity · fertility          (depletion)
             + r_soil · (fertility_max − fertility)         (logistic recovery)
             + ash_bonus · [cell became ASH this tick]
             + manure · granary_adjacency
```

with `r_soil` ≈ 2 × 10⁻⁴ per tick, i.e. a recovery half-life of roughly 3,500 ticks. Sustained over-farming therefore creates a dust-bowl that outlasts the decision horizon of a myopic agent — one of the deliberate traps in the design.

Food yield per cell: `yield = y_base · fertility · moisture · (1 + irrigation_bonus)`.

---

## 10. Subsystem: Kingdoms and Population

### 10.1 State

Each kingdom *k* has:

```cpp
struct Kingdom {
  int      id;
  bool     alive;
  Vec2i    capital;                    // may be relocated on capital loss
  Stocks   stock;                      // wood, stone, grain, treasury
  Genome   genome;                     // heritable behavior parameters, §17.3
  Reputation rep;                      // per-rival defection history, §18.3
  Allocation alloc;                    // current macro action, §19.2
  KingdomStats stats;                  // rolling aggregates for observation
};
```

Plus the per-kingdom layers listed in §5.3.

### 10.2 Population dynamics

The core equation, applied per cell:

```
∂n/∂t = D ∇²n  −  ∇·(n · v)  +  r · n · (1 − n/K) · (n/A − 1)  −  m · n
```

| Term | Meaning |
|---|---|
| `D ∇²n` | Diffusion. Population spreads to adjacent cells. Default *D* = 0.08. |
| `∇·(n v)` | Advection. Directed drift up an attractiveness gradient, §10.4. |
| `r n (1 − n/K)(n/A − 1)` | **Strong Allee logistic growth.** |
| `m n` | Mortality from famine, disease, and combat, computed by other systems. |

**The Allee term is the most important single choice in this document.** With 0 < *A* < *K*:
- For *n* < *A*, growth is **negative**. Sparse populations do not limp along; they go extinct.
- For *A* < *n* < *K*, growth is positive.
- At *n* = *K*, growth is zero.

Consequences that make the whole "die and scatter" premise work:
1. **A viability threshold exists.** A kingdom reduced below *A* over its territory is doomed regardless of what the agent does next.
2. **Hysteresis.** Restoring the conditions that existed before a decline does not restore the population, because the state has crossed a separatrix. Collapse is not reversible by undoing its cause.
3. **Refugee reseeding is a real gamble.** Scattered refugees found a new kingdom only if enough of them accumulate in one place to clear *A*. Most attempts fail. This is what makes scatter feel consequential rather than like a free respawn.

Defaults: *A* = 0.12, *K*_max = 1.0 (normalized), *r* = 0.03/tick.

### 10.3 Local carrying capacity

*K* is not constant; it is where every other subsystem's state enters population dynamics:

```
K(cell) = K_max · clamp(
    w_food · min(1, food_supply_local / food_demand_local)
  + w_house · housing_capacity(cell)
  + w_water · moisture
  − w_crowd · pollution(cell)
, 0, 1)
```

`food_supply_local` comes from the logistics flow field: a cell is fed if it lies within supply range of a functional granary with grain in it. **Cutting supply lowers K, which pushes n below A, which kills the population outright.** Siege therefore works through starvation and the Allee threshold, not through a damage number — exactly the emergent-over-scripted property P1 demands.

### 10.4 Advection

Population drifts toward attractive cells:

```
attract(cell) = a_food · expected_yield
              + a_res  · (nearby wood + stone)
              + a_safe · (own claim − rival claim)
              − a_risk · (fire risk + disease prevalence)
              − a_dist · supply_distance
```

Velocity `v = μ ∇ attract`, clamped to `v_max` for numerical stability. Advection is discretized with **first-order upwind differencing**, which is diffusive but unconditionally monotone — no negative densities, no spurious oscillations. Higher-order schemes are not worth the risk of producing negative population, which would corrupt the Allee logic.

### 10.5 Numerical stability

Explicit Euler on the diffusion term is stable in 2D only if `D · Δt / Δx² ≤ 1/4`. With Δx = Δt = 1, this requires **D ≤ 0.25**. Advection adds a CFL condition `|v| Δt / Δx ≤ 1`, so `v_max ≤ 1` cell/tick.

Both bounds are asserted at config load:

```cpp
ASHFALL_CONFIG_ASSERT(cfg.pop.D <= fx16::from_double(0.25),
  "population diffusion D exceeds 2D explicit-Euler stability limit");
ASHFALL_CONFIG_ASSERT(cfg.pop.v_max <= fx16::ONE,
  "advection velocity exceeds CFL limit");
```

An implicit or ADI solver is explicitly out of scope. If larger *D* is ever needed, run multiple diffusion substeps per tick instead — simpler, still confluent, and the cost is linear.

### 10.6 Validation: Fisher–KPP wave speed

With the Allee term replaced by plain logistic growth (`A → 0`, available via a test-only config flag), the population front invading empty land is a Fisher–KPP wave with asymptotic speed:

```
c = 2 √(r D)
```

At *r* = 0.03, *D* = 0.08: *c* = 2√0.0024 ≈ **0.098 cells/tick**.

This is a strong end-to-end test of the population integrator, and it is cheap: seed a strip on a homogeneous map, run 5,000 ticks, regress front position (the *n* = *K*/2 contour) against time, and require the measured speed within 10% of theory. Systematic under-speed indicates excessive numerical diffusion; over-speed indicates a buffering error. Test in `tests/statistical/kpp_wave.cpp`.

With the strong Allee term restored, the front becomes a **pushed wave** and travels more slowly than 2√(rD), and for sufficiently strong Allee effect it can stall or retreat entirely. The test asserts the ordering `c_allee < c_fisher`, and that the front reverses for *A* > *K*/2.

---

## 11. Subsystem: Territory and Claim

### 11.1 Purpose

Claim is the territorial control field. It answers "who owns this cell," which drives extraction rights, treaty violation detection, upkeep cost, and score.

Claim is a separate field from population because they decouple in interesting ways: a kingdom can claim more than it populates (overextension), or populate land it does not claim (refugee settlement, migration into rival territory).

### 11.2 Dynamics

Per kingdom *k*, per cell:

```
claim_k ← claim_k
        + c_gen  · pop_k · (1 − claim_k)              generation from population
        + c_diff · Σ_{j ∈ N4} (claim_k[j] − claim_k)  spatial smoothing
        + c_fort · fort_projection_k                  structures project claim
        − c_decay· claim_k                            passive decay
        − c_cont · claim_k · rival_pressure           contest
```

where `rival_pressure = Σ_{j≠k} (claim_j + w_mil · military_j)`.

Ownership of a cell is `argmax_k claim_k`, with `NONE` if the max is below `claim_floor` (default 0.15). Ties broken by lowest kingdom id, deterministically.

Claim is **not** normalized to sum to one across kingdoms. Contested cells legitimately have high claim from several kingdoms simultaneously; that is what "contested" means, and the combat system (§16) keys off exactly this condition.

### 11.3 Derived territorial quantities

Computed once per tick and cached, because several systems need them:

| Quantity | Definition | Used by |
|---|---|---|
| `area_k` | Count of cells owned by *k* | Upkeep, score |
| `perimeter_k` | Owned cells with ≥1 non-owned N4 neighbor | Upkeep, border defense |
| `mean_dist_k` | Mean L1 distance from owned cells to capital | Upkeep |
| `contested_k` | Cells where *k* has claim ≥ 0.3 and some rival also ≥ 0.3 | Combat, diplomacy |
| `components_k` | Number of connected components of owned territory | Fragmentation metric |

`mean_dist_k` is the expensive one. Rather than a full BFS per kingdom per tick, it is computed incrementally: maintain a running sum of distances, updated only for cells that changed ownership this tick, using the precomputed distance field from the capital (recomputed only when the capital moves).

---

## 12. Subsystem: Economy and Logistics

### 12.1 Resources

| Resource | Source | Renewable | Storage | Primary sink |
|---|---|---|---|---|
| **Wood** | Harvest `TREE` cells | Yes (forest CA) | Warehouse | Construction, fuel |
| **Stone** | Mine `stone` deposits | **No** | Warehouse | Construction, fortification |
| **Grain** | Farm cells (fertility × moisture) | Yes (soil recovery) | Granary | Population upkeep, military |
| **Treasury** | Taxation of population | Flow | Abstract | Administrative upkeep |

Stocks are global per kingdom, but **access is local**: a cell can only be fed, built on, or reinforced if it is within supply range of a functional depot. This is the mechanism that makes geography matter without tracking individual carts.

### 12.2 Flow-field logistics

Per-unit pathfinding is forbidden (P5). Instead, once per tick per kingdom, run a **multi-source Dijkstra** from every functional depot, producing `supply[k]`: cost-to-nearest-depot for every cell.

```cpp
void LogisticsSystem::compute(int k) {
  auto& dist = supply_[k];
  dist.fill(UNREACHABLE);
  bucket_queue_.clear();                            // integer costs → bucket queue, O(V+E)
  for (NodeId d : functional_depots_[k]) {
    dist.at(d.pos) = 0;
    bucket_queue_.push(0, index_of(d.pos));
  }
  while (auto cur = bucket_queue_.pop()) {
    // relax N4 neighbors with terrain- and road-dependent cost
  }
}
```

Movement cost per cell:

```
cost = base_cost
     × terrain_mult(terrain)               // plain 1.0, hill 1.8, mountain 3.5, water ∞ (or ferry)
     × (1 + 0.05 · |Δelevation|)           // slope penalty
     × road_mult                           // 0.35 if road structure present
     × (1 + hostile_claim · 2.0)           // rival territory is expensive to move through
```

Costs are small integers (scaled by 16), so a **bucket queue** replaces a binary heap and the whole thing is O(V + E) with excellent constant factors. At *N* = 256 this is ~65k cells × 4 edges per kingdom. Measured target: ≤ 0.35 ms per kingdom (§23).

Supply range is a hard cutoff at `supply_max_cost`. Beyond it, cells are unsupplied: no food delivery, no construction, no reinforcement, and a claim decay penalty. **Roads are the primary way to extend an empire's viable radius, and building them is a large wood/stone investment — this is the concrete form of the overextension trade-off.**

### 12.3 Production

Per tick, per kingdom:

```
labor_total      = Σ_cells pop_k(cell)
labor_available  = labor_total · (1 − military_fraction − sick_fraction)

For each sector s ∈ {wood, stone, grain}:
  effort_s   = alloc.extraction_share[s] · labor_available
  harvest_s  = Σ_cells q_s · effort_density_s(cell) · resource_available(cell) · supplied(cell)
```

Effort is distributed across cells in proportion to a per-sector desirability heuristic (resource density × supply access × ownership), **not** chosen cell-by-cell by the agent. The agent controls the sector split; the mechanical distribution within a sector is scripted. This is the key action-space reduction that makes the RL problem tractable (§20.2).

### 12.4 Consumption and upkeep

```
food_demand      = Σ_cells pop_k(cell) · food_per_capita
                 + military_k · food_per_soldier

admin_upkeep     = c_area · area_k
                 + c_dist · area_k · mean_dist_k        ← superlinear in practice
                 + c_bord · perimeter_k
                 + c_frag · (components_k − 1)²          ← fragmentation is punished

structure_upkeep = Σ_nodes node.upkeep_cost
```

**The `c_dist` term is the Tainter mechanic.** For roughly circular territory, `mean_dist ∝ √area`, so administrative cost scales as *A*^(3/2) while yield scales as *A*. Net return therefore peaks at a finite empire size and declines past it. See Appendix B.2 for the closed form: with benefit *bA* and cost *cA*^(3/2), the optimum is

```
A* = (2b / 3c)²
```

This is a genuine, tunable, analytically-known optimum. It gives us (a) a designed reason why unbounded expansion is self-defeating, (b) a validation target — a scripted expansionist agent should visibly overshoot *A*\* and decline — and (c) a benchmark for whether RL agents discover the optimum.

### 12.5 Shortfall handling

When demand exceeds supply, apply in this order:

1. **Grain shortfall** → mortality `m += famine_rate · shortfall_ratio`, and *K* drops in unsupplied cells. Famine is the leading cause of Allee-threshold crossings.
2. **Treasury shortfall** → structures go unpaid. Each unpaid node has probability `p_fail_unpaid` per tick of becoming non-functional, seeding a cascade (§14).
3. **Wood/stone shortfall** → construction halts; nothing else.

Order matters and is asserted: people starve before buildings decay, so a player who overbuilds sees population loss first, which is the legible warning signal.

---

## 13. Subsystem: Structures and the Dependency Graph

### 13.1 Node model

Structures are the only discrete entities in the simulation. Expected count: a few hundred to ~2,000 across all kingdoms.

```cpp
struct Node {
  NodeId       id;
  StructureType type;
  int          owner;              // kingdom id
  Vec2i        pos;
  fx16         integrity;          // 0..1; below repair threshold → non-functional
  fx16         progress;           // construction completion 0..1
  uint8_t      k_required;         // needs >= k_required functional dependencies
  bool         functional;         // recomputed each cascade
  uint32_t     dep_begin, dep_end; // slice into the global edge array (CSR)
};
```

Edges are stored **CSR-style** in one flat array, so cascade traversal is cache-friendly and the graph has no pointer chasing.

### 13.2 Structure catalogue

| Type | Cost (W/S) | Upkeep | Depends on (k of m) | Provides |
|---|---|---|---|---|
| `Capital` | — | high | 0 of 0 | Supply depot, claim projection, admin center |
| `Farm` | 20 / 0 | low | 1 of 1: Granary in range | Grain from fertility |
| `Granary` | 40 / 20 | med | 1 of 1: Capital or Depot in supply range | Food storage, supply depot |
| `Woodcamp` | 25 / 5 | low | 1 of 1: Depot in range | Wood extraction bonus |
| `Quarry` | 30 / 10 | med | 1 of 1: Depot in range | Stone extraction bonus |
| `Kiln` | 40 / 30 | med | **2 of 3**: Woodcamp, Quarry, Depot | Charcoal → enables Smithy |
| `Smithy` | 60 / 60 | high | **2 of 3**: Kiln, Quarry, Granary | Military equipment quality |
| `Barracks` | 50 / 40 | high | **2 of 3**: Granary, Smithy, Depot | Military recruitment capacity |
| `Wall` | 10 / 60 | low | 1 of 1: Depot in range | Defense multiplier, claim projection |
| `Road` | 15 / 10 | v.low | 1 of 2: adjacent Road or Depot | Movement cost 0.35× |
| `Depot` | 35 / 25 | med | **1 of 2**: Road chain to Capital, or Granary | Extends supply range |
| `Market` | 45 / 25 | med | **2 of 3**: Depot, Granary, Road | Trade, treasury, epidemic coupling |
| `Aqueduct` | 30 / 70 | med | 1 of 1: adjacent Aqueduct or water source | Irrigation, +moisture |

**The `k of m` thresholds are the design's core lever.** Note that most mid- and high-tier structures use *k* = 2. This is deliberate and is explained in §14.2: threshold-2 bootstrap percolation produces a **discontinuous** cascade transition, while threshold-1 reduces to ordinary percolation and produces a boring continuous one.

### 13.3 Placement

Placement is not an agent action at cell granularity. Instead the agent's macro action includes a **construction priority vector** over structure types (§19.2), and a scripted placer selects sites:

```cpp
Vec2i StructurePlacer::choose_site(int k, StructureType t) const;
```

The placer scores candidate cells by a per-type heuristic (resource proximity for extractors, supply-gap filling for depots, frontier position for walls, fertility for farms), samples the top *m* candidates with a softmax for stochasticity, and returns the best legal site. Legality requires: owned or unclaimed, in supply range, terrain-compatible, and not overlapping.

This keeps the agent's action space at ~13 continuous priorities instead of 65,536 discrete placements, and it means the *interesting* decision (what to build and how much) stays with the agent while the *tedious* decision (exactly where) is delegated.

### 13.4 Construction

Construction consumes resources up front (reserved), then progresses at a rate proportional to local supplied labor. Half-built structures are non-functional, contribute no upkeep, and can be destroyed — losing the invested resources. Abandonment refunds 25%.

### 13.5 Damage

Integrity decreases from:

| Source | Rate |
|---|---|
| Fire in the structure's cell | `fire_damage` per tick burning (large; wooden structures often lost) |
| Combat in the cell | proportional to attacking `military` density |
| Unpaid upkeep | slow decay, `decay_unpaid` |
| Age | very slow baseline, requires periodic repair investment |

Below `integrity_min` (default 0.3) a node becomes non-functional but is not deleted; it can be repaired. Below 0.0 it is destroyed and removed from the graph.

---

## 14. Subsystem: Collapse

This is the centerpiece. It is worth being precise about, because the temptation to replace it with a health bar will be constant and must be resisted.

### 14.1 The cascade

A node is functional this tick iff **all** of:

1. `integrity ≥ integrity_min`
2. `progress ≥ 1.0` (construction complete)
3. `supply[owner][pos] ≤ supply_max_cost` (reachable from a functional depot)
4. At least `k_required` of its dependency nodes are functional
5. Its owner is alive

Conditions 3 and 4 are **recursive**: functionality depends on the functionality of others. The functional set is therefore defined as the **greatest fixed point** of the above — start with all candidate nodes functional, then repeatedly remove nodes that fail, until no more removals occur.

```cpp
// Cascade to the greatest fixed point. Monotone: nodes only ever leave the set.
void CascadeSystem::run(World& w) {
  // seed the worklist with nodes failing a non-recursive condition (1,2,3,5)
  for (NodeId id : dirty_) if (!local_ok(id)) queue_.push(id);

  while (!queue_.empty()) {
    NodeId id = queue_.pop();
    if (!functional_[id]) continue;
    functional_[id] = false;
    removed_this_cascade_.push_back(id);
    for (NodeId dep : dependents_of(id))                 // reverse edges, CSR
      if (functional_[dep] && !threshold_ok(dep)) queue_.push(dep);
  }
}
```

Because removal is monotone (a node never re-enters the set during a single cascade), the fixed point is unique and independent of worklist order. This is a **provable confluence property** and is directly unit-tested by running the cascade with shuffled worklists and comparing results.

**Incrementality.** Recomputing from scratch each tick would be wasteful. Instead maintain a `dirty_` set of nodes whose local conditions changed since last tick (damaged, unpaid, supply changed, owner changed). Only these seed the worklist. Nodes can *re-enter* the functional set only via an explicit **recovery pass**, run every `recovery_interval` ticks (default 8), which re-tests non-functional nodes bottom-up. Separating removal (fast, every tick) from restoration (slower, periodic) matches the asymmetry we want: **collapse is fast, recovery is slow.**

### 14.2 Why k = 2: the discontinuous transition

This is the theoretical heart of the design.

- With **k = 1**, a node survives if any one dependency survives. The functional set is essentially the connected component containing the depot, and its size shrinks *continuously* as nodes are removed. Ordinary percolation. Empires would decay gradually and unremarkably.
- With **k ≥ 2**, the process is **bootstrap percolation** (equivalently, *k*-core decomposition). The transition is **hybrid/discontinuous**: as pressure increases, the functional giant component shrinks slowly, slowly, slowly — and then, at a critical point, a *finite fraction* of the network fails in a single cascade.

That is precisely the qualitative behavior we want: **empires that look stable right up to the moment they aren't.** We do not script "the empire looks fine then suddenly collapses." We choose *k* = 2 and the mathematics produces it.

**Validation (this is a headline result of the project).** Sweep a pressure parameter ρ (fraction of randomly disabled nodes, or upkeep deficit), and for each value record the fraction of nodes in the functional giant component *S*. Plot *S*(ρ). Requirements:

1. For *k* = 1, *S*(ρ) is continuous with no jump.
2. For *k* = 2, *S*(ρ) exhibits a jump of at least 0.25 at some ρ_c.
3. Near ρ_c the mean cascade size diverges — plot it and confirm the peak.
4. **Hysteresis**: sweeping ρ up then back down traces different curves; the system does not recover at the ρ where it failed.

Test: `tests/statistical/cascade_transition.cpp`, sweep 40 values of ρ × 200 seeds. Results logged to `docs/VALIDATION.md` with the plot.

### 14.3 Pressure sources

Different pressures produce different *shapes* of death, which is what makes runs distinguishable from each other.

| Pressure | Mechanism | Signature |
|---|---|---|
| **Depletion** | Local resource exhausted → extractor non-functional → Kiln loses a dependency → cascade | Starts at periphery, propagates inward along supply chains |
| **Overextension** | `admin_upkeep ∝ A^{3/2}` exceeds treasury → widespread unpaid decay | Simultaneous multi-site failure; the whole network is stressed at once |
| **Fire** | Percolating fire destroys a dense cluster of structures | Spatially localized, sudden, correlated with structure density |
| **Epidemic** | Labor collapse → production collapse → upkeep unmet | Slow onset, follows trade routes, hits high-density cores first |
| **War** | Siege cuts supply → condition 3 fails for a whole region at once | Sharp, directional, from the contested border |

Note that **fire and density are in tension** (dense building is efficient and flammable), and **trade and epidemic are in tension** (markets raise treasury and raise infection coupling). These trade-offs are emergent from the mechanics, not authored.

### 14.4 Collapse detection

A kingdom is declared collapsed when **any** of:

| Condition | Threshold |
|---|---|
| Functional giant component fraction | < 0.25 for `collapse_persist` ticks (default 100) |
| Total population | < `A · min_viable_cells` (i.e. Allee-doomed) |
| Capital lost and not relocated | within `capital_grace` ticks (default 300) |
| Grain stock zero and food production zero | for `starve_persist` ticks (default 150) |

The persistence windows exist so that a transient shock does not immediately end a kingdom. They also give the RL agent a **recovery window**, which is where the interesting emergency policies (abandon the periphery, dissolve the army, sue for peace) can be learned.

### 14.5 Scatter

On collapse:

1. All structures owned by *k* are marked derelict. Derelict structures decay at `derelict_decay`. **They can be claimed and repaired by another kingdom** at 40% of build cost — the ruins of a dead empire are a real prize, and a reason to be near a collapse.
2. `pop[k]` is converted into `refugee_pop`, at a conversion efficiency of `scatter_survival` (default 0.6). The remainder dies. Refugees inherit the infected fraction.
3. `refugee_genome` is updated as a population-weighted blend with kingdom *k*'s genome.
4. `claim[k]` decays to zero over `claim_dissolve` ticks (default 200), so territory is released gradually and neighbors race for it rather than instantly teleporting the border.
5. Kingdom *k* is marked `alive = false`. Its slot may be reused by a refugee-founded successor (§17.4).
6. A `CollapseEvent` is emitted with a **cause attribution** — the dominant pressure over the preceding 500 ticks, by integrated contribution. This is essential for analysis: "kingdoms died of X in Y% of runs" is one of the primary experimental outputs.

---

## 15. Subsystem: Epidemic

### 15.1 Model

Spatial **SIRS** on the population field. Per kingdom *k*, per cell, with `S = pop − I − R`:

```
new_infections = β · S · (I_local + κ · I_trade) / pop
ΔI = new_infections − γ I
ΔR = γ I − ω R                      (waning immunity, ω small)
mortality       = δ · I             (added to population mortality m)
```

`I_local` is a N4-weighted average of infected density including the cell itself. `I_trade` is the coupling through market structures: every pair of functional markets connected by a supply path exchanges infection at rate proportional to trade volume. This is a **small dense graph** (markets number in the tens), so an all-pairs computation is trivially affordable and gives long-range disease transmission without a full network simulation.

### 15.2 Threshold behavior

The basic reproduction number is *R*₀ = β/(γ + δ). For a well-mixed population, an epidemic grows iff *R*₀ > 1. On a lattice, spatial structure lowers the effective *R*₀ because infectives waste contacts on already-infected neighbors; the lattice threshold is strictly higher than the mean-field one.

**This is the trade-and-disease trade-off, and it is quantitative.** Trade coupling κ effectively raises the connectivity of the contact network, pushing a locally-subcritical outbreak over threshold. An agent that builds a dense market network gains treasury and buys a higher epidemic risk. Neither effect is scripted; both fall out of *R*₀.

Validation: for a fixed β/γ ratio, sweep κ and measure final attack rate. Expect a sigmoid with a sharp rise at a critical κ_c. Confirm that κ_c decreases as market count increases. `tests/statistical/epidemic_threshold.cpp`.

### 15.3 Sources of outbreak

- Spontaneous, at rate `p_spontaneous` per populated cell per tick (very low).
- **Refugee absorption**: refugees carry `refugee_infected`, and absorbing them transmits with probability proportional to it. This makes taking in refugees a genuine gamble — a strategic decision with an actual downside, rather than free population.
- Density-dependent: `β_eff = β · (1 + crowd_factor · pop_density)`, so cities are dangerous.

### 15.4 Countermeasures

Available through the agent's allocation vector (not as discrete buildings, to keep the action space small):

- **Quarantine**: `alloc.quarantine` raises movement cost across the borders of infected regions, reducing both diffusion and advection there. Costs treasury and reduces effective supply throughput.
- **Trade suspension**: setting `alloc.trade` to zero removes κ coupling but forfeits market treasury income.

---

## 16. Subsystem: Warfare

### 16.1 Military as a field

Military strength is a per-kingdom density layer, recruited from population:

```
recruit_rate = alloc.military · barracks_capacity_k · available_labor
```

Recruits are removed from `pop` and added to `military`, consume grain at a higher per-capita rate, and do not reproduce. Standing armies are expensive in exactly the way that matters: they suppress population growth and raise the food demand that drives famine.

Military moves via the same advection machinery as population, with the attractiveness field replaced by a **military objective field** derived from the agent's target selection (§19.2): contested border cells, rival depots, and rival capitals.

### 16.2 Combat resolution

In any cell where two kingdoms both have `military > 0`, apply **Lanchester's square law** in discretized form:

```
ΔA = −β · B · terrain_def_B · fort_B
ΔB = −α · A · terrain_def_A · fort_A
```

The square law's conserved quantity is `α A² − β B² = const`, which means **effectiveness scales with the square of concentration**. Consequences, all emergent:

- Splitting forces is severely punished; a kingdom fighting on two fronts loses disproportionately.
- Defensive multipliers (walls, hills, rivers) are worth much more than their linear cost suggests.
- There is a real first-mover advantage in concentration, which makes coalition timing (§18.4) genuinely strategic.

### 16.3 Siege

Combat is not the primary way territory changes hands. **Supply interdiction is.** Military presence in a cell raises the logistics cost multiplier for rival kingdoms (`hostile_claim` term in §12.2). Enough presence along a corridor pushes the region beyond `supply_max_cost`, which:

1. Fails condition 3 for every structure there → regional cascade.
2. Cuts food delivery → *K* drops → population falls below *A* → the region depopulates.

**Sieges therefore kill through the Allee threshold, not through damage.** This is the clearest example of P1 in the design: "siege" is not a mechanic anyone wrote; it is what happens when the logistics, cascade, and population systems interact.

### 16.4 Attrition and stalemate

To prevent endless border grinding, military in cells with no supply suffers desertion at `desert_rate` per tick. Offensives therefore have a natural clock, and deep strikes without road support are self-limiting.

---

## 17. Subsystem: Refugees and Strategy Transmission

### 17.1 Refugee field

Refugees are a single unaffiliated field (not per-kingdom), plus a carried genome and infected fraction.

```
∂R/∂t = D_R ∇²R − ∇·(R v_R) − m_R R − absorption + scatter_source
```

with `D_R ≈ 3 × D_pop` (refugees move faster than settled populations) and elevated mortality `m_R` (default 0.008/tick, a half-life of about 87 ticks). Refugee attractiveness `v_R` points toward food, away from war and disease, and **toward kingdoms whose reputation for accepting refugees is high** — reputation thus has a concrete mechanical effect beyond diplomacy bookkeeping.

### 17.2 Absorption

A kingdom with `alloc.openness > 0` absorbs refugees in its territory:

```
absorbed = min(R(cell), openness_k · absorb_rate · spare_capacity(cell))
pop_k += absorbed
```

Absorption is a real decision with three coupled consequences:

| Effect | Sign |
|---|---|
| Free population, immediately productive | **+** |
| Disease transmission risk ∝ `refugee_infected` | **−** |
| Genome blending toward the refugees' strategy | **?** |

### 17.3 The genome and strategy transmission

Each kingdom carries a fixed-length vector of behavioral parameters:

```cpp
struct Genome {
  fx16 aggression;        // weight on military allocation
  fx16 expansionism;      // weight on claim/territory
  fx16 sustainability;    // discount on extraction rate vs. regeneration
  fx16 openness;          // refugee acceptance baseline
  fx16 trustfulness;      // prior on treaty compliance
  fx16 vengefulness;      // reputation penalty applied after a defection
  fx16 risk_tolerance;    // fire/disease risk weighting
  fx16 centralization;    // preference for compact vs. sprawling territory
};                        // G = 8 genes
```

The genome enters the simulation in two ways:
- For **scripted policies**, it directly parameterizes behavior.
- For **learned policies**, it is appended to the observation vector and *biases* the policy — the network can learn to condition on it, and it acts as a slowly-varying context variable.

**Transmission.** On absorption, the host kingdom's genome blends toward the refugees':

```
genome_k ← (pop_k · genome_k + absorbed · refugee_genome) / (pop_k + absorbed)
```

This is spatial replicator dynamics. Successful strategies spread by two channels — conquest (the winner's genome occupies more territory) and **collapse-and-absorption** (a dead kingdom's ideas survive in its neighbors). The second channel is unusual and is one of the more novel things this project does: a strategy can lose every war and still propagate, provided its refugees are numerous and welcome.

Small mutation is applied on kingdom founding: `genome += N(0, σ_mut)` with σ_mut = 0.03, clamped. This keeps genetic diversity from collapsing and makes long runs evolutionarily nontrivial.

### 17.4 Reseeding

Refugees found a new kingdom when, in a connected region of unclaimed land:

1. Total refugee population ≥ `found_threshold` (set well above the Allee threshold *A* — default 3*A* × 25 cells).
2. The region is outside `found_min_dist` of any living capital.
3. A free kingdom slot exists.

The new kingdom inherits the local `refugee_genome` (plus mutation), starts with a Capital and a small grain stock, and enters as a full participant.

Because the Allee effect makes sub-threshold populations shrink, **most reseeding attempts fail** — refugees accumulate, fail to reach threshold, and die out. This is intended. Founding should be rare and notable. Expected rate: 0–3 successful foundings per 20,000-tick episode at default parameters, which is a tuning target checked in the metrics dashboard.

---

## 18. Game-Theoretic Layer

### 18.1 The core dilemma

The strategic center of the game is a **common-pool resource problem**. The forest regenerates; every kingdom can harvest it; over-harvest by anyone degrades it for everyone; and the private benefit of one extra unit of effort exceeds its social benefit.

We take this seriously enough to solve the simplified version analytically, because it gives us something rare in multi-agent RL: **ground truth to measure learned behavior against.**

For the symmetric model (derivation in Appendix B.1), with *n* kingdoms, logistic regeneration *r*, catchability *q*, carrying capacity *K*, price *p*, and effort cost *w*:

```
Nash effort:        E_N = (r/q) · (n/(n+1)) · (1 − w/(pqK))
Social optimum:     E_S = (r/2q) · (1 − w/(pqK))

Ratio:              E_N / E_S = 2n / (n + 1)
```

So Nash effort is **twice** the social optimum in the large-*n* limit, and exactly optimal at *n* = 1. This gives a concrete experimental question: **where do trained agents land on the interval between E_S and E_N?** Reporting that number, with error bars, across values of *n*, discount factor, and treaty availability, is the project's primary scientific output.

`src/game/cpr.cpp` implements `nash_effort(params, n)` and `social_optimum_effort(params)` directly, and the metrics dashboard plots realized aggregate extraction against both lines in real time.

### 18.2 Treaties

Treaties are lightweight, cheap to sign, and profitable to break.

```cpp
struct Treaty {
  int          a, b;
  TreatyKind   kind;         // NonAggression, BorderFix, ExtractionQuota, RefugeePassage, TradePact
  fx16         terms;        // quota level / border line parameter / etc.
  uint64_t     signed_tick;
  uint64_t     expires_tick;
};
```

Proposal and acceptance happen at macro steps via the agent's diplomacy action head (§19.2). A treaty is formed when both parties' proposals are compatible in the same macro step.

**Violation detection is imperfect and spatial**, which is the design's most important diplomatic choice. Kingdom *a* detects *b*'s violation with probability:

```
p_detect = base_detect + monitor_a · observation_coverage(violation_cell, a)
```

where coverage falls off with distance from *a*'s territory and structures. Consequences:

- Distant violations are nearly invisible; you can cheat far away with relative impunity.
- `alloc.monitoring` is a real resource expenditure competing with production.
- This makes the game a **repeated game with imperfect public monitoring**, where the folk theorem's cooperation results hold only under conditions we can vary and study.

### 18.3 Reputation

Per ordered pair (*a*, *b*), *a* maintains:

```
rep[a][b] ← rep[a][b] · (1 − forgiveness) + evidence
```

with `evidence` negative on detected violation, positive on observed compliance over time. `forgiveness` comes from the genome's `vengefulness` gene (inverted), so forgiveness is heritable and evolves.

Reputation gates treaty acceptance thresholds, refugee flow direction (§17.1), and trade volume. A kingdom with universally bad reputation is functionally isolated — which is a meaningful punishment, and one that emerges from the mechanics rather than a scripted penalty.

### 18.4 Coalitions

No formal coalition-formation protocol is implemented in v1 (that way lies scope creep). Instead, coalitions **emerge** from pairwise treaties: if *a* and *b* both hold non-aggression pacts and both hold hostile reputation toward *c*, their military objective fields will independently point at *c*, producing a de facto alliance.

For analysis, we compute — offline, in `analyze_main.cpp`, not in the tick path — the **Shapley values** of the coalition game over the final-score characteristic function, using Monte Carlo permutation sampling. This tells us whether emergent alliance patterns track marginal-contribution logic. Exact Shapley is 2^n; at *n* ≤ 8 exact is feasible and preferred, with sampling as the fallback above that.

### 18.5 Ostrom conditions

The treaty and monitoring system is deliberately structured so that Ostrom's design principles for durable commons governance are all *tunable knobs* rather than assumptions:

| Principle | Knob |
|---|---|
| Clearly defined boundaries | `claim_floor`, BorderFix treaties |
| Monitoring | `alloc.monitoring`, `base_detect` |
| Graduated sanctions | reputation decay rate, `vengefulness` |
| Conflict resolution | treaty renegotiation frequency |
| Low-cost enforcement | detection cost parameters |

Sweeping these and measuring the resulting extraction ratio against E_N/E_S is a designed experiment, described in §27 M8.

---

## 19. Agent Interface

### 19.1 The policy interface

Every controller — scripted, learned, or human — implements one interface. This uniformity is what makes league training, ablation, and human play all use the same code path.

```cpp
class IPolicy {
public:
  virtual ~IPolicy() = default;
  virtual Action decide(const Observation& obs, RngStream& rng) = 0;
  virtual void   on_episode_start(const EpisodeInfo&) {}
  virtual void   on_episode_end(const EpisodeResult&) {}
  virtual std::string name() const = 0;
};
```

### 19.2 Action space

An action is emitted every *K* = 16 ticks. It has three parts.

**(a) Allocation simplex** — 8 non-negative components summing to 1:

```cpp
struct Allocation {
  fx16 extraction_wood, extraction_stone, extraction_grain;
  fx16 construction;
  fx16 military;
  fx16 monitoring;
  fx16 openness;         // refugee acceptance
  fx16 reserve;          // stockpiling / treasury
};
```

**(b) Construction priorities** — a categorical distribution over the 13 structure types, used by the placer to decide what to build next when `construction` effort is available.

**(c) Discrete targets** — a small set of categorical choices:

| Head | Cardinality | Meaning |
|---|---|---|
| `expand_dir` | 9 | 8 compass directions + "consolidate" (no expansion) |
| `military_target` | *n* + 1 | which rival to pressure, or "defend" |
| `treaty_partner` | *n* | which rival to address diplomatically |
| `treaty_action` | 6 | propose NA / propose quota / propose border / accept / break / none |

Total action dimensionality: 8 continuous (simplex) + 13 continuous (simplex) + 4 categorical. This is small enough for stable policy-gradient learning and expressive enough for every strategy we care about.

**What the agent explicitly does not control**, and why:

| Not controlled | Handled by | Reason |
|---|---|---|
| Individual cell placement | `StructurePlacer` heuristic | 65,536-way action space is untrainable and uninteresting |
| Per-cell extraction targeting | Sector effort distribution | Same |
| Unit movement | Advection on objective fields | P5; micro is not the interesting decision |
| Repair prioritization | Integrity-ordered greedy | Tedium, not strategy |

### 19.3 Observation space

Partial observability is real: a kingdom sees its own state fully, and rivals only where its coverage reaches.

**Spatial input — 20 channels, two scales:**

Egocentric crop, 48×48 at full resolution centered on the capital, **plus** a 48×48 downsampled view of the entire map (average-pooled). Two scales at the same tensor shape lets one shared conv trunk process both, and gives the agent both tactical detail and strategic context.

| Ch | Content |
|---|---|
| 0 | Own population density |
| 1 | Own claim |
| 2 | Own military |
| 3 | Own supply accessibility (1 − normalized cost) |
| 4 | Own infected fraction |
| 5–7 | Rival population, claim, military — **summed over rivals, masked by coverage** |
| 8 | Refugee density |
| 9 | Forest density (TREE + 0.5·SAPLING) |
| 10 | Burning |
| 11 | Stone remaining |
| 12 | Fertility |
| 13 | Moisture |
| 14 | Elevation |
| 15 | Structure presence (own) |
| 16 | Structure presence (rival, masked) |
| 17 | Structure functional mask (own) |
| 18 | Observation coverage (what the agent can actually see) |
| 19 | Fire risk estimate (dryness × forest × 1/moisture) |

Channel 18 is important: the agent must know what it *doesn't* know, otherwise masked-to-zero rival channels are indistinguishable from genuinely empty territory.

**Scalar vector — ~60 dims:**

Own stocks (4), stock deltas (4), population total, military total, area, perimeter, mean distance to capital, component count, functional node fraction, node count by type (13), grain balance, treasury balance, upkeep breakdown (4), infected fraction, own genome (8), per-rival summary — reputation, relative size estimate, treaty status, last-known-hostility (4 × *n*), global dryness phase, normalized tick, episode fraction remaining.

**Normalization:** all channels scaled to roughly [−1, 1] using **fixed constants from config**, never running statistics. Running normalization is a hidden source of non-determinism and of train/eval mismatch in multi-agent settings; the constants are worth the small tuning cost.

---

## 20. Reinforcement Learning

### 20.1 Problem formulation

A partially observable stochastic game: `(n, S, {O_i}, {A_i}, P, {R_i}, γ)`. Since actions occur every *K* ticks with variable-outcome durations, it is formally a **semi-MDP**, and the macro action is an *option* in the options framework. Discounting between macro steps uses γ^K, with γ = 0.999 per tick → γ_macro ≈ 0.984 per decision. At 20,000 ticks, that is 1,250 macro steps per episode, with an effective horizon of roughly 60 macro steps — deliberately shorter than the imperial cycle, so that *some* long-horizon reasoning must be carried by the value function rather than raw discounting.

### 20.2 Why hierarchy is mandatory

Stated plainly, because this is the decision that determines whether the project succeeds: **a flat policy over cell-level actions in a 256² world will not learn.** The action space is astronomically large, rewards are delayed by thousands of ticks, and the credit assignment problem is hopeless.

The options formulation collapses this. The agent chooses *how to allocate effort*, and scripted controllers execute. The scripted layer is deterministic given the allocation, so the SMDP is well-defined, and the effective action space is ~25 continuous dimensions plus 4 small categoricals.

The corresponding risk — that the scripted layer is where all the intelligence lives, and the RL is decorative — is real and is managed by the ablation in §25.5: a fixed uniform allocation against a learned one must show a large, significant performance gap. If it does not, the scripted layer is too smart and must be dumbed down.

### 20.3 Algorithm

**MAPPO** (multi-agent PPO) with centralized training and decentralized execution.

- **Actor** (per agent, shared weights across agents by default): conv trunk over the 20×48×48 spatial input → 4 residual blocks, 64 channels → global average pool → concat with scalar vector → 2 FC layers of 512 → heads.
  - Allocation head: outputs Dirichlet concentration parameters α ∈ ℝ⁸₊ (softplus + 1). A Dirichlet is the natural distribution on a simplex, gives well-defined entropy for regularization, and avoids the pathologies of squashed Gaussians on constrained spaces.
  - Construction-priority head: same, 13 dims.
  - Four categorical heads with logits.
- **Critic** (centralized): same trunk architecture but takes the **full unmasked global state** plus all agents' scalar vectors. Used only in training. This is the CTDE property, and it substantially reduces value-estimate variance under non-stationarity.

Parameter sharing across agents is the default, with an agent-id embedding so policies can still differentiate. Independent networks are a config flag for experiments on strategy divergence.

**Hyperparameters (starting point):**

| Parameter | Value |
|---|---|
| Rollout length | 128 macro steps |
| Parallel envs | 32 |
| Epochs per update | 4 |
| Minibatches | 8 |
| Learning rate | 3e-4, linear decay |
| Clip ε | 0.2 |
| GAE λ | 0.95 |
| Entropy coefficient | 0.01, annealed to 0.001 |
| Value loss coefficient | 0.5 |
| Max grad norm | 0.5 |

### 20.4 Reward design

Reward design is where this project is most likely to go wrong, so it is specified conservatively.

**Primary (sparse, at episode end):**

```
R_terminal = w_survive · survived
           + w_score   · normalized_final_score
```

where score is a weighted sum of integrated population-ticks, territory held, and structures functional at end.

**Dense shaping (potential-based, and only potential-based):**

```
F(s, s') = γ_macro · Φ(s') − Φ(s)

Φ(s) = φ_pop  · log(1 + total_population)
     + φ_stock· log(1 + grain_stock / demand)
     + φ_node · functional_node_fraction
     + φ_area · log(1 + area)
```

**Potential-based shaping provably leaves the optimal policy unchanged** (Ng, Harada & Russell), so we can be aggressive with it without corrupting the objective. This is the reason no other form of dense reward is permitted in this project. Any proposal to add a raw bonus for, e.g., "buildings constructed" must be rejected on these grounds — it changes the optimum and produces agents that build monuments while their people starve.

The `log(1 + ·)` form matters: it makes marginal reward decrease with size, which prevents the runaway-leader dynamic where an early advantage produces overwhelming reward gradient.

**Anticipated reward-hacking modes**, to be watched for in the metrics dashboard:

| Exploit | Detection | Mitigation |
|---|---|---|
| Turtling — minimal territory, never risk anything | Area metric flatlines near zero | Score weight on territory; survival alone is insufficient |
| Population farming with zero infrastructure | Node fraction low, population high | Score requires functional structures |
| Suicide-scatter to boost a successor | Collapse events correlated with score | Score is per-kingdom-slot, not inherited by successors |
| Refugee vacuuming without absorbing cost | Openness high, disease low | Ensure `refugee_infected` transmission is actually calibrated |

### 20.5 Non-stationarity and the league

Independent learners in a shared environment face a moving target. Without countermeasures, self-play cycles indefinitely through rock-paper-scissors strategies.

**League training**, following the prioritized fictitious self-play approach:

1. Maintain a pool of policy snapshots, checkpointed every *M* updates.
2. For each episode, the learning agent's opponents are sampled from the pool with probability weighted toward opponents it currently loses to (prioritized fictitious self-play), plus a fixed 20% floor of uniform sampling to avoid forgetting.
3. Retain a permanent set of **scripted anchors** in the pool: `Random`, `Greedy` (myopic max-extraction), `Sustainer` (fixed extraction at E_S), `Expansionist` (max territory), `TitForTat` (treaty-compliant until defected against). These prevent the population from drifting into a degenerate niche where every strategy is only good against the others.
4. Track an **exploitability proxy**: periodically train a fresh best-response agent against the frozen current policy for a fixed budget and record its win rate. A decreasing exploitability proxy over training is the primary evidence of genuine progress, and it is far more informative than self-play win rate (which is 50% by construction).

### 20.6 Curriculum

Training directly on the full problem will fail. Stages, with promotion gated on a measured criterion:

| Stage | Setup | Promotion criterion |
|---|---|---|
| **C0** | *N*=64, 1 kingdom, no rivals, no fire, no disease. Survive. | > 90% survival to 5,000 ticks |
| **C1** | + fire and disease enabled | > 80% survival |
| **C2** | + 1 scripted rival, abundant resources | > 60% win rate vs. Greedy |
| **C3** | *N*=128, 2 rivals, scarce resources (the CPR dilemma bites) | Extraction ratio measured; beats Greedy |
| **C4** | + treaties and reputation enabled | Treaty formation rate > 0 and non-trivially conditioned |
| **C5** | *N*=256, 4 kingdoms, full league self-play | Exploitability proxy declining |

Grid size increases mid-curriculum, which is affordable because the conv trunk is fully convolutional up to the global pool, so it transfers across resolutions without reinitialization. Fixed-size egocentric crops mean the *actual* tensor shape never changes; only the downsampled global view's coverage does.

### 20.7 The Python bridge

```cpp
PYBIND11_MODULE(ashfall, m) {
  py::class_<VecWorld>(m, "VecWorld")
    .def(py::init<const Config&, int /*num_envs*/, uint64_t /*base_seed*/>())
    .def("reset",  &VecWorld::reset)                      // -> (obs, info)
    .def("step",   &VecWorld::step,                       // -> (obs, rew, term, trunc, info)
         py::call_guard<py::gil_scoped_release>())
    .def("state",  &VecWorld::global_state)               // centralized critic input
    .def("render_rgb", &VecWorld::render_rgb, py::arg("env") = 0);
}
```

Critical details:

- `VecWorld` runs *E* independent worlds, stepping them **in parallel across a thread pool with the GIL released**. This is where the throughput comes from.
- Observations are written directly into pre-allocated NumPy arrays (zero-copy via `py::array_t` over borrowed buffers). No per-step allocation.
- Auto-reset on episode termination, with the final observation preserved in `info` for correct bootstrapping.
- All fixed-point → float conversion happens here, at the boundary, exactly once.

---

## 21. Rendering

### 21.1 Projection

Standard 2:1 isometric ("dimetric") projection:

```
screen_x = origin_x + (x − y) · (TILE_W / 2)
screen_y = origin_y + (x + y) · (TILE_H / 2) − z · TILE_Z
```

Defaults: `TILE_W = 32`, `TILE_H = 16`, `TILE_Z = 8`.

Inverse projection, for mouse picking on the ground plane (z = 0):

```
u = (sx − origin_x) / (TILE_W / 2)
v = (sy − origin_y) / (TILE_H / 2)
x = (v + u) / 2
y = (v − u) / 2
```

Picking a cell when stacks have height requires testing candidate cells from front to back along the ray; since maximum stack height is bounded (~20 units), iterate at most 20 candidates and take the first hit. No general ray-caster needed.

### 21.2 Draw order

Painter's algorithm with sort key `x + y`, then `z` ascending within a cell. Because the grid is traversed in a fixed order, no explicit sort is required at all — iterating `for (d = 0; d < 2N-1; ++d) for each cell with x+y == d` produces correct back-to-front order directly. This is O(cells) with no comparison sorting, which is the main reason to use a strict isometric grid rather than free 3D.

### 21.3 What height encodes

Per P6, every vertical element is a state variable:

| Element | Height source |
|---|---|
| Ground block | `elevation` |
| Canopy | forest state and age (sapling short, mature tall) |
| Structure | type-specific model height × `progress`; leans/crumbles as `integrity` falls |
| Population column | translucent column, height ∝ `pop_k`, tinted by owner |
| Smoke plume | `smoke` layer, drawn as billboards with additive blending |
| Refugee marks | short desaturated columns, ∝ `refugee_pop` |

A collapsing kingdom is therefore visually unmistakable: columns shorten, structures tilt and go grey (non-functional), and a spreading wash of desaturated refugee marks flows outward.

### 21.4 Chunked caching

Redrawing 65,536 tile stacks per frame is wasteful when most of the map is static between frames.

- Divide the map into 32×32 chunks; each renders to its own `RenderTexture`.
- A chunk is marked dirty by the simulation via a per-chunk dirty flag set whenever any cell in it changes a *rendered* quantity. Systems set this flag; it is a render-only concern and lives outside `ashfall_sim` via a callback interface so P3 is preserved.
- Clean chunks blit their cached texture. Dirty chunks re-render.
- Overlays (claim, supply, fire risk, disease) are drawn as separate translucent full-map layers with their own cache, toggled independently.

Expected: 100–500 dirty chunks per frame during active play out of 64 total at *N*=256 — i.e. most chunks dirty during a big fire, few during quiet periods. Budget assumes worst case (§23).

### 21.5 Camera and controls

- Pan (drag / WASD), zoom (discrete levels 0.5×, 1×, 2×, 4× — integer-ish scales keep the pixel art crisp), rotate in 90° steps by remapping (x, y) at draw time.
- Time controls: pause, 1×, 4×, 16×, 64×, and single-step. At 64× the renderer draws every 8th tick.
- Selection: click a cell to inspect all layers at that cell; click a structure for its dependency subgraph, drawn as a small node-link diagram with functional/failed coloring. **This inspector is the primary debugging tool for the cascade system** and should be built early.

### 21.6 HUD panels

| Panel | Contents |
|---|---|
| Kingdom bar | Per kingdom: population, area, stocks, functional fraction, alive/collapsed |
| Extraction plot | Aggregate extraction vs. E_N and E_S reference lines (§18.1) |
| Fire histogram | Live log-log CCDF of burn sizes with fitted τ |
| Cascade plot | Functional giant component vs. time, per kingdom |
| Event log | Fires, cascades, collapses, treaties, foundings, epidemics |
| Genome panel | Strategy-frequency stacked area over time |

The HUD is not decoration. In a system with this many coupled nonlinear parts, **the isometric view cannot be used to debug anything**; it tells you something dramatic happened, and the panels tell you what. Build the panels alongside each subsystem, not at the end.

---

## 22. Engineering Standards

### 22.1 Language and style

- C++20. Concepts and ranges permitted; coroutines and modules are not (compiler support is still uneven and the benefit is nil here).
- No exceptions in the simulation core. Errors are `std::expected`-style or assertions. The core must be usable from a `-fno-exceptions` build.
- No RTTI in the core.
- No dynamic allocation in the tick path. All buffers are sized at world construction. This is checked with a debug allocator hook that asserts on allocation between `tick_begin` and `tick_end`.
- Naming: `PascalCase` types, `snake_case` functions and variables, `trailing_underscore_` for private members, `SCREAMING_CASE` for constants.
- `clang-format` enforced in CI (config in repo). `clang-tidy` with a curated check set, warnings as errors.

### 22.2 Warnings and sanitizers

Build with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wold-style-cast -Werror`. `-Wconversion` is included specifically because implicit narrowing in fixed-point arithmetic is a likely and silent bug class.

CI matrix runs the test suite under: ASan+UBSan, TSan (on the parallel field update specifically), and MSan.

### 22.3 Threading model

```
Main thread
  ├─ Sequential phases (logistics, cascade, economy, diplomacy)
  └─ Parallel phases (forest, soil, population, epidemic, combat, claim, refugee)
       └─ Thread pool, static row-band partitioning
```

Rules:

1. Parallel phases partition the grid into **contiguous row bands**, one per worker. Bands are static (not work-stealing) so the partition is identical every run — though as established in §7.1, per-cell stateless RNG means even a dynamic schedule would be deterministic. Static bands are chosen for cache behavior, not determinism.
2. Each band reads front buffers (shared, const) and writes its own disjoint slice of the back buffer. **No synchronization inside a phase.** One barrier at the phase boundary.
3. Reductions (e.g. total population) use per-band partial accumulators combined in **fixed band order**, so fixed-point accumulation is bit-identical regardless of completion order.
4. The thread pool is created once per `World` and reused. No per-tick thread creation.

Row bands must be at least 8 rows tall to avoid false sharing at band boundaries; at *N* = 256 that caps useful parallelism around 32 threads, far more than needed.

### 22.4 Error handling and assertions

Three assertion tiers:

| Macro | Enabled in | Use |
|---|---|---|
| `ASHFALL_ASSERT` | Debug only | Internal invariants, hot paths |
| `ASHFALL_CHECK` | All builds | Invariants whose violation corrupts a run (buffer aliasing, NaN-equivalent fixed-point states, negative population) |
| `ASHFALL_CONFIG_ASSERT` | Config load | Parameter validation with a human-readable message |

Negative population, negative stock, and claim outside [0,1] are `ASHFALL_CHECK` conditions. They should be impossible; if they occur, the run is invalid and must fail loudly rather than produce plausible-looking garbage for another 40,000 ticks.

### 22.5 Configuration

All tunable parameters live in TOML, loaded into a `Config` struct at startup, validated, and then **immutable for the run**. The config's canonical serialization is hashed into the replay header, so a replay that was produced under different parameters is detected immediately rather than silently diverging.

No magic numbers in source. A number that appears in a formula either comes from `Config` or is a mathematical constant with a comment naming it.

---

## 23. Performance Budget

Targets at *N* = 256, *n* = 4, single thread, release build, on a modern desktop core.

| Phase | Budget | Notes |
|---|---|---|
| Forest (growth + lightning full pass) | 0.25 ms | 65k cells, branch-light |
| Forest (spread, active set) | 0.05 ms typical, 0.30 ms during large fire | Active set is small except during percolating events |
| Soil | 0.10 ms | |
| Logistics (4 × bucket-queue Dijkstra) | 1.40 ms | **Largest single cost.** 0.35 ms per kingdom |
| Structures | 0.05 ms | ~2,000 nodes |
| Cascade (incremental) | 0.05 ms typical, 2.00 ms during major cascade | Rare spikes are acceptable |
| Economy | 0.20 ms | Grid reductions |
| Population (4 kingdoms × diffusion+advection+growth) | 0.60 ms | |
| Epidemic | 0.30 ms | |
| Combat | 0.10 ms | Only contested cells |
| Claim | 0.40 ms | |
| Refugees | 0.15 ms | |
| Metrics | 0.05 ms | |
| **Total (typical)** | **≈ 1.4 ms** | ≈ 700 ticks/s |
| **Total (worst case)** | **≈ 6 ms** | Major fire + major cascade simultaneously |

**Throughput math for training.** At 700 ticks/s/thread and *K* = 16, one thread yields ~44 macro steps/s. With 32 parallel envs on 16 cores (2 envs/core), that is roughly 700 macro steps/s wall-clock. A 20,000-tick episode is 1,250 macro steps, so ~0.6 episodes/s, i.e. ~50,000 episodes/day. That is adequate but not generous, which drives two decisions:

1. **Training uses *N* = 128 by default**, roughly 4× cheaper, with *N* = 256 reserved for evaluation and demonstration. The fully-convolutional trunk and fixed-size egocentric crops make this transfer clean.
2. **Logistics is the first optimization target** if throughput becomes binding. Options in order of preference: recompute flow fields every 4 ticks instead of every tick (supply geometry changes slowly, and this is a 4× saving on the dominant cost); restrict Dijkstra to the kingdom's territory plus a margin; SIMD the relaxation inner loop.

### 23.1 Profiling

`core/profile.hpp` provides scoped timers writing into a per-tick ring buffer, aggregated into a rolling histogram per phase, dumped on demand and displayed in the HUD. Overhead when disabled is zero (compiled out). Microbenchmarks for the hot kernels (diffusion step, fire pass, Dijkstra relaxation) live in `tests/bench/` using nanobench and run in CI with regression thresholds at +15%.

---

## 24. Serialization, Replay, and Tooling

### 24.1 Snapshot format

Binary, versioned, little-endian, no padding:

```
Header:
  magic       "ASHF"                    4 B
  version     uint32                    4 B
  config_hash uint64 (XXH3 of canonical TOML)
  world_seed  uint64
  tick        uint64
  N, n        uint16, uint16
Body:
  world layers      (raw, in fixed declaration order)
  kingdom layers    (raw, per kingdom, in id order)
  kingdom structs   (POD, packed)
  node array        (POD) + CSR edge array
  treaty array
  rng bank state    (only the sequential streams; cell RNG is stateless)
Footer:
  state_digest uint64
```

Compressed with LZ4 for storage. A snapshot at *N* = 256 is ~8 MB raw, ~1.5 MB compressed.

### 24.2 Replay format

```
Header (as above, at tick 0)
Initial snapshot
Repeated:
  ActionRecord { tick: uint32, kingdom: uint8, action: packed Action }
  DigestRecord { tick: uint32, digest: uint64 }        every H ticks
```

Replays are tiny (a 20,000-tick, 4-kingdom episode is ~1,250 × 4 actions ≈ 200 KB) and are the primary artifact for debugging, analysis, and demonstration. **Every training episode that ends in an anomalous state should have its replay retained automatically.**

### 24.3 Tools

| Tool | Purpose |
|---|---|
| `ashfall_headless --config X --seed S --ticks T --out run.replay` | Batch runs |
| `ashfall_headless --sweep param=a:b:n --seeds K` | Parameter sweeps for validation |
| `ashfall_replay run.replay [--verify] [--from-tick T]` | Replay, with digest verification, jump-to-tick |
| `ashfall_analyze run.replay --metric burn_sizes --fit powerlaw` | Offline statistics: MLE exponent fits, transition curves, Shapley values |
| `ashfall_viewer --replay run.replay` | Watch a replay with full HUD and inspector |

`--verify` re-simulates from the initial snapshot and compares against the stored digests, reporting the first divergent tick. This is the workhorse for determinism regressions.

---

## 25. Testing and Validation

Five tiers, each with a distinct purpose. The statistical tier is what distinguishes this project from a hobby simulation.

### 25.1 Unit tests

Standard. Fixed-point arithmetic (including overflow boundaries and the lookup-table transcendentals against `double` references), grid index math, projection round-trips, bucket queue correctness against a reference Dijkstra, CSR graph construction, config validation rejecting out-of-range parameters.

### 25.2 Property tests

These encode the invariants that make the architecture sound.

| Property | Test |
|---|---|
| **Confluence** | Run a parallel phase with 1, 2, 4, 8 threads and with reversed band order; assert identical output digests |
| **Buffer discipline** | Debug build traps reads of the back buffer during a phase |
| **Mass conservation** | With growth and mortality disabled, total population is conserved by diffusion + advection to within fixed-point rounding (bounded by cells × 2⁻¹⁶) |
| **Refugee conservation** | Scatter + absorption + mortality accounts for exactly the scattered population |
| **Cascade order-independence** | Run cascade with shuffled worklist orders; assert identical functional sets |
| **Cascade monotonicity** | Adding pressure never increases the functional set |
| **No allocation in tick** | Allocator hook asserts zero allocations between `tick_begin`/`tick_end` |
| **Non-negativity** | Population, stocks, claim never go negative across a 10,000-tick fuzz run with random actions |

### 25.3 Statistical validation

Slow-tagged, run nightly rather than per-commit. Each writes results to `docs/VALIDATION.md`.

| Test | Assertion |
|---|---|
| **Forest SOC** | Burn-size CCDF is power-law over ≥2 decades; MLE τ ∈ [1.0, 1.4]; stable to ±0.1 under *N*: 128→256 |
| **Fisher–KPP** | Front speed within 10% of 2√(rD) with Allee disabled |
| **Allee ordering** | Front speed with Allee < Fisher speed; front retreats for *A* > *K*/2 |
| **Cascade transition** | *k*=2 shows a jump in giant-component fraction ≥ 0.25; *k*=1 shows none; mean cascade size peaks at ρ_c; hysteresis loop has nonzero area |
| **Epidemic threshold** | Attack rate is sigmoid in κ with a sharp rise; κ_c decreases with market count |
| **Tainter optimum** | A scripted expansionist agent's net yield peaks near the predicted *A*\* = (2b/3c)², within 20% |
| **CPR equilibrium** | Best-response dynamics among simple learners converge toward E_N, not E_S; the ratio approaches 2n/(n+1) as agents get more myopic |

The CPR test deserves emphasis: it validates that the *game* is the game we think it is. If simple best-responders do not converge to the analytic Nash effort, the implemented resource dynamics differ from the model in Appendix B.1, and the discrepancy must be found before any RL results mean anything.

### 25.4 Golden replays

A set of ~10 fixed (config, seed, scripted-policy) triples, each run 5,000 ticks, with digests committed to the repository. Any change to simulation logic breaks them, which is the point: the diff must then be reviewed and the goldens deliberately regenerated with a commit message explaining the behavioral change. This makes silent simulation drift impossible.

### 25.5 Ablation and sanity suite

Run before believing any RL result.

| Ablation | Expected outcome |
|---|---|
| Uniform fixed allocation vs. learned | Learned wins by a large margin — otherwise the scripted layer is doing the work (§20.2) |
| Learned vs. `Greedy` | Learned wins, and extracts less |
| Shuffled observations | Performance collapses to random — otherwise the agent is ignoring input |
| Zeroed rival channels | Performance degrades — otherwise the agent is not modeling rivals at all |
| Treaties disabled | Extraction ratio moves toward E_N |
| Monitoring cost → 0 | Cooperation increases (Ostrom prediction) |
| Discount γ lowered | Extraction moves toward E_N (folk-theorem prediction) |

The last two are not just sanity checks; they are the designed experiments the project exists to run.

---

## 26. Instrumentation and Metrics

Metrics are sampled every 16 ticks into a fixed-capacity ring buffer, and flushed to a columnar file (Parquet-like, or simply CSV in v1) at episode end.

**Per kingdom:** population, area, perimeter, mean distance to capital, component count, stocks (4), extraction rates (3), upkeep breakdown (4), functional node count and fraction, military, infected fraction, treaty count, mean reputation held, genome (8).

**Global:** forest coverage, burn events (id, size, duration), total extraction vs. E_N and E_S, refugee population, active epidemics, collapse events with cause attribution, foundings, treaty formation and violation counts, Gini coefficient over kingdom sizes.

**Derived, computed offline:** burn-size exponent τ, cascade-size distribution, empire lifetime distribution (is it heavy-tailed?), strategy frequency trajectories, extraction ratio vs. theoretical bounds, exploitability proxy over training.

**Empire lifetime distribution** deserves a note: it is not a designed target, but it is one of the most interesting things the simulation could produce. If lifetimes turn out heavy-tailed, that is a genuine emergent result worth reporting, and it costs nothing to measure.

---

## 27. Milestones

Each milestone has a concrete acceptance criterion. Do not proceed past one until its criterion is met — the failure mode for a project of this shape is building everything to 70% and being unable to debug any of it.

### M0 — Skeleton (week 1)
Build system, `Layer`, `fx16`, `RngBank`, config loading, digest, test harness, CI.
**Accept:** `ashfall_headless` runs 10,000 empty ticks; digest reproducible across thread counts and compilers; CI builds with no graphics libraries present.

### M1 — Forest and viewer (weeks 2–3)
Terrain generation, forest CA, isometric renderer with chunk cache, camera, cell inspector.
**Accept:** Forest reaches visible steady state after warmup. Burn-size CCDF is power-law over ≥2 decades with MLE τ ∈ [1.0, 1.4]. Renderer holds 60 fps at *N* = 256 during a large fire. **This milestone is the project's first real result and its first pretty picture; do not rush it.**

### M2 — Single kingdom (weeks 4–5)
Population field with Allee dynamics, claim, soil, basic extraction and stocks, one scripted policy.
**Accept:** Fisher–KPP wave speed within 10% of theory. Population reliably goes extinct when seeded below *A* and reliably grows when seeded above. Hysteresis demonstrated: a kingdom driven below *A* by temporary famine does not recover when famine ends.

### M3 — Structures and cascade (weeks 6–8)
Structure catalogue, placer, CSR dependency graph, logistics flow fields, incremental cascade, dependency-subgraph inspector.
**Accept:** The *k* = 2 vs. *k* = 1 transition plot is produced and shows a discontinuity ≥ 0.25 for *k* = 2 and none for *k* = 1. Hysteresis loop has nonzero area. The inspector correctly visualizes a live cascade. **This is the milestone the whole design rests on.**

### M4 — Collapse and refugees (weeks 9–10)
Pressure sources wired to the cascade, collapse detection, scatter, refugee field, absorption, genome blending, reseeding.
**Accept:** All five death causes (depletion, overextension, fire, epidemic, war) each observed as the attributed cause in at least 10% of runs across a seed sweep — i.e. no single cause dominates and none is unreachable. At least one successful refugee founding observed in a 20,000-tick run.

### M5 — Multi-kingdom and metrics (weeks 11–12)
Multiple kingdoms, combat, epidemic, five scripted policies, full metrics dashboard.
**Accept:** Round-robin tournament among scripted policies produces a non-transitive (rock-paper-scissors) result matrix — evidence the strategy space has real depth rather than a dominant strategy. Realized extraction plotted against E_N and E_S; `Greedy` sits near E_N and `Sustainer` near E_S.

### M6 — Diplomacy (week 13)
Treaties, imperfect spatial monitoring, reputation, refugee-flow coupling.
**Accept:** `TitForTat` outperforms `Greedy` in a mixed population, and the advantage disappears when monitoring is disabled — confirming the mechanism works through detection, not through some incidental effect.

### M7 — RL infrastructure (weeks 14–16)
pybind11 `VecWorld`, observation/action encoding, PettingZoo wrapper, MAPPO implementation, curriculum stages C0–C2.
**Accept:** Single agent clears C0 and C1 at criterion. Throughput ≥ 500 macro steps/s wall-clock at *N* = 128 with 32 envs. All §25.5 sanity ablations pass.

### M8 — League and experiments (weeks 17–20+)
League training, curriculum C3–C5, exploitability proxy, designed experiments.
**Accept:** Exploitability proxy declines over training. The headline measurement is produced: **realized extraction ratio relative to E_S and E_N, as a function of *n*, discount factor, monitoring cost, and treaty availability, with error bars over seeds.**

---

## 28. Risk Register

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | **Parameter coupling makes the system untunable** — every parameter affects every subsystem, and tuning becomes whack-a-mole | High | High | Tune each subsystem in isolation against its own validation target *first* (M1, M2, M3 order is chosen for this). Freeze validated parameters. Introduce couplings one at a time and re-run validation after each. |
| R2 | **RL fails to learn anything** beyond what the scripted layer already does | High | High | The §25.5 ablation is the detector. If uniform allocation matches learned allocation, dumb down the scripted placer/distributor until it doesn't. Curriculum exists precisely to catch this early at C0–C2 rather than at C5. |
| R3 | **Non-stationarity prevents convergence**; league cycles forever | Medium | Medium | Scripted anchors in the pool, prioritized fictitious self-play, exploitability proxy as the real progress metric rather than win rate. Accept "does not converge but is decreasingly exploitable" as a legitimate result. |
| R4 | **Throughput insufficient** for training | Medium | High | *N* = 128 for training; flow-field recomputation every 4 ticks; the §23 optimization ladder. Budget was computed before implementation specifically so this is caught by measurement, not surprise. |
| R5 | **Collapse is uninteresting** — either never happens or always happens | Medium | High | M4's acceptance criterion (all five causes present, none dominant) is exactly this check. The pressure scales are the tuning knobs. |
| R6 | **Reward hacking** produces degenerate but high-scoring agents | Medium | Medium | Potential-based shaping only (provably optimum-preserving). §20.4 exploit table monitored in the dashboard. Retain replays of anomalously high-scoring episodes for inspection. |
| R7 | **Determinism breaks** subtly under parallelism | Medium | High | Stateless per-cell RNG (§7.1) removes the largest class of causes structurally. Thread-count sweep and golden replays in CI catch the rest. |
| R8 | **Scope creep** — tech trees, named units, narrative, seasons, weather | High | Medium | §1.3 is the contract. Any addition must come with a validation target per P2, or it is rejected. |
| R9 | **Fire dominates everything**, making all other mechanics irrelevant | Medium | Medium | Fire damage to structures is a single scaled parameter; sweep it in M4 and require balanced cause attribution. |
| R10 | **The isometric view becomes a maintenance burden** disproportionate to its value | Low | Medium | P3 keeps it fully decoupled; it can be frozen at any time without affecting the research path. |

---

## Appendix A: Parameter Reference

Baseline values. `config/default.toml`.

### A.1 World
| Key | Default | Notes |
|---|---|---|
| `world.size` | 256 | 128 for training |
| `world.kingdoms` | 4 | |
| `world.macro_interval` | 16 | *K* |
| `world.episode_ticks` | 20000 | |
| `world.digest_interval` | 64 | *H* |
| `world.forest_warmup` | 3000 | Ticks before kingdoms spawn |

### A.2 Forest
| Key | Default | Notes |
|---|---|---|
| `forest.p_growth` | 0.02 | |
| `forest.f_lightning` | 2e-5 | *f/p* = 1e-3 |
| `forest.base_spread` | 0.65 | |
| `forest.t_mature` | 40 | Sapling → tree |
| `forest.t_burn` | 1 | |
| `forest.t_ash` | 30 | |
| `forest.ash_fertility` | 0.15 | |
| `forest.dryness_period` | 2000 | Seasonal oscillator |
| `forest.dryness_amp` | 0.35 | |

### A.3 Population
| Key | Default | Constraint |
|---|---|---|
| `pop.r_growth` | 0.03 | |
| `pop.D_diffusion` | 0.08 | ≤ 0.25 (stability) |
| `pop.allee_A` | 0.12 | 0 < A < K |
| `pop.K_max` | 1.0 | |
| `pop.v_max` | 0.5 | ≤ 1.0 (CFL) |
| `pop.mu_advect` | 0.4 | |
| `pop.food_per_capita` | 0.05 | |
| `pop.famine_rate` | 0.02 | |

### A.4 Soil
| Key | Default |
|---|---|
| `soil.q_farm` | 0.004 |
| `soil.r_recover` | 2e-4 |
| `soil.fertility_max` | 1.0 |

### A.5 Claim
| Key | Default |
|---|---|
| `claim.c_gen` | 0.05 |
| `claim.c_diff` | 0.02 |
| `claim.c_decay` | 0.004 |
| `claim.c_contest` | 0.03 |
| `claim.floor` | 0.15 |
| `claim.dissolve_ticks` | 200 |

### A.6 Logistics and upkeep
| Key | Default |
|---|---|
| `log.supply_max_cost` | 900 |
| `log.road_mult` | 0.35 |
| `log.hostile_mult` | 2.0 |
| `up.c_area` | 0.002 |
| `up.c_dist` | 0.00006 |
| `up.c_bord` | 0.004 |
| `up.c_frag` | 0.01 |

### A.7 Structures and cascade
| Key | Default |
|---|---|
| `struct.integrity_min` | 0.3 |
| `struct.decay_unpaid` | 0.006 |
| `struct.p_fail_unpaid` | 0.01 |
| `struct.fire_damage` | 0.25 |
| `cascade.recovery_interval` | 8 |
| `cascade.default_k` | 2 |

### A.8 Collapse and refugees
| Key | Default |
|---|---|
| `collapse.giant_frac` | 0.25 |
| `collapse.persist` | 100 |
| `collapse.capital_grace` | 300 |
| `collapse.starve_persist` | 150 |
| `ref.scatter_survival` | 0.6 |
| `ref.D_diffusion` | 0.24 |
| `ref.mortality` | 0.008 |
| `ref.absorb_rate` | 0.05 |
| `ref.found_threshold` | 9.0 |
| `ref.found_min_dist` | 40 |
| `ref.sigma_mutation` | 0.03 |

### A.9 Epidemic
| Key | Default |
|---|---|
| `epi.beta` | 0.28 |
| `epi.gamma` | 0.06 |
| `epi.delta` | 0.012 |
| `epi.omega` | 0.001 |
| `epi.kappa_trade` | 0.15 |
| `epi.crowd_factor` | 0.8 |
| `epi.p_spontaneous` | 5e-7 |

### A.10 Combat and diplomacy
| Key | Default |
|---|---|
| `war.alpha` | 0.04 |
| `war.beta` | 0.04 |
| `war.desert_rate` | 0.015 |
| `war.wall_def_mult` | 2.2 |
| `dip.base_detect` | 0.05 |
| `dip.monitor_scale` | 0.6 |
| `dip.treaty_duration` | 2000 |

### A.11 RL
| Key | Default |
|---|---|
| `rl.gamma_tick` | 0.999 |
| `rl.crop_size` | 48 |
| `rl.w_survive` | 1.0 |
| `rl.w_score` | 2.0 |
| `rl.phi_pop` | 0.3 |
| `rl.phi_stock` | 0.1 |
| `rl.phi_node` | 0.2 |
| `rl.phi_area` | 0.1 |

---

## Appendix B: Analytical Results

### B.1 Common-pool resource equilibrium

**Setup.** A single renewable stock *S* with logistic regeneration. *n* symmetric kingdoms apply extraction efforts *e_i*, total *E* = Σ*e_i*. Harvest is proportional to effort and stock (Gordon–Schaefer): *h_i* = *q e_i S*. Resource price *p*, unit effort cost *w*.

**Stock at steady state.**
```
rS(1 − S/K) = qES
⇒ 1 − S/K = qE/r
⇒ S* = K(1 − qE/r)
```
(valid for *E* < *r*/*q*; beyond that the stock is driven to zero).

**Nash equilibrium.** Kingdom *i* maximizes π_i = *p q e_i S** − *w e_i*, treating *E*₋*ᵢ* as fixed:

```
π_i = p q e_i K(1 − q(e_i + E_{−i})/r) − w e_i

∂π_i/∂e_i = p q K(1 − qE/r) − p q e_i · (qK/r) − w = 0
```

Imposing symmetry, *e_i* = *E*/*n*:

```
p q K (1 − qE/r) − p q²K E /(n r) − w = 0
p q K [1 − (qE/r)(1 + 1/n)] = w
(qE/r)(1 + 1/n) = 1 − w/(pqK)
```

**⇒  E_N = (r/q) · (n/(n+1)) · (1 − w/(pqK))**

**Social optimum.** Maximize total rent Π = *pqEK*(1 − *qE*/*r*) − *wE*:

```
dΠ/dE = pqK − 2pq²KE/r − w = 0
2(qE/r) = 1 − w/(pqK)
```

**⇒  E_S = (r/2q) · (1 − w/(pqK))**

**Ratio.**
```
E_N / E_S = 2n / (n + 1)
```

Checks: at *n* = 1 the ratio is 1 (a sole owner internalizes everything); as *n* → ∞ it approaches 2 (Nash effort is double the optimum). Both limits are correct, which is reassuring about the algebra.

**Use.** `cpr.cpp` exposes both quantities. The dashboard plots realized aggregate extraction against them. The headline experimental result is where learned agents fall on this interval under varying conditions.

**Caveats to state honestly when reporting results.** The full simulation differs from this model in ways that matter: the forest is spatially structured rather than well-mixed, extraction is spatially heterogeneous, kingdoms are not symmetric, and regeneration is a CA rather than a logistic ODE. E_N and E_S are therefore **reference lines, not exact predictions.** Their value is as a fixed, theoretically-grounded scale against which to compare policies — not as a claim that the simulation should hit them exactly.

### B.2 Optimal empire size (Tainter)

Gross yield scales with area: *B*(*A*) = *bA*.

Administrative cost has a term proportional to *A* × mean distance to capital. For compact territory of area *A*, radius *R* ∝ √*A*, and mean distance ∝ *R* ∝ √*A*. So the dominant cost term is:

```
C(A) = c · A^{3/2}
```

Net return:
```
Π(A) = bA − cA^{3/2}
dΠ/dA = b − (3/2) c A^{1/2} = 0
A^{1/2} = 2b / (3c)
```

**⇒  A\* = (2b / 3c)²**, with **Π(A\*) = 4b³/(27c²)**

Two immediate consequences used in the design:

1. Expansion past *A*\* **reduces** net return. Unbounded expansion is self-terminating without any scripted penalty.
2. Roads reduce mean distance for a given area, which raises *A*\*. Infrastructure investment literally buys a larger sustainable empire — a clean, legible strategic trade-off with a closed-form justification.

Validation: run a scripted expansionist agent with fire, war, and disease disabled, sweep its expansion aggression, and plot net yield against realized area. The peak should sit within 20% of *A*\* computed from the configured *b* and *c*.

### B.3 Numerical stability bounds

**Diffusion (explicit Euler, 2D, five-point Laplacian):** stable iff
```
D Δt / Δx² ≤ 1/4
```
With Δ*x* = Δ*t* = 1, this gives **D ≤ 0.25**.

**Advection (first-order upwind, CFL):**
```
|v| Δt / Δx ≤ 1  ⇒  v_max ≤ 1
```

Both asserted at config load (§10.5). With *D* = 0.08 and *v_max* = 0.5, both have comfortable margin.

### B.4 Fisher–KPP front speed

For ∂*n*/∂*t* = *D*∇²*n* + *rn*(1 − *n*/*K*), a front invading the *n* = 0 state propagates asymptotically at

```
c = 2 √(rD)
```

At the defaults *r* = 0.03, *D* = 0.08: **c ≈ 0.098 cells/tick**, so a front crosses a 256-cell map in roughly 2,600 ticks. This sets the natural spatial timescale of the simulation and is why episodes are 20,000 ticks: long enough for roughly 7–8 map crossings, i.e. several complete rise-and-fall cycles.

With the strong Allee term, the front becomes *pushed* and slower; for sufficiently strong Allee effect (*A* > *K*/2 in the simplest treatment) the front reverses and the population retreats. Both behaviors are asserted in the M2 acceptance test.

### B.5 Lanchester square law

```
dA/dt = −β B,    dB/dt = −α A
⇒ α A² − β B² = const
```

Force effectiveness scales as the **square** of concentration. Two consequences the design relies on: dividing forces is severely punished (making two-front wars catastrophic, which drives coalition dynamics), and defensive multipliers on terrain and walls are worth much more than their linear cost implies.

### B.6 Bootstrap percolation / *k*-core

For threshold *k* ≥ 2 on a network, the *k*-core emerges through a **discontinuous (hybrid) transition**: as the fraction of retained nodes crosses a critical value, the giant *k*-core appears or vanishes with a finite jump, unlike ordinary percolation (*k* = 1), which is continuous.

We do not attempt to derive the critical point analytically for our specific dependency topology — it is neither random nor regular, and the derivation would not survive the first design change. Instead, ρ_c is **measured** by the sweep in §25.3, and the *qualitative* claims (jump present for *k* = 2, absent for *k* = 1; hysteresis on the reverse sweep; mean cascade size peaking at ρ_c) are what the test asserts. This is the right level of rigor: the theory tells us what shape to expect, and the measurement confirms our implementation produces it.

---

## Appendix C: File Formats

### C.1 Config (TOML)

```toml
[world]
size = 256
kingdoms = 4
macro_interval = 16
episode_ticks = 20000

[forest]
p_growth = 0.02
f_lightning = 2e-5
base_spread = 0.65

[pop]
r_growth = 0.03
D_diffusion = 0.08
allee_A = 0.12

# ... see Appendix A for the full schema
```

Unknown keys are a hard error, not a warning — a typo'd parameter that silently uses a default is a debugging nightmare in a system this coupled.

### C.2 Metrics (CSV)

One row per sample (every 16 ticks), wide format:

```
tick,k0_pop,k0_area,k0_wood,...,k3_genome_openness,global_forest_frac,global_extraction,...
```

Events go to a separate log with a discriminated-union schema:

```
tick,type,kingdom,x,y,magnitude,detail
1832,FIRE_END,-1,88,140,4127,"fire_id=209"
5104,CASCADE,2,-1,-1,63,"trigger=UNPAID"
5204,COLLAPSE,2,-1,-1,-1,"cause=OVEREXTENSION"
```

---

## Appendix D: Core API Sketch

```cpp
namespace ashfall {

// ---- Construction ------------------------------------------------------
class World {
public:
  World(const Config& cfg, uint64_t seed);

  // Advance exactly one tick. Actions are consumed only on macro boundaries.
  void tick();

  // Advance K ticks, applying the given actions at the boundary.
  // Returns per-kingdom rewards accumulated over the interval.
  StepResult step(std::span<const Action> actions);

  // ---- Observation -----------------------------------------------------
  void encode_observation(int kingdom, ObservationBuffer& out) const;
  void encode_global_state(GlobalStateBuffer& out) const;   // critic input

  // ---- Introspection (const, cheap) ------------------------------------
  uint64_t                current_tick() const;
  std::span<const Kingdom> kingdoms() const;
  const Layer<uint8_t>&   forest_state() const;
  const Layer<fx16>&      population(int k) const;
  const StructureStore&   structures() const;
  const MetricsFrame&     latest_metrics() const;

  // ---- Persistence -----------------------------------------------------
  uint64_t digest() const;
  void     save(std::ostream&) const;
  static World load(std::istream&);

  // ---- Render hookup (render clients only; no graphics types here) ------
  void set_dirty_callback(std::function<void(int chunk_x, int chunk_y)>);
};

// ---- Vectorized wrapper for training -----------------------------------
class VecWorld {
public:
  VecWorld(const Config&, int num_envs, uint64_t base_seed);
  void reset(ObsBatch& obs);
  void step(const ActionBatch& actions, ObsBatch& obs,
            RewardBatch& rew, DoneBatch& done, InfoBatch& info);
private:
  std::vector<World>  worlds_;
  ThreadPool          pool_;
};

} // namespace ashfall
```

---

## Change Log

| Version | Date | Change |
|---|---|---|
| 1.0 | — | Initial baseline for implementation |

**Amendment procedure.** Changes to §6 (tick schedule), §7 (determinism), §14 (collapse), or Appendix B (analytical results) require regenerating golden replays and re-running the full statistical validation suite. Record the measured results in `docs/VALIDATION.md` alongside the change.