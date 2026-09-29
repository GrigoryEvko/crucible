# Crucible

Adaptive ML runtime. It intercepts framework dispatch and builds content-addressed computation graphs. The compilation of execution plans is planned.

## How it works

Crucible intercepts the ATen dispatcher of PyTorch through `DispatchKey::Crucible`. The key reaches one unboxed recording kernel for each of the 3110 ATen operators of the fork, and a boxed fallback for every other operator. These run on every op: forward, backward and optimizer. Each one writes tensor metadata (168B/tensor), scalar arguments, schema identity and the dispatch-context bits of `op_flags` into lock-free SPSC ring buffers. The trace ring takes one 64-byte cache-line write per op. Each one also redispatches the op, so the backend runs the real kernel on real storage. Mock handles (`CrucibleTensorImpl`) with full tensor metadata, no real storage and no kernel launch are planned.

A background thread drains the ring, detects iteration boundaries from signatures of schema and shape hashes, constructs a bidirectional CSR dataflow graph, and builds a content-addressed Merkle DAG. Identical sub-computations produce identical hashes. The kernel reuse across models, runs and organizations that these hashes permit is planned, because no code compiles a kernel at this time. A sweep-line allocator computes a static memory plan from dataflow lifetimes. A check of each plan against the device memory, which makes OOM structurally impossible, is planned.

The design in `misc/FORGE.md` adds the submission of an `ExecutionPlan` at sync points, through a single doorbell write. An `ExecutionPlan` is a pre-composed vendor pushbuffer with typed `PatchPoint` slots for runtime-mutable scalars and `ChainEdge` semaphores that set the sequence of several plans. In that design, plans are content-addressed and reused across iterations. The per-step CPU cost on the warm path is then the patch writes plus the doorbell. No code builds or submits an `ExecutionPlan` at this time.

## Compilation pipeline

Each frontend has a **Vessel** adapter that intercepts tensor dispatch. The PyTorch Vessel is the only adapter at this time. Adapters for JAX and for a native Python, C++ or Rust API are planned. The PyTorch Vessel records each op and runs it eagerly, as the section above says. In the planned mode, the Vessel returns a `CrucibleTensorImpl` mock handle with full tensor metadata and no real storage. The program then produces graph structure: it launches no kernel, allocates no device memory and calls no vendor library. Captured ops accumulate in lock-free SPSC ring buffers, and a background thread folds them into a content-addressed Merkle DAG of `RegionNode`s. The design expresses that DAG in the tensor-level IR of Crucible, **IR001**. IR001 is vendor-neutral by construction: a graph of operator identities from the CKernel taxonomy, interned symbolic shape and stride algebra (`Expr`), and structural extensions (`BranchNode`, `LoopNode`) for dynamic and cyclic computation. These types exist. The background thread builds only `RegionNode`s at this time. The regions do not use `Expr`.

**Forge** is the planned vendor-agnostic optimizer that compiles IR001. The design in `misc/FORGE.md` has twelve phases, each with a hard wall-clock budget: canonicalization and analysis of IR001 (A–B), exhaustive rewriting (C), global fusion via DP and ILP (D), and the lowering to **IR002** at phase E. IR002 is a portable kernel-level DAG. Each `KernelNode` matches one of the kernel templates — GEMM, ATTENTION, NORM, REDUCE, COLLECTIVE, MOE_ROUTE, OPTIMIZER, and so on — commits a semantic layout, and pins a `NumericalRecipe`: a 16-byte interned record of accumulator dtype, reduction algorithm, rounding mode, scale policy, and one of four determinism tiers (UNORDERED, ORDERED, BITEXACT_TC, BITEXACT_STRICT). The recipe is the cross-vendor portability contract: the same IR002 kernel produces equivalent results on every supported chip because every backend realizes the same pinned algorithm rather than delegating to a vendor library whose behavior drifts across SDK versions. Phases F and G refine IR002 with concrete tile shapes and a content-addressed static memory plan. In the design, a cross-vendor numerics CI matrix enforces the contract on every merged change. No Forge phase and no compute backend exist at this time, so the matrix does not run. The code holds the `NumericalRecipe` record and the recipe registry in `include/crucible/`. It holds the IR001 operation kinds in `include/crucible/forge/`.

**Mimic** is Crucible's per-vendor backend framework. No kernel backend exists at this time. `include/crucible/mimic/` holds three parts that the backends will share:

- `Fence.h` lowers a fence to x86, Arm or GPU instructions
- `Semaphore.h` gives device semaphores over a host oracle
- `CogMimic.h` gives the projection of a Cog that the cache of compiled binaries uses as a key.

`include/crucible/mimic/_wip/` declares one network backend for each of six vendor families (cpu, nv, am, intel, mellanox, broadcom). None of them has an emit path.

