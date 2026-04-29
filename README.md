# Crucible

Adaptive ML runtime. Intercepts framework dispatch, builds content-addressed computation graphs, compiles execution plans.

## How it works

Crucible interposes on PyTorch's ATen dispatcher via `DispatchKey::Crucible` — a C++ boxed fallback that fires on every op (forward, backward, optimizer). The fallback extracts tensor metadata (168B/tensor), scalar arguments, schema identity, and 5 bits of dispatch context (mutability, training phase, inference mode) into a lock-free SPSC ring buffer at one 64-byte cache-line write per op. From iteration zero the user's program runs entirely against `CrucibleTensorImpl` mock handles: full tensor metadata, no real storage, no kernel launched, no vendor library invoked.

A background thread drains the ring, detects iteration boundaries from schema-hash signatures, constructs a bidirectional CSR dataflow graph, and builds a content-addressed Merkle DAG. Identical sub-computations produce identical hashes — enabling kernel reuse across models, runs, and organizations. A sweep-line allocator computes a static memory plan from dataflow lifetimes, making OOM structurally impossible.

At sync points Crucible submits an `ExecutionPlan` — a pre-composed vendor pushbuffer with typed `PatchPoint` slots for runtime-mutable scalars and `ChainEdge` semaphores for multi-plan sequencing — via a single doorbell write. Plans are content-addressed and reused across iterations; per-step CPU cost on the warm path is patch writes plus the doorbell.

## Compilation pipeline

Each frontend — PyTorch, JAX, or a native Python / C++ / Rust API — has a ~2K-LoC **Vessel** adapter that intercepts tensor dispatch and returns a `CrucibleTensorImpl` mock handle: full tensor metadata, no real storage. The user's program runs synchronously, producing graph structure rather than execution; no kernel is launched, no device memory is allocated, no vendor library is invoked. Captured ops accumulate in lock-free SPSC ring buffers, and a background thread folds them into a content-addressed Merkle DAG expressed in Crucible's tensor-level IR — **IR001**. IR001 is vendor-neutral by construction: a graph of operator identities from the CKernel taxonomy, interned symbolic shape and stride algebra (`Expr`), and structural extensions (`BranchNode`, `LoopNode`) for dynamic and cyclic computation.

**Forge** is the vendor-agnostic optimizer that compiles IR001. Twelve phases run within hard wall-clock budgets: canonicalization and analysis of IR001 (A–B), exhaustive rewriting (C), global fusion via DP and ILP (D), and the lowering to **IR002** at phase E. IR002 is a portable kernel-level DAG. Each `KernelNode` matches one of the kernel templates — GEMM, ATTENTION, NORM, REDUCE, COLLECTIVE, MOE_ROUTE, OPTIMIZER, and so on — commits a semantic layout, and pins a `NumericalRecipe`: a 16-byte interned record of accumulator dtype, reduction algorithm, rounding mode, scale policy, and one of four determinism tiers (UNORDERED, ORDERED, BITEXACT_TC, BITEXACT_STRICT). The recipe is the cross-vendor portability contract: the same IR002 kernel produces equivalent results on every supported chip because every backend realizes the same pinned algorithm rather than delegating to a vendor library whose behavior drifts across SDK versions. Phases F and G refine IR002 with concrete tile shapes and a content-addressed static memory plan; a cross-vendor numerics CI matrix enforces the contract on every merged change.

**Mimic** is Crucible's per-vendor backend framework. Forge's phase H dispatches each KernelNode by `TargetCaps::vendor_id` to one of `mimic/nv/`, `mimic/am/`, `mimic/tpu/`, `mimic/trn/`, `mimic/cer/`, or `mimic/cpu/`. Each backend owns its **IR003\***: a machine IR specialized to the vendor's native ISA, with address-space resolution, register allocation, instruction scheduling, and peephole rewriting against a calibrated per-chip latency table. Mimic searches the kernel design space via MAP-Elites guided by a three-tier simulator (fast, medium, accurate; calibrated to 95–98% on Forge-emitted instruction streams), then emits the native binary format — cubin on NVIDIA, HSACO on AMD, TPU executable on Google, NEFF on Trainium, CSL on Cerebras, ELF on CPU. No vendor SDK runtime is linked: each backend ships its own runtime library wrapping kernel-driver ioctls directly, and its own collective library over the native fabric (NVLink, XGMI, ICI, NeuronLink, EFA). Compilation results land in a three-level content-addressed cache: L1 holds vendor-neutral IR002 snapshots and is federation-shareable across installations; L2 holds per-vendor IR003\* snapshots and is reusable across chips within a family; L3 holds compiled bytes per chip. Forge's remaining phases (I–L) assemble the per-kernel results into an `ExecutionPlan`, distribute it across the fleet, and continuously sample hardware counters to compare measured behavior against Mimic's predictions, triggering recalibration or recompilation when drift exceeds tolerance.

## Vessel: PyTorch integration

The PyTorch Vessel is a two-library bridge. `libcrucible_dispatch.so` registers `DispatchKey::Crucible` against a small PyTorch fork patch (one dispatch key, one TLS struct with mode/context/scope fields) and feeds extracted `TensorMeta` plus packed `op_flags` into Vigil's `dispatch_op`. `libcrucible_vessel.so` exposes the C lifecycle API (`create` / `destroy` / `flush` / `export`) against standalone Crucible headers. A Python controller (`crucible_native.py`) handles `DispatchKey::Crucible` enable/disable, module-scope tracking via forward hooks, and training-phase TLS. JAX and native Python / C++ / Rust frontends follow the same adapter pattern.

