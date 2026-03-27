# Crucible

Adaptive ML runtime. Records framework dispatch, builds content-addressed computation graphs, compiles execution plans with formal safety guarantees.

**Status:** Research prototype. Recording pipeline, graph construction, Merkle DAG, memory planning, compiled replay with divergence recovery, and a 146-op kernel taxonomy are implemented. Shadow handles, CUDA graph replay, distributed execution, and model-aware optimization are designed but not built.

---

## Architecture

Crucible interposes on PyTorch's ATen dispatch layer (the *Vessel*) to record execution traces into a lock-free SPSC ring buffer. A background thread constructs a bidirectional dataflow graph, builds a content-addressed Merkle DAG, and compiles execution plans. Identical sub-computations produce identical content hashes — enabling kernel reuse across models, runs, and organizations.

The runtime operates in three phases: **recording** (intercept Vessel dispatch, write 64B entries to SPSC ring at 3–5ns/op), **analysis** (background thread drains ring, builds CSR graph, constructs Merkle DAG, computes static memory plan, prepares compiled execution), and **compiled** (Vessel interceptor returns pre-allocated shadow handles — no execution, no allocation, no redispatch).

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
| **Vessel** | Framework adapter (PyTorch ATen dispatch). Filled with user code, emptied by Crucible. |
| **Vigil** | The model as persistent entity: computation DAG, weights, accumulated knowledge. Outlives any hardware. |
| **Cipher** | Event-sourced persistent state (DAG chain, weight snapshots, KernelCache, RNG state, proof certificates). Survives hardware failure, enables deterministic replay and migration. |
| **Relay** | Compute node running a Crucible daemon. Mortal, replaceable. |
| **Keeper** | Per-Relay daemon: health monitoring, mesh participation, executes Augur's optimization recommendations. |
| **Canopy** | Masterless Keeper mesh. Gossip for discovery, Raft for consensus, CRDTs for metrics. No single point of failure. |
| **Longitude** | Startup calibration: GPU profiling, network probing, kernel calibration, Z3-optimal topology/parallelism/communication. Re-solves on topology change. |
| **Augur** | Continuous monitoring: digital twin (per-iteration prediction), drift detection, bottleneck diagnosis (compute/memory/communication/bubble/imbalance), model intelligence (Hessian, gradient health, effective rank, CKA, scaling laws). |
| **FX** | Formal verification: four layers — Z3 SMT (universal proofs), consteval (bounded model checking), P2996 reflection (structural completeness), type system (capability tokens, strong types, phantom tags). |

## Build

Requires C++26. Primary: Clang 22 + libc++ 22. Fallback: GCC 15 + libstdc++ 15. Bleeding-edge: GCC 16 for P2996 reflection.

```bash
cmake --preset default          # Clang 22 + libc++ (debug)
cmake --build --preset default -j$(nproc)
ctest --preset default

cmake --preset gcc              # GCC 15 fallback
cmake --preset gcc16            # GCC 16 (reflection, inplace_vector)
cmake --preset release          # -O3 -march=native -DNDEBUG
cmake --preset verify           # Z3 proof suite
cmake --preset tsan             # ThreadSanitizer
cmake --preset tidy             # clang-tidy
```

## Project Structure