This paragraph gives the design in `misc/MIMIC.md`. Forge's phase H will send each KernelNode, by `TargetCaps::vendor_id`, to the backend of its vendor: NVIDIA, AMD, TPU, Trainium, Cerebras or CPU. Each backend owns its **IR003\***: a machine IR specialized to the vendor's native ISA, with address-space resolution, register allocation, instruction scheduling, and peephole rewriting against a calibrated per-chip latency table. Mimic searches the kernel design space via MAP-Elites guided by a three-tier simulator (fast, medium, accurate; calibrated to 95–98% on Forge-emitted instruction streams), then emits the native binary format — cubin on NVIDIA, HSACO on AMD, TPU executable on Google, NEFF on Trainium, CSL on Cerebras, ELF on CPU. No vendor SDK runtime is linked: each backend ships its own runtime library wrapping kernel-driver ioctls directly, and its own collective library over the native fabric (NVLink, XGMI, ICI, NeuronLink, EFA). Compilation results land in a three-level content-addressed cache: L1 holds vendor-neutral IR002 snapshots and is federation-shareable across installations; L2 holds per-vendor IR003\* snapshots and is reusable across chips within a family; L3 holds compiled bytes per chip. Forge's remaining phases (I–L) assemble the per-kernel results into an `ExecutionPlan`, distribute it across the fleet, and continuously sample hardware counters to compare measured behavior against Mimic's predictions, triggering recalibration or recompilation when drift exceeds tolerance.

## Vessel: PyTorch integration

The PyTorch Vessel is a two-library bridge. `libcrucible_dispatch.so` registers `DispatchKey::Crucible` against a small PyTorch fork patch (one dispatch key, one TLS struct with mode/context/scope fields) and feeds extracted `TensorMeta` plus packed `op_flags` into Vigil's `dispatch_op`. `libcrucible_vessel.so` exposes the C lifecycle API (`create` / `destroy` / `flush` / `export`) against standalone Crucible headers. A Python controller (`crucible_native.py`) handles `DispatchKey::Crucible` enable/disable, module-scope tracking via forward hooks, and training-phase TLS. The planned adapters for JAX and for native Python, C++ and Rust frontends will follow the same pattern.

Python loads the two libraries from `build-release/lib`, or from the directory that `CRUCIBLE_BUILD_DIR` names. Build them with `cmake --preset release && cmake --build --preset release`. The default and tsan presets put ASan or ThreadSanitizer on every target. A library built that way loads only after its sanitizer runtime, so the controller refuses it and says how to rebuild it or preload the runtime.

## Build

C++26. **The patched GCC 16.2.1 is the only supported compiler** — Crucible's safety axioms structurally depend on contracts (P2900R14), reflection (P2996R13), erroneous behavior for uninit reads (P2795R5), and partial program correctness (P1494R5). All four are GCC 16 exclusive. Clang 22 cannot compile the codebase; no fallback is pursued.

The patched compiler is GCC 16.2.1 plus the fixes in `utils/toolchain/gcc/patches`. To build it, run `utils/toolchain/gcc/build.sh ~/.local/gcc16-patched`. The presets use that prefix when `CRUCIBLE_GCC16_PREFIX` and `CRUCIBLE_CXX` are not set. The configure step refuses a compiler that does not have each fix, and it tells you how to build one. CI builds the same compiler with the same script and caches it.

```bash
cmake --preset default && cmake --build --preset default
ctest --preset default          # full suite, parallel

cmake --preset release          # -O3 -march=native -DNDEBUG -g, contracts observe, no LTO
cmake --preset tsan             # ThreadSanitizer on every target
cmake --preset ubsan-strict     # full UBSan on every target, signed overflow undefined
cmake --preset verify           # Release code with every contract clause enforced
```

The build presets and the test presets run 192 jobs (`CMakePresets.json`). A `-j N` on the command line of `cmake --build --preset` or `ctest --preset` replaces that count. On a host with fewer cores or less memory, give a smaller N. The heaviest translation unit is `vessel/torch/register.cpp`, and `vessel/torch/CMakeLists.txt` gives its measured time and memory.

## Project layout

