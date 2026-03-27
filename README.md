# Crucible

Adaptive ML runtime. Records framework dispatch, builds content-addressed computation graphs, compiles execution plans with formal safety guarantees.

**Status:** Research prototype. Recording pipeline, graph construction, Merkle DAG, memory planning, compiled replay with divergence recovery, and a 146-op kernel taxonomy are implemented. Shadow handles, CUDA graph replay, distributed execution, and model-aware optimization are designed but not built.

---

## Architecture

Crucible interposes on PyTorch's ATen dispatch layer (the *Vessel*) to record execution traces into a lock-free SPSC ring buffer. A background thread constructs a bidirectional dataflow graph, builds a content-addressed Merkle DAG, and compiles execution plans. Identical sub-computations produce identical content hashes — enabling kernel reuse across models, runs, and organizations.

```
L16  Ecosystem        kernel genome, federated learning, hardware co-design
L15  Longitude+Augur  calibration, digital twin, monitoring, recommendations
────────────────────────────────────────────────────────────────────────────
L14  Lifecycle        Cipher persistence, reincarnation, deterministic replay
L13  Distribution     Canopy mesh, no master, RAID redundancy, DiLoCo, 5D parallelism
L12  Data             pipeline absorption, curriculum, latent augmentation
L11  Training         meta-gradients, Hessian, K-FAC, optimizer evolution
L10  Models           growing, pruning, width mutation, composition, live surgery
L9   Layers           attention replacement, local losses, per-layer gradient strategy
L8   Tokens           merging, early exit, adaptive patching, per-token precision
────────────────────────────────────────────────────────────────────────────
L7   Merkle DAG       content/merkle hashing, branches, guards, LoopNodes, atomic swaps
L6   Graphs           bidirectional CSR, DFG/alias edges, iteration detection
L5   Tensors          shadow handles, TensorMeta, latent space observation
L4   Operations       Vessel dispatch interception, recording, divergence detection
L3   Memory           static plans from dataflow lifetimes, sweep-line allocation
L2   Kernels          SMT-optimal configuration, proved fusion, KernelCache, Philox RNG
L1   Hardware         Relays, hardware profiling, multi-vendor, health monitoring
────────────────────────────────────────────────────────────────────────────
L0   FX               formal verification: Z3, consteval, reflection, type system
```

## Terminology

| Name | Role |
|------|------|
| **Vessel** | Framework adapter (PyTorch ATen dispatch layer) |
| **Vigil** | The model as persistent entity: DAG, weights, knowledge |
| **Cipher** | Event-sourced persistent state surviving hardware failure |
| **Relay** | Compute node running a Crucible daemon |
| **Keeper** | Per-Relay daemon: health, mesh, optimization execution |
| **Canopy** | Masterless Keeper mesh (gossip, Raft, CRDTs) |
| **Longitude** | Startup calibration: hardware measurement, Z3 topology optimization |
| **Augur** | Continuous monitoring: digital twin, drift detection, bottleneck diagnosis |
| **FX** | Formal verification layer: Z3 proofs, consteval, reflection, type system |

## Build

Requires C++26. Primary toolchain: Clang 22 + libc++ 22. Fallback: GCC 15 + libstdc++ 15.

```bash
cmake --preset default          # Clang 22 + libc++ (debug)
cmake --build --preset default -j$(nproc)
ctest --preset default

cmake --preset gcc              # GCC 15 fallback
cmake --preset release          # -O3 -march=native -DNDEBUG
cmake --preset verify           # Z3 proof suite
```

## Project Structure

