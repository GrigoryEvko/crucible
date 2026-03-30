# Crucible

Adaptive ML runtime. Intercepts framework dispatch, builds content-addressed computation graphs, compiles execution plans with formal safety guarantees.

[Whitepaper](papers/whitepaper/crucible-whitepaper.pdf) — *A Content-Addressed Adaptive Runtime for Machine Learning.* Architecture, recording pipeline, compilation model, formal verification, hardware adaptation, distribution, model-aware optimization, related work.

[Yellowpaper](papers/yellowpaper/crucible-yellowpaper.pdf) — *Formal Specification of the Crucible Runtime.* Byte-level struct layouts, step-by-step algorithms, protocol state machines, hash constants, proof obligations with Z3 encodings.

---

## How it works

Crucible interposes on PyTorch's ATen dispatcher via `DispatchKey::Crucible` — a C++ boxed fallback that fires on every op (forward, backward, optimizer) at ~100ns overhead. The fallback extracts tensor metadata (168B/tensor), scalar arguments, schema identity, and 5 bits of dispatch context (mutability, training phase, inference mode) into a lock-free SPSC ring buffer at one 64-byte cache-line write per op.

A background thread drains the ring, detects iteration boundaries from schema-hash signatures, constructs a bidirectional CSR dataflow graph, and builds a content-addressed Merkle DAG. Identical sub-computations produce identical hashes — enabling kernel reuse across models, runs, and organizations. A sweep-line allocator computes a static memory plan from dataflow lifetimes, making OOM structurally impossible.

Three execution tiers: **recording** (intercept + eager redispatch, ~100ns/op), **compiled Tier 1** (guard check + pre-allocated shadow handles, ~2ns/op), **compiled Tier 2** (CUDA graph replay, ~50ns/iteration).

```
L7   Merkle DAG       content/merkle hashing, branches, guards, LoopNodes
L6   Graphs           bidirectional CSR, DFG/alias edges, iteration detection
L5   Tensors          shadow handles, 168B TensorMeta, latent observation
L4   Operations       DispatchKey::Crucible interception, op_flags, recording
L3   Memory           static plans from dataflow lifetimes, sweep-line allocation
L2   Kernels          SMT-optimal configuration, proved fusion, KernelCache
L1   Hardware         Relays, profiling, multi-vendor, health monitoring
L0   FX               Z3, consteval, reflection, type system
```

## Vessel: PyTorch integration

The Vessel is a two-library design that bridges PyTorch's dispatcher with Crucible's standalone runtime:

**libcrucible_dispatch.so** — C++ boxed fallback registered via `TORCH_LIBRARY_IMPL(_, Crucible, m)`. Links against the PyTorch fork (with `DispatchKey::Crucible` + `CrucibleState` TLS). Extracts `TensorMeta`, computes `SchemaHash`/`ShapeHash` via FNV-1a, caches `schema.is_mutable()` per-op in a thread-local parallel array, packs 5 `op_flags` bits, and feeds everything to Vigil's `dispatch_op()`.

**libcrucible_vessel.so** — C API to Vigil lifecycle (`create`/`destroy`/`flush`/`export`). Compiled against standalone Crucible headers only. Both libraries share Vigil through headers but get independent copies of `global_schema_table()` (inline static local per `.so`), bridged by Python before export.

**crucible_native.py** — Python controller. `CrucibleNative` context manager handles: library loading, `CrucibleState` TLS setup, `DispatchKey::Crucible` enable/disable, module scope tracking via forward hooks, training phase TLS, schema name bridging, `.crtrace` export.

The PyTorch fork patch is minimal (~200 lines across 5 files): one dispatch key, one TLS struct with mode/context/scope fields.

## Build

C++26. Primary: Clang 22 + libc++ 22. Fallback: GCC 15 + libstdc++ 15.

```bash
cmake --preset default && cmake --build --preset default -j$(nproc)
ctest --preset default          # 24 tests

cmake --preset gcc              # GCC 15
cmake --preset release          # -O3 -march=native -DNDEBUG
```