```
include/crucible/          C++26 headers
  Platform.h               Macros, branch hints ([[likely]]/[[unlikely]]), CRUCIBLE_INLINE
  Types.h                  ScalarType (24 dtypes), DeviceType, Layout, 5 strong ID types, 6 strong hash types
  Effects.h                Capability tokens: fx::Alloc, fx::IO, fx::Block (zero-cost authorization)
  Arena.h                  Bump-pointer allocator (~2ns/alloc, 1MB blocks, zero fragmentation)
  TraceRing.h              SPSC ring buffer (64B entries, 2^16 capacity, alignas(64) atomics)
  MetaLog.h                Parallel SPSC for TensorMeta (144B entries, 2^20 capacity, zero-copy drain)
  IterationDetector.h      K=5 schema hash signature, two-match confirmation, ~1ns steady state
  BackgroundThread.h       Drains ring, builds TraceGraph, constructs Merkle DAG, computes MemoryPlan
  TraceGraph.h             Bidirectional CSR property graph (DATA_FLOW, ALIAS, CONTROL_FLOW, SCALAR_FLOW)
  MerkleDag.h              RegionNode, BranchNode (sorted arms, 5 guard kinds), LoopNode (64B),
                           KernelCache (lock-free open-addressing, CAS insert), content/merkle hashing
  Graph.h                  Mutable graph IR: 64B GraphNode (10 NodeKinds), 8B Inst (~50 micro-ops),
                           ComputeBody, FuseKind (6 classifications), DCE, CSE, toposort (Kahn's O(V+E))
  ExprPool.h               Swiss table interned symbolic expressions (32B Expr, wyhash, pointer equality)
  SymbolTable.h            Per-symbol metadata (kind, hint, range, flags)
  Ops.h                    55 symbolic ops for shape/dimension algebra
  CKernel.h                146 device-agnostic compute ops (Section 1: 1–99 frozen, Section 2: 100–146)
  CostModel.h              HardwareProfile (4-level memory hierarchy, per-dtype throughput),
                           KernelConfig (tile/pipeline/warp params), C1–C8 constraints, roofline model.
                           Presets: B200 (sm_100), H100 (sm_90), MI300X (gfx942), A100 (sm_80)
  Reflect.h                GCC 16 P2996 reflection: reflect_hash<T>, reflect_print<T>
  Philox.h                 Deterministic counter-based RNG (platform-independent, ~10 int ops)
  SwissTable.h             SIMD control bytes (AVX-512/AVX2/SSE2/NEON/portable)
  Cipher.h                 Serialization/deserialization for persistent state
  ReplayEngine.h           Compiled Tier 1 replay with graduated divergence detection
  CrucibleContext.h        Top-level context: mode transitions, recording/compiled dispatch
  PoolAllocator.h          Single-allocation memory pool (base_ptr + offset, ~2ns/alloc)

test/                      C++ tests
lean/Crucible/             Lean 4 formalization (39 modules, 1,331 theorems)
papers/
  whitepaper/              LaTeX source — system architecture, design, related work
  yellowpaper/             LaTeX source — formal specification (struct layouts, algorithms, proofs)
vessel/                    PyTorch Vessel adapter
verify/                    Z3 proof suite scaffolding (enhanced Z3 fork with CaDiCaL)
misc/                      Manifesto, notes, design documents
```

## Papers

[**Whitepaper**](papers/whitepaper/crucible-whitepaper.pdf) ([source](papers/whitepaper/)) — *A Content-Addressed Adaptive Runtime for Machine Learning.* System architecture, design principles, recording pipeline, compilation model, formal verification (FX), hardware adaptation (Longitude + Augur), distribution (Canopy + Cipher), model-aware optimization, implementation status, related work (XLA, TVM, Triton, TorchInductor, FlexFlow, Alpa, Megatron, DeepSpeed, seL4, CompCert), research roadmap.

[**Yellowpaper**](papers/yellowpaper/crucible-yellowpaper.pdf) ([source](papers/yellowpaper/)) — *Formal Specification of the Crucible Runtime.* Every data structure layout (byte-level), algorithm (step-by-step), protocol state machine, hash function (exact constants), and proof obligation (with Z3 encoding sketches). Chapters 1–5 complete (notation, types, arena, SPSC ring, tensor metadata). Chapters 6–20 in progress.

Rebuild from source: `cd papers && make whitepaper yellowpaper`

## Lean 4 Formalization

39 modules, 1,331 theorems. Core infrastructure has zero `sorry`; 11 remain in intelligence-layer modules.

Coverage: Arena (pairwise disjointness, alignment, waste bound, zero external fragmentation), MemoryPlan (sweep-line non-overlap, offset bounds), PoolAllocator (slot disjointness), SPSC ring (FIFO ordering, batch drain, capacity invariant), MetaLog (bulk read-after-write), IterationDetector (detection latency, false positive bound), TraceGraph (CSR consistency), Merkle DAG (collision probability, structural diff, replay completeness), Graph IR (DCE fixpoint convergence, topological sort validity), scheduling (Graham's bound, Brent's theorem, critical path optimality), roofline model (multi-level cache, wave quantization, correction factors), fusion (chain optimality, occupancy).