```
include/foundation/              Layer 1, namespace foundation: Platform.h macros, Pinned, Simd,
                                 saturated arithmetic
include/foundation/algebra/      Graded<Modality, Lattice, T>, the lattices and the modalities
include/foundation/contracts/    CRUCIBLE_PRE, CRUCIBLE_POST and the decide predicates
include/foundation/diag/         The diagnostic catalog, the fail-closed edges and the row hash
include/foundation/effects/      Effect atoms, effect rows, contexts, capabilities, Computation<R, T>
include/foundation/permissions/  Permission<Tag>, SharedPermission, ReadView, mint_permission_fork
include/foundation/reflect/      Reflection helpers: enum names, type hashes, signatures
include/fixy/                    Layer 2, namespace fixy: the safety wrappers (Linear, Refined,
                                 Tagged, Secret, Machine, Mutation, ScopedView) and the axis table
include/fixy/atoms/              The atoms, each of which relaxes one axis of a binding
include/fixy/concurrent/         SPSC and MPSC rings, permissioned channels, pipelines,
                                 ParallelismRule, Topology
include/fixy/fp/                 Reproducible floating point: one NaN pattern, written-out polynomials
include/fixy/handle/             Handles that a caller sets or publishes one time
include/fixy/os/                 Doors to the operating system: files, maps, sockets, threads, time
include/fixy/session/            Binary and multiparty session types, subtyping, crash-stop
include/crucible/                Layer 3, namespace crucible: the runtime.  Arena, TraceRing,
                                 MetaLog, TraceGraph, MerkleDag, Graph, ExprPool, CKernel, Philox,
                                 Serialize, ReplayEngine, PoolAllocator, Vigil, RecipeRegistry
include/crucible/canopy/         Mesh membership and gossip: SWIM, HyParView, Plumtree, CRDTs
include/crucible/cipher/         The computation cache and its federation protocol
include/crucible/cntp/           The CNTP network transport: congestion control, pacing, FEC, AF_XDP
include/crucible/cog/            Hardware identity: NIC setup and audit, TargetCaps, latency tables
include/crucible/forge/          IR001 operation kinds and the network recipe table
include/crucible/ledger/         The hardware-capability ledger and its probes
include/crucible/mimic/          Fences, semaphores and the Cog projection of the backends
include/crucible/observe/        Metrics, health observations, histograms, synthetic probes
include/crucible/perf/           BPF sensors: scheduler, syscalls, lock contention, PMU samples
include/crucible/topology/       Discovery, Pingmesh, PTP, congestion telemetry
include/crucible/vis/            Trace visualization: block detection, Sugiyama layout, SVG
include/crucible/warden/         Deadline watchdog, hardening policy, quarantine, CPU topology
src/                             The .cpp files of the foundation, crucible and crucible_perf
                                 libraries
test/                            Tests of include/crucible and src, the constant-time tests (ct/),
                                 the layer fixtures (layer/) and the session oracle answers
test/fixy/ test/foundation/      Tests of layers 1 and 2, with negative-compile fixtures in neg/
test/*_neg/                      Negative-compile fixtures of layer 3, one directory per subject
test/fuzz/                       Property tests and boundary fuzz harnesses (test/fuzz/README.md)
utils/scripts/                   CI guards, their allowlists and rosters, and the generators
utils/tools/                     Hardware probe, row-hash witness, mutation runner, session oracle
utils/toolchain/                 Build script and patches of the patched GCC
vessel/torch/                    The PyTorch Vessel: the dispatch fallback and recording kernels,
                                 the C API, the Python controller
vessel/torch/examples/           Recording scripts.  Their traces go to traces/, which git ignores
patches/                         PyTorch fork patch
bench/                           Micro-benchmarks, with their baselines in bench/baselines/
examples/                        Worked bindings of fixy::fn
cmake/                           Toolchain file, patched-GCC probes, contract semantic, BPF build
crucible/data/                   Network recipe constraints in JSON.  No code reads this file
misc/                            Design specs: CRUCIBLE.md (runtime), FORGE.md (vendor-agnostic
                                 optimizer), MIMIC.md (per-vendor backends), and the mint inventory
papers/                          Whitepaper and yellowpaper, legacy documents that do not agree
                                 with the code
```

## Design principles

**Content-addressed computation.** Same ops + same shapes + same dtypes = same `ContentHash`. `KernelCache` keys on `(ContentHash, RowHash)`. The row hash is the effect row of the region. In the design, reuse is automatic across models, runs and organizations. No code puts a compiled kernel into the cache at this time.

**Observe before optimizing.** Record one iteration as graph structure while the ops run eagerly. The planned mock-tensor handles remove that eager execution and the vendor-library warmup. Compile from concrete shapes and dataflow. No symbolic tracing, no shape inference, no Python AST analysis.

**Two threads, spin only.** The foreground records each op with one cache-line write and one release store. `bench/baselines/record_leaf.json` holds the measured cost. The background drains the rings and builds the graph, the DAG and the memory plan. SPSC rings with acquire/release atomics. No mutexes, no condition variables, no OS waits on the hot path.

**Eight safety axioms.** InitSafe (NSDMI on every field), TypeSafe (strong types for all semantic values), NullSafe (`[[nodiscard]]`, span accessors), MemSafe (arena allocation, `= delete("reason")`), BorrowSafe (SPSC ownership protocol), ThreadSafe (acquire/release only), LeakSafe (arena bulk-free), DetSafe (DAG + plan + Philox = bit-identical replay).

**No training/inference distinction.** The compiled DAG is both. In the design, a deploy is a copy of the Cipher.

## License

MIT