## Build

C++26. **GCC 16.0.1 is the only supported compiler** — Crucible's safety axioms structurally depend on contracts (P2900R14), reflection (P2996R13), erroneous behavior for uninit reads (P2795R5), and partial program correctness (P1494R5). All four are GCC 16 exclusive. Clang 22 cannot compile the codebase; no fallback is pursued.

```bash
cmake --preset default && cmake --build --preset default -j8
ctest --preset default          # full suite, parallel

cmake --preset release          # -O3 -march=native -DNDEBUG -flto=auto
cmake --preset tsan             # ThreadSanitizer
cmake --preset verify           # + Z3 SMT solver
```

Build with `-j8` maximum — heaviest TUs peak ~1GB cc1plus RSS each (template + reflection + contracts); `-j$(nproc)` on multi-core boxes hits ~35GB and starts swapping.

## Project layout

```
include/crucible/        Tensor-level IR + runtime
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
  Ops.h                  Symbolic ops for shape/dimension algebra
  CKernel.h              Device-agnostic compute op taxonomy (frozen ordinals)
  CostModel.h            Hardware profiles, roofline model, kernel constraints
  SchemaTable.h          SchemaHash → op name (sorted, binary search)
  TraceLoader.h          .crtrace binary loader (auto-detects meta format)
  Philox.h               Deterministic counter-based RNG
  Serialize.h            CDAG serialization
  ReplayEngine.h         Graduated divergence detection, atomic plan swap
  PoolAllocator.h        base_ptr + offset allocation (~2ns)

  safety/                Linear, Refined, Tagged, Secret, Permission, Session,
                         Machine, Monotonic, AppendOnly, ScopedView, ConstantTime,
                         Checked, Mutation, FinalBy, NotInherited, … —
                         the 8-axiom enforcement layer
  algebra/               Graded<Modality, Lattice, T> substrate + lattice
                         catalog (Tolerance, RecipeFamily, NumaNode, Affinity,
                         BitsBudget, PeakBytes, Epoch, Generation, ProductLattice, …)
  sessions/              Type-state binary + MPST session types,
                         PermissionedSessionHandle (CSL × session integration)
  effects/               Met(X) effect rows, EffectRow, Computation<R, T>,
                         capability tags (Alloc / IO / Block / Bg / Init / Test)
  permissions/           Permission<Tag>, SharedPermission + pool, permission_fork
                         (Concurrent Separation Logic primitives)
  concurrent/            SPSC / MPSC / MPMC rings, ChaseLevDeque, AtomicSnapshot,
                         AdaptiveScheduler, NumaThreadPool
  handles/               Pinned, OwnedRegion, RAII handle wrappers
  bridges/               Cross-substrate adapters
  perf/                  Bench harness primitives
  rt/                    Realtime policy (SCHED_DEADLINE, mlock, isolcpus)

  vis/                   Trace visualization
    BlockDetector.h        Scope-based block grouping
    NetworkSimplex.h       Minimum-cost rank assignment
    SugiyamaLayout.h       Layered graph drawing
    SvgRenderer.h          Direct SVG emission with interactive JS

vessel/torch/
  crucible_fallback.cpp  C++ boxed fallback (DispatchKey::Crucible)
  vessel_api.cpp         C API to Vigil lifecycle
  crucible_native.py     Python controller (CrucibleNative context manager)
  crucible_mode.py       Legacy Python-only TorchDispatchMode path
  examples/              Recording scripts
    traces/              .crtrace output (gitignored)

patches/                 PyTorch fork patch
test/                    Positive tests + negative-compile fixtures
bench/                   Micro-benchmarks
misc/                    Current design specs: CRUCIBLE.md (runtime),
                         FORGE.md (vendor-agnostic optimizer), MIMIC.md (per-vendor backends)
papers/                  Whitepaper + yellowpaper (legacy artifacts, pending update)
```

## Design principles

**Content-addressed computation.** Same ops + same shapes + same dtypes = same `ContentHash`. `KernelCache` keys on `(ContentHash, device_capability)`. Reuse is automatic across models, runs, organizations.

**Observe before optimizing.** Capture one iteration as graph structure via mock-tensor handles — no eager execution, no vendor-library warmup. Compile from concrete shapes and dataflow. No symbolic tracing, no shape inference, no Python AST analysis.

**Two threads, spin only.** Foreground records at 3-5ns/op (one cache-line write + release store). Background drains, builds, compiles. SPSC rings with acquire/release atomics. No mutexes, no condition variables, no OS waits on the hot path.

**Eight safety axioms.** InitSafe (NSDMI on every field), TypeSafe (strong types for all semantic values), NullSafe (`[[nodiscard]]`, span accessors), MemSafe (arena allocation, `= delete("reason")`), BorrowSafe (SPSC ownership protocol), ThreadSafe (acquire/release only), LeakSafe (arena bulk-free), DetSafe (DAG + plan + Philox = bit-identical replay).

**No training/inference distinction.** The compiled DAG is both. Deploy = copy the Cipher.

## License

MIT