```bash
cd lean && lake build           # Lean 4.28.0 + Mathlib
```

## Roadmap

| Phase | Target | Status |
|-------|--------|--------|
| **1. Foundation** | TraceRing, MetaLog, TraceGraph, Merkle DAG (Region/Branch/LoopNode), MemoryPlan, PoolAllocator, compiled Tier 1 replay, divergence recovery, CKernel 146-op taxonomy, Graph IR (DCE, CSE, toposort), effects system, Lean 4 formalization | **Done** |
| **2. FX Core** | Z3 proof suites (arena, hashing, SPSC, memory plan, kernel access), consteval infrastructure (dual-mode Arena, Philox fuzzing, finite-state model checking), reflection structural checks (InitSafe, TypeSafe per struct), proof-carrying build | **Next** |
| **3. Longitude** | GPU profiling (GEMM/copy benchmarks, NVML), network probing (N*N latency/bandwidth), kernel calibration (correction factors), Z3 topology solver (TP*DP*PP*EP*CP, placement, communication algorithms, checkpointing, precision — all jointly) | Planned |
| **4. Augur** | Digital twin (DAG + cost model + corrections), per-iteration prediction, drift detection, bottleneck diagnosis, model intelligence (Hessian spectrum, gradient health, effective rank, CKA, convergence prediction, Chinchilla scaling) | Planned |
| **5. Compiled Tier 2–3** | Shadow handles (ConductorTensorImpl, ~2ns/op), batched kernel launch, CUDA Graph capture and replay (~50ns/iter) | Planned |
| **6. Distribution** | Canopy (gossip, Raft, CRDTs), Keepers (health, self-update), full Cipher (hot/warm/cold tiers, configurable alpha redundancy), heterogeneous compute (UCX multi-backend), DiLoCo enhancement (adaptive H, selective sync, compressed pseudo-gradients), 5D parallelism auto-tuning | Planned |
| **7. Intelligence** | Token merging/early exit/adaptive patching, attention head classification and replacement, architecture mutation (grow/prune/width), meta-gradients, per-layer LR from curvature, curriculum learning, automatic mixed precision, optimizer evolution. All as DAG branches: Augur diagnoses, FX verifies, Keeper activates. | Planned |

## Key Design Decisions

- **Content-addressed computation.** Same ops + same shapes + same dtypes = same ContentHash. KernelCache keys: `(ContentHash, device_capability)`. Cross-model, cross-run, cross-organization reuse is automatic.
- **Observe before optimizing.** Record one iteration eagerly, then compile from observations. No shape inference, no symbolic tracing, no Python AST analysis. Concrete execution only.
- **Formal verification where tractable.** Z3 for universal bitvector properties (alignment, hashing, protocol safety, kernel access bounds). consteval for bounded model checking (compiler catches UB). Reflection for structural completeness (every field initialized, no raw integers for IDs). Type system for zero-cost enforcement (capability tokens, strong types, phantom thread tags). Explicit boundary between proved, tested, and mitigated.
- **Two threads, spin only.** Foreground records at 3–5ns/op (one cache-line write + release store). Background drains, builds, compiles. SPSC rings with acquire/release atomics. No mutexes, no condition variables, no OS waits, no timeouts on the hot path.
- **No training/inference distinction.** The compiled Merkle DAG is both artifacts. Deploy = copy the Cipher to a Relay. Same kernels, same memory plans, same dispatch.
- **Eight safety axioms on every change.** InitSafe (NSDMI), TypeSafe (strong types), NullSafe ([[nodiscard]], span accessors), MemSafe (arena, = delete("reason")), BorrowSafe (SPSC ownership), ThreadSafe (acquire/release only), LeakSafe (arena bulk-free, unique_ptr), DetSafe (DAG + plan + Philox = bit-identical).

## License

Proprietary. All rights reserved.