## Recording traces

```bash
cd vessel/torch/examples
PYTHONPATH=~/Downloads/pytorch:.. python3 record_resnet18.py
```

Each script sets `set_training_phase()` around forward/backward/optimizer to annotate ops with phase context. Traces land in `vessel/torch/examples/traces/`.

## Project layout

```
include/crucible/
  Platform.h             Macros, branch hints, CRUCIBLE_INLINE
  Types.h                ScalarType, DeviceType, Layout, strong ID/hash types
  Arena.h                Bump-pointer allocator (~2ns, 1MB blocks)
  TraceRing.h            SPSC ring (64B entries, op_flags 5-bit packed context)
  MetaLog.h              Parallel SPSC for TensorMeta (168B, zero-copy drain)
  IterationDetector.h    K=5 schema hash signature, two-match confirmation
  BackgroundThread.h     Ring drain → TraceGraph → Merkle DAG → MemoryPlan
  TraceGraph.h           Bidirectional CSR (DATA_FLOW, ALIAS edges)
  MerkleDag.h            RegionNode, BranchNode, LoopNode, KernelCache
  Graph.h                Mutable IR: 64B GraphNode, 8B Inst, DCE, CSE, toposort
  ExprPool.h             Swiss table interned expressions (wyhash, pointer equality)
  Ops.h                  55 symbolic ops for shape/dimension algebra
  CKernel.h              146 device-agnostic compute ops
  CostModel.h            Hardware profiles, roofline model, kernel constraints
  SchemaTable.h          SchemaHash → op name (sorted, binary search)
  TraceLoader.h          .crtrace binary loader (auto-detects meta format)
  Philox.h               Deterministic counter-based RNG
  Serialize.h            CDAG v7 serialization
  ReplayEngine.h         Compiled Tier 1 with graduated divergence detection
  PoolAllocator.h        base_ptr + offset allocation (~2ns)

include/crucible/vis/   Trace visualization
  BlockDetector.h        Scope-based block grouping
  NetworkSimplex.h       Minimum-cost rank assignment
  SugiyamaLayout.h       Layered graph drawing
  SvgRenderer.h          Direct SVG emission with interactive JS

vessel/torch/
  crucible_fallback.cpp  C++ boxed fallback (DispatchKey::Crucible)
  vessel_api.cpp         C API to Vigil lifecycle
  crucible_native.py     Python controller (CrucibleNative context manager)
  crucible_mode.py       Legacy Python-only TorchDispatchMode path
  examples/              Recording scripts (ResNet-18, ViT-B, SD 1.5, Llama 8B)
    traces/              .crtrace output (gitignored)

patches/                 PyTorch fork patch (~200 lines)
test/                    C++ tests (24)
bench/                   Micro-benchmarks
lean/Crucible/           Lean 4 formalization (39 modules, 1,331 theorems)
papers/
  whitepaper/            System architecture paper
  yellowpaper/           Formal specification
```

## Design principles

**Content-addressed computation.** Same ops + same shapes + same dtypes = same `ContentHash`. `KernelCache` keys on `(ContentHash, device_capability)`. Reuse is automatic across models, runs, organizations.

**Observe before optimizing.** Record one iteration eagerly, compile from observations. No symbolic tracing, no shape inference, no Python AST analysis.

**Two threads, spin only.** Foreground records at 3-5ns/op (one cache-line write + release store). Background drains, builds, compiles. SPSC rings with acquire/release atomics. No mutexes, no condition variables, no OS waits on the hot path.

**Eight safety axioms.** InitSafe (NSDMI on every field), TypeSafe (strong types for all semantic values), NullSafe (`[[nodiscard]]`, span accessors), MemSafe (arena allocation, `= delete("reason")`), BorrowSafe (SPSC ownership protocol), ThreadSafe (acquire/release only), LeakSafe (arena bulk-free), DetSafe (DAG + plan + Philox = bit-identical replay).

**No training/inference distinction.** The compiled DAG is both. Deploy = copy the Cipher.

## License

MIT