```
include/crucible/     31 C++26 headers
  Platform.h          Macros, branch hints, CRUCIBLE_INLINE
  Types.h             ScalarType, DeviceType, Layout, strong ID/hash types
  Effects.h           Capability tokens (fx::Alloc, fx::IO, fx::Block)
  Arena.h             Bump-pointer allocator (~2ns, 1MB blocks)
  TraceRing.h         SPSC ring buffer (64B entries, 65536 capacity)
  MetaLog.h           Parallel SPSC for TensorMeta (144B entries, 1M capacity)
  IterationDetector.h K=5 signature matching, two-match confirmation
  BackgroundThread.h  Drains ring, builds TraceGraph, constructs DAG
  TraceGraph.h        Bidirectional CSR property graph (4 edge kinds)
  MerkleDag.h         RegionNode, BranchNode, LoopNode, KernelCache, Guards
  Graph.h             Mutable computation graph IR (64B GraphNode, 8B Inst)
  ExprPool.h          Swiss table interned symbolic expressions
  SymbolTable.h       Per-symbol metadata
  Ops.h               55 symbolic ops for shape/dimension expressions
  CKernel.h           146 device-agnostic compute ops (two-section taxonomy)
  CostModel.h         HardwareProfile, KernelConfig, roofline cost model
  Reflect.h           GCC 16 P2996 reflection (reflect_hash, reflect_print)
  Philox.h            Deterministic counter-based RNG
  ...

test/                 24 C++ tests
lean/Crucible/        39 Lean 4 modules, 1331 theorems
papers/
  whitepaper/         13 LaTeX chapters (25 pages)
  yellowpaper/        20 LaTeX chapters (formal specification, in progress)
vessel/               PyTorch adapter
verify/               Z3 proof suite scaffolding
```

## Lean 4 Formalization

39 modules, 1,331 theorems. Covers Arena, MemoryPlan, PoolAllocator, SPSC ring, MetaLog, IterationDetector, TraceGraph, Merkle DAG, Graph IR, scheduling, roofline model, and fusion. 11 `sorry` remain in intelligence-layer modules (attention, curriculum, fusion, Hessian, quantization, scaling, sparsity, token merging).

```bash
cd lean && lake build    # Lean 4.28.0 + Mathlib
```

## Papers

**Whitepaper** (25 pages). System architecture, design principles, key algorithms, implementation status, related work. Build: `cd papers && make whitepaper`

**Yellowpaper** (in progress). Formal specification — every struct layout, algorithm, protocol state machine, hash function, and proof obligation. Build: `cd papers && make yellowpaper`

## Roadmap

| Phase | Target | Status |
|-------|--------|--------|
| 1. Foundation | Recording pipeline, TraceGraph, Merkle DAG, MemoryPlan, compiled Tier 1 replay, CKernel taxonomy, Graph IR, effects system | **Done** |
| 2. FX Core | Z3 proof suites, consteval infrastructure, reflection structural checks, proof-carrying build | Next |
| 3. Longitude | GPU profiling, network probing, kernel calibration, Z3 topology solver | Planned |
| 4. Augur | Digital twin, drift detection, bottleneck diagnosis, model intelligence | Planned |
| 5. Compiled Tier 2-3 | Shadow handles (~2ns/op), CUDA Graph replay (~50ns/iter) | Planned |
| 6. Distribution | Canopy mesh, Keepers, full Cipher (three-tier persistence, redundancy) | Planned |
| 7. Intelligence | Token adaptation, layer analysis, architecture mutation, meta-gradients | Planned |

## Key Design Decisions

- **Content-addressed computation.** Same ops + same shapes + same dtypes = same hash. KernelCache keys: `(ContentHash, device_capability)`. Cross-model reuse is automatic.
- **Observe before optimizing.** Record one iteration eagerly, then compile from observations. No shape inference, no symbolic tracing.
- **Formal verification where tractable.** Z3 for universal properties (alignment, hashing, protocol safety). consteval for bounded model checking. Reflection for structural completeness. Type system for zero-cost API enforcement. Explicit boundary: what's proved vs tested vs mitigated.
- **Two threads, spin only.** Foreground records at 3-5ns/op. Background builds graphs and compiles. Communication: SPSC rings with acquire/release atomics. No mutexes, no condition variables, no OS waits on the hot path.
- **No training/inference distinction.** The compiled Merkle DAG is both artifacts. Deploy = copy the Cipher to a Relay.

## License

Proprietary. All rights reserved.
