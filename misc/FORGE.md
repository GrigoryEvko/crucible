# Forge — The Crucible ML Compiler

*The vendor-agnostic optimizer from tensor-level IR001 to portable kernel-level IR002. Hands IR002 to Mimic per-vendor backends for native ISA emission.*

Forge consumes Crucible's **Graph IR (IR001)** — the tensor-level DAG built from the Merkle-addressable region nodes of L7, with affine index expressions from the interned Expr symbolic engine and op identities from the 147-op CKernel taxonomy. Forge produces **IR002** — a portable kernel-level DAG where each kernel is a matched template (GEMM, ATTENTION, NORM, ...) with concrete tile shapes (or symbolic shapes with bounded ranges), committed layouts, and a pinned `NumericalRecipe` that dictates the algorithmic choices every downstream backend must honor exactly. IR002 crosses vendor lines; the same IR002 kernel produces numerically-equivalent results on any supported chip.

Forge has **zero vendor code**. No cubin emission, no SASS, no WGMMA, no MFMA, no MXU, no NEFF, no CSL — none of it in Forge. Everything below IR002 lives in Mimic per-vendor backends (`mimic/nv/`, `mimic/am/`, `mimic/tpu/`, `mimic/trn/`, `mimic/cer/`, `mimic/cpu/`), each of which owns its target's IR003*, ISA emitter, binary writer, three-tier simulator, MAP-Elites search, runtime library, and collective implementation — all written by us, all talking to the kernel driver directly, all independent of vendor SDKs.

This document specifies Forge. Mimic is documented separately in MIMIC.md; IR003* per-vendor details live there. The two together replace the entire LLVM + Inductor + XLA + ptxas + cicc + libcuda + cuBLAS + cuDNN + NCCL + UCX + libtpu + libnrt + rocBLAS + RCCL + hcoll + libsharp stack end-to-end with ~300-400K lines of C++26 we own forever.

This document is written in the voice of Crucible's CLAUDE.md: direct, opinionated, dense, without hedging.

---

## Contents

1. Thesis and scope
2. The Crucible context — what Forge inherits and what it produces
3. The three-tier IR — IR001, IR002, IR003*
4. Naming — Forge, Mimic, and the metalworking line
5. The 12-phase pipeline
6. Phase A — INGEST (IR001)
7. Phase B — ANALYZE (IR001)
8. Phase C — REWRITE (IR001)
9. Phase D — FUSE (IR001)
10. Phase E — LOWER_TO_KERNELS (IR001 → IR002, the layer boundary)
11. Phase F — TILE (IR002)
12. Phase G — MEMPLAN (IR002)
13. Phase H — COMPILE (delegate to Mimic per-vendor backend)
14. Phase I — SCHEDULE (IR002)
15. Phase J — EMIT (ExecutionPlan)
16. Phase K — DISTRIBUTE (IR002)
17. Phase L — VALIDATE (runtime)
18. IR002 specification
19. NumericalRecipe — the portability contract
20. Recipe registry and fleet intersection
21. Abstract TargetCaps
22. The Mimic integration surface
23. KernelCache — three-level content-addressed cache
24. ExecutionPlan and the COMPILED-mode runtime
25. Distribution and 5D parallelism as a compiler pass
26. The validate loop and continuous calibration
27. What Forge deliberately does not do
28. Head-to-head comparison
29. Size budget and build plan
30. Open questions deferred
31. Glossary

---

## 1. Thesis and scope

Six sentences:

1. **A tensor IR that preserves affine structure plus a content-addressable cache is enough to compile every ML workload that matters** — you do not need LLVM's general SSA + pass manager + TableGen backend to ship excellent ML kernels. Crucible's existing Graph IR (IR001) already has this. Forge is the optimizer that sits above it.
2. **A portable kernel IR (IR002) with pinned numerical recipes is enough to guarantee identical numerics across every vendor** — not "approximately equivalent," but bit-exact or ULP-bounded by construction, because every IR003* backend realizes the same pinned algorithm rather than delegating to a vendor library with undocumented drift.
3. **An analytical performance model calibrated to 95-98% accuracy replaces hardware autotuning in the inner loop** — but that model lives per-vendor in Mimic, not in Forge. Forge only queries `mimic::fast_cost` for cost-driven fusion and tile decisions, and `mimic::compile_kernel` for per-region search. Forge never simulates.
4. **Everything is content-addressed** — the Merkle hash of every region becomes a cache key at three levels: IR001→IR002 (portable cross-vendor), IR002→IR003* (per-vendor), IR003*→binary (per-chip). Cache hits dominate steady-state; compile latency is first-time cost, amortized across the fleet.
5. **Persistent multi-layer fused kernels dissolve the "per-op launch" bottleneck** that keeps existing compilers at 40-65% of peak GPU utilization on real workloads. Forge fuses as wide as abstract register + local-storage budget permits (expressed via `TargetCaps.fast_local_bytes`, not "Hopper 228KB smem"). The concrete realization is Mimic's problem.
6. **No vendor libraries, ever** — not cuBLAS, not NCCL, not libtpu, not libnrt, not hcoll. Crucible depends on the kernel driver ioctls, RDMA verbs, the Linux kernel, and the CPU. Every other vendor-proprietary piece is replaced by Mimic code we wrote ourselves.

Scope boundaries:

| Category | In scope | Out of scope |
|---|---|---|
| Hardware | NVIDIA sm_90+, AMD CDNA3/RDNA3 and later, Google TPU v5+, AWS Trainium/Inferentia, Cerebras WSE, x86_64/aarch64/riscv CPUs | Pre-sm_90 NVIDIA, Intel Xe/Gaudi for first two years, mobile GPU backends, graphics shader pipelines, compute shaders routed through graphics APIs |
| Language | Tensor IR (Crucible Graph + Inst + Expr + CKernel) | CUDA C++, OpenCL, SYCL, HIP C++, Triton, raw PTX, raw SASS, assembly |
| Workloads | Dense and sparse matrix ops, convolutions, reductions, elementwise, attention, MoE, state-space models, quantization kernels, collectives | Graphics (ROP, depth, tex filtering), media (NVDEC/NVENC), ray tracing, arbitrary HPC with irregular control flow |
| Numerical | FP64, FP32, TF32-like, BF16, FP16, FP8 (E3/E4/E5/E4M3/E5M2), FP6, FP4 with MX scaling, INT32/16/8/4, B1 | FP128, INT128, arbitrary-precision, posit, stochastic rounding above what hardware supports |
| Semantics | Affine loops, bounded trip counts, structured control flow, static tile shapes, symbolic tile ranges, explicit synchronization via abstract barriers | Recursion, general goto, exceptions, stack-based dynamic dispatch, runtime-variable trip counts beyond what symbolic Expr can capture |
| Vendor deps | Kernel driver (ioctls), RDMA verbs, Linux kernel, glibc, our own runtime libraries | cuBLAS, cuDNN, cuSPARSE, cuSOLVER, cuFFT, NCCL, RCCL, rocBLAS, MIOpen, libnrt, libncfw, libnccom, libtpu, xla-tpu, hcoll, libsharp, UCX, PJRT shims, vendor-supplied BLAS/DNN/collective libraries |

The scope is the leverage. Every out-of-scope item is ~10-500K lines of C++ that the traditional stack carries and Forge refuses to carry. Every vendor-library dependency is a version-drift liability we refuse to inherit. The fit-in-head compiler Crucible ships is about owning every line between user code and silicon.

## 2. The Crucible context

Forge does not exist in a vacuum. It is one layer in Crucible's 17-layer architecture. Reading CLAUDE.md is assumed; this section establishes only the interfaces Forge relies on.

**Crucible provides to Forge:**

- **L4 Vessel (mock-tensor dispatch capture)** — Crucible intercepts every op at the frontend's dispatch layer and returns a **mock tensor** (a `CrucibleTensorImpl` with metadata but no real storage) immediately. The frontend (PyTorch, JAX, or Crucible's native Python/C++/Rust frontend) never allocates device memory, never calls a vendor library, never executes a kernel during capture. The user's Python code "runs" synchronously producing only graph structure. Real execution happens when (a) the graph is complete (training step, inference call, explicit materialize), or (b) a sync point forces it (`tensor.item()`, `print(tensor)`, `if tensor > 0:`). Crucible compiles the accumulated graph via Forge + Mimic and runs a real kernel then. First iteration pays compile cost; subsequent iterations hit L3 cache. PyTorch's backend never runs; no cuBLAS, no cuDNN, no vendor library is ever called.
- **L6 TraceGraph** — the bidirectional CSR property graph built from one mock-tensor capture pass. Nodes are ops with SchemaHash and ShapeHash; edges are DATA_FLOW (mock-tensor identity tracking) and ALIAS (views, in-place mutation). Built in one pass in ~50-100μs for 1000 ops.
- **L7 Merkle DAG** — the content-addressed computation graph. `RegionNode` bundles compilable op sequences and carries `content_hash = hash(schema_hashes, input shapes/strides/dtypes/devices, scalar values)` and `merkle_hash = content_hash + children_hashes` for O(1) subtree equality. `BranchNode` handles dynamic behavior with guard predicates.
- **The Expr symbolic engine** — arena-allocated, interned, immutable expression nodes covering arithmetic, relational, logic, modular/floor/ceil division, mod, rounding, shifts, bitwise, where, min/max, trig, exp, log, power variants, bitcast, bitwise-and, bitwise-or, bitwise-xor, neg. 32 bytes per node. Structural equality = pointer equality. 13 assumption flags per node (is_integer, is_real, is_finite, is_positive, is_negative, is_nonnegative, is_nonpositive, is_zero, is_even, is_odd, is_number, is_symbol, is_boolean). Forge uses Expr for shape math, stride computations, affine loop bounds, and address calculations. A symbolic `tile.m * blockIdx.x + threadIdx.x * elements_per_thread + k` stays symbolic through Forge until it lowers to IMAD/IADD3 sequences at SASS emit time.
- **The CKernel taxonomy** — 146 op identities covering every ML op family (GEMM, conv, attention, normalization, activations, reductions, pooling, data movement, embedding, RNG, fused variants, linalg decompositions, SSMs, production inference ops like MoE, collective communication). CKernelId is a stable ordinal; the taxonomy is frozen in a way that new Vessel ops map to existing IDs or register as OPAQUE.
- **L3 Arena allocator** — bump-pointer, 2ns per allocation, no fragmentation, cache-line-aware. Forge allocates all IR002 nodes (KernelGraph, KernelNode, attrs pools, recipes, tiles), all intermediate data structures, all analysis results, all fused region descriptors from per-compile arenas that free instantly when the compile ends. Vendor binary bytes live in CompiledKernel handles owned by Mimic; Forge never touches them.
- **L3 PoolAllocator** — static memory plans for device memory, giving 2ns allocation per tensor "allocation" (pointer bump into a pre-allocated pool region acquired once per device via Mimic's per-vendor runtime library). Forge consumes memory plans as input to Phase G.
- **L2 KernelCache** — lock-free open-addressing hash table keyed on `(content_hash, device_capability) → CompiledKernel`. Forge populates this with cubin bytes + predicted-cycles + MAP-Elites archive for every region it compiles.
- **Effects system (fx::Alloc, fx::IO, fx::Block, fx::Bg, fx::Init, fx::Test)** — capability tokens enforced at compile time. Forge's phases run on the Crucible background thread, so every phase function signature takes `fx::Bg` and can freely alloc, IO, and block. Forge never runs on the foreground hot path; that is strictly the compiled shadow-handle dispatch.
- **L14 Cipher** — event-sourced persistent state. Forge's MAP-Elites archive, per-region CompiledKernel artifacts, calibration samples, and drift-detection residuals all persist in the Cipher and survive Relay reincarnation.
- **L15 Meridian** — hardware calibration at startup. Forge consumes `TargetCaps` populated by Meridian's microbenchmark suite (GEMM FLOPS, memory bandwidth, launch overhead, per-opcode latencies derived by Mimic's calibration harness).
- **L15 Augur** — continuous runtime monitoring. Augur calls `mimic::predict()` to compare predicted vs actual per-kernel runtime. Drift > 10% triggers Forge to invalidate and recompile affected cache entries.

**What Forge adds, that Crucible did not have:**

- **The IR001 → IR002 lowering (Phase E LOWER_TO_KERNELS)** — kernel template matching, recipe pinning, layout commitment. IR001 is an op DAG; IR002 is a kernel DAG with shapes and pinned numerics.
- **The middle-end optimization pipeline** — 12 phases, operating exclusively on IR001 and IR002. All vendor-specific concerns (IR003*, ISA emission, CSSA, address-space tagging, register allocation) live inside Mimic per-vendor backends and never cross the boundary into Forge.
- **The integration with Mimic for cost-driven decisions** — `mimic::fast_cost`, `mimic::propose_tiles`, `mimic::compile_kernel`, `mimic::predict`, `mimic::probe_counters`. These are the five entry points Forge uses. Mimic dispatches internally to the per-vendor backend matching `TargetCaps::vendor_id`.
- **The recipe registry and fleet intersection** — the global catalog of `NumericalRecipe`s with per-chip native-support bitmaps, queried at Phase E to pick recipes natively realizable across every member of the current fleet.

Crucible handles everything above the graph (tracing, Vessel dispatch, shadow handles, memory planning, distribution lifecycle). Forge handles IR001 optimization, IR001→IR002 lowering, and IR002 optimization; Mimic handles IR002→IR003*, ISA emission, simulation, search, runtime library, and per-vendor collective implementation. Crucible owns the runtime; Forge owns the portable optimizer; Mimic owns the per-vendor backends and the performance oracle. Three clean cuts.

## 3. The three-tier IR

```
Crucible Vessel (thin frontend adapter, ~2K LoC per framework)
        │
        ▼  ~5ns/op recording to TraceRing
Crucible TraceGraph (L6, CSR property graph)
        │
        ▼  per-iteration build + Merkle tree
Crucible MerkleDAG (L7, content-addressed regions)
        │
        ▼  cache miss triggers compile
┌─────── FORGE (vendor-agnostic optimizer) ─────────────────────────┐
│                                                                    │
│   IR001:  Crucible Graph + Inst + Expr + CKernel                   │
│           op DAG · symbolic shapes · 147 CKernelIds · pure          │
│           semantics · no tiles · no recipes · no vendor anything    │
│                                                                    │
│   ┌─ Phase A  INGEST             — specialize, canonicalize, CSE   │
│   ├─ Phase B  ANALYZE            — dom, liveness, cost, uniformity │
│   ├─ Phase C  REWRITE            — 10 sub-passes, bounded 3 rounds │
│   └─ Phase D  FUSE               — DP/ILP lattice, 7 FuseKinds     │
│                                                                    │
│   ════════ IR001 → IR002 layer boundary ══════════                 │
│                                                                    │
│   ┌─ Phase E  LOWER_TO_KERNELS    — match templates, pin recipe    │
│                                                                    │
│   IR002:  KernelGraph + KernelNode + NumericalRecipe + TileSpec    │
│           kernel DAG · 21 kernel families · concrete or ranged     │
│           shapes · pinned numerics · portable cross-vendor         │
│                                                                    │
│   ┌─ Phase F  TILE                — propose via mimic, pareto set  │
│   ├─ Phase G  MEMPLAN             — lifetime, alias, offset        │
│   ├─ Phase H  COMPILE             — delegate per-region to Mimic   │
│   ├─ Phase I  SCHEDULE            — stream, barriers, launch recs  │
│   ├─ Phase J  EMIT                — ExecutionPlan + Cipher commit  │
│   ├─ Phase K  DISTRIBUTE          — 5D parallelism auto-tune       │
│   └─ Phase L  VALIDATE            — runtime probe, drift, recompile│
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
        │  FusedKernelGraph (IR002) + abstract TargetCaps
        ▼
┌───── MIMIC per-vendor backend (vendor-specific, owned fully) ─────┐
│                                                                    │
│   IR002 → IR003* lowering:                                         │
│     CSSA, address-space resolution, register allocation, schedule   │
│                                                                    │
│   IR003*: vendor machine IR (one per backend)                      │
│     mimic/nv/  — Mercury SASS (Hopper, Blackwell, consumer, Thor)  │
│     mimic/am/  — AMDGPU ISA (CDNA3, RDNA3, later)                  │
│     mimic/tpu/ — TPU executable (v5p, v5e, v6e, v7)                │
│     mimic/trn/ — NeuronCore bytecode → NEFF (trn1, trn2, trn3)     │
│     mimic/cer/ — Cerebras wafer-scale CSL                          │
│     mimic/cpu/ — x86_64 / aarch64 / riscv (reference + fallback)   │
│                                                                    │
│   MAP-Elites search · 3-tier simulator · encoder · peephole        │
│   Runtime library (our replacement for libnrt / libcuda / libtpu)  │
│   Collective library (our replacement for NCCL / RCCL / libnccom)  │
│                                                                    │
│   → vendor binary (cubin / HSACO / NEFF / TPU-exec / CSL / ELF)    │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
        │  CompiledKernel {opaque_bytes, predicted_cycles, insights}
        ▼
Crucible L14 KernelCache + Cipher  (three-level content-addressed)
        │
        ▼  ~2ns/op shadow handle dispatch in COMPILED mode
Crucible L4 ExecutionPlan + per-vendor launch (CUDA graph / HIP graph / TPU plan)
        │
        ▼
Accelerator execution (via our runtime library, not vendor SDK)
        │
        ▼  ~1% sampling via mimic::probe_counters
Crucible L15 Augur — predicted vs measured
        │
        ▼  drift > 10% → invalidate L3 cache → Phase H recompile
```

The cardinal invariants:

- **The hot path never enters Forge**. Forge runs on Crucible's background thread exclusively. The foreground sees only the ExecutionPlan. Forge can take 300ms to compile because the foreground runs eagerly during compile and swaps atomically at iteration boundary.
- **Forge never touches IR003\***. Forge's source tree has zero `#include` of any vendor header. The only knowledge Forge has of a vendor is through the abstract `TargetCaps` struct (which carries an opaque `vendor_specific` pointer Mimic casts internally).
- **Mimic owns the per-vendor backend end-to-end**. Each backend has its own IR003*, emitter, simulator, runtime library, collective library, calibration harness. Adding a new vendor = new `mimic/<vendor>/` subdir + its `TargetCaps` factory. Zero Forge changes.
- **Numerics are pinned at IR002**. Same IR002 kernel produces ULP-bounded or bit-exact equivalent results on every backend, because every backend realizes the same `NumericalRecipe`. This is what makes mixed-vendor fleet training feasible.

## 4. Naming: Forge, Mimic, and the metalworking line

**Forge** — the compiler. A forge shapes material on an anvil. Forge takes Crucible's tensor DAG (raw material) and shapes it into the precise instruction sequences that GPU silicon wants. The compile loop is a shaping loop: every phase removes a degree of freedom, every pass adds constraint, until the final binary is a single point in a multi-dimensional design space.

**Mimic** — the simulator. A mimic imitates reality without being reality. Mimic imitates a GPU cycle-by-cycle (or interval-by-interval, at the fast tier) so the compiler can explore decisions without running binaries on actual hardware. Accuracy target 95-98% on the workload class Forge produces.

**Crucible** — the runtime. A crucible is where material is tested by fire: the ML runtime where models prove themselves against the hardware reality. Matches the existing name.

Three metalworking verbs, three sharp roles: the Crucible melts the raw models into a tensor DAG; the Forge shapes the DAG into kernels; the Mimic measures the shaping before it's committed to silicon. Renames are trivial if anyone objects, but I'll use these names consistently through the rest of the document.

Other roles from Crucible's existing ontology that interact with Forge:

- **Keeper** (per-Relay daemon) — calls into Forge when KernelCache misses during a compile window; runs Forge on the background thread.
- **Augur** (continuous monitoring) — calls `mimic::predict()` and compares to measured; triggers Forge recompile when drift exceeds threshold.
- **Cipher** (persistent state) — stores Forge's outputs (cubin bytes, MAP-Elites archives, calibration residuals) and replays them on reincarnation.
- **Meridian** (startup calibration) — populates `TargetCaps` that Forge and Mimic consume.
- **Vigil** (the model) — is the DAG Forge compiles.
- **Vessel** (PyTorch interception) — is upstream of Forge, never inside it.

## 5. The 12-phase pipeline

Total wall-clock budget on a 1000-region graph: **283ms worst case, 180ms typical, sub-10ms for cache-hit steady state**. Compare to Inductor seconds and XLA minutes.

| # | Phase | Operates on | Sub-passes | Budget | Iterates? | Opt-level gate |
|---|---|---|---|---|---|---|
| A | INGEST | IR001 | 7 | 8ms | No | Always |
| B | ANALYZE | IR001 | 8 | 20ms | Shape-eqs bounded 16 iters | Always |
| C | REWRITE | IR001 | 10 | 25ms | Max 3 rounds | Rounds 2+ gated at O1+ |
| D | FUSE | IR001 | 6 | 35ms | Single priority-queue pass | O1 single-output, O2+ multi-output, O3+ multi-layer |
| **E** | **LOWER_TO_KERNELS** | **IR001 → IR002** | **6** | **20ms** | **Once** | **Always** |
| F | TILE | IR002 | 5 | 40ms | Once, 64 candidates capped | O2 heuristic, O3 Z3 optimizer |
| G | MEMPLAN | IR002 | 6 | 15ms | Once | Always |
| H | COMPILE | delegates to Mimic | 2 (dispatch + collect) | 60ms per kernel, parallel across kernels | MAP-Elites bounded inside Mimic | Always |
| I | SCHEDULE | IR002 | 4 | 10ms | Once | Always |
| J | EMIT | ExecutionPlan | 4 | 25ms | Once | Always |
| K | DISTRIBUTE | IR002 | 4 | 15ms | Once | Skipped if single-device |
| L | VALIDATE | runtime measurements | 5 | 10ms per sample, 1% default rate | Continuous background | Always |

**Phase-ordering hard rules** (each has a structural justification):

1. B before C — rewrites need uniformity, dtype, and shape information.
2. C before D — never fuse dead or foldable ops. Fusion cost is non-trivial; clean first.
3. D before E — fusion decides which IR001 regions become one FusedRegion, and each FusedRegion becomes one (or a small number of) IR002 KernelNode(s). Lowering before fusion would force Phase E to undo work.
4. E before F — tile decisions live on IR002 KernelNodes; the mapping IR001→IR002 must exist first.
5. F before G — slot sizes known only after tiling commits to concrete or ranged dims.
6. G before H — Mimic needs the memory plan (slot layout, offsets) to know which inputs/outputs live where.
7. H parallelizable across KernelNodes. Mimic spawns one worker per kernel on a hardware-concurrency thread pool.
8. I before J — launch order affects the stream-annotated ExecutionPlan.
9. K runs between J and L if multi-device; single-device skips K entirely.
10. L is concurrent with the foreground runtime; it is continuous sampling not a gating phase.

**Budget enforcement**: every phase has a hard wall-clock kill. If a phase exceeds its budget by more than 1.5×, Forge aborts the compile attempt for that region, marks it OPAQUE, and emits a diagnostic. The runtime falls back to reference-eager from the Genesis Kernel Pack for that region, while the background thread retries compilation on the next opportunity. Pathological graphs cannot lock the background thread.

**Verification cadence**: phases B, D, E, F, G, H, J each run an IR verifier at their exit point. Verifier is cheap (O(nodes)) — no proof, just structural invariants: SSA form, typed connections, FuseKind legality, recipe validity, slot consistency, no forbidden operand combinations.

**Determinism requirement**: every phase is deterministic given its inputs and abstract `TargetCaps` signature. No hash-table-order iteration (sort first), no floating-point sums whose order depends on allocator state (canonical reduction order), no random number usage outside the explicitly-seeded Philox stream derived from `content_hash`. Same inputs → same IR002 → same IR003* from Mimic → same compiled bytes → same execution. Crucible-wide Axiom 8 DetSafe.

---

## 6. Phase A — INGEST

Purpose: take a RegionNode from Crucible's L7 Merkle DAG and produce a canonical, shape-specialized, fully-typed, CSE-cleaned IR001 subgraph ready for analysis. This phase does not change semantics; it only normalizes form.

Budget: **8ms** on a 1000-node graph.

### A.1 ShapeSpecialize

Walks the RegionNode's input tensors. For each `TensorMeta` (from the MetaLog parallel to the TraceRing), extracts concrete `sizes[ndim]` and `strides[ndim]` and substitutes them into the Expr nodes that reference symbolic shapes. Uses Expr's interning: `Expr::SYMBOL s_tile_m` becomes `Expr::INTEGER 128` if the trace recorded `blockDim.x = 128` and the shape equation is tight. Partial specialization is supported — if `N` is still dynamic, `M` is known 4096, and `K` is known 64, the resulting Expr tree has two concrete leaves and one symbol.

Output: an IR001 subgraph where every Expr that *can* be concrete is concrete, and the remaining symbols are the genuine dynamic dimensions. This drops the static-shape graph to fully-specialized in O(nodes), enabling A.3 and downstream passes to constant-fold.

Edge case: when shape specialization produces a value that violates an op's precondition (e.g., a reduction axis becomes `INTEGER 0`), the region is marked "degenerate" and emitted as a trivial kernel (no-op or a single MOV/STG sequence) skipping all further passes.

### A.2 DtypePropagate

Forward + backward dataflow over dtype. Forward propagates from input tensors; backward propagates from output tensors and from kernel-contract annotations (e.g., "this output is FP32"). Where forward and backward meet, insertions happen: if producer wants FP16 and consumer wants FP32, an explicit `cvt` IR001 node is inserted.

Promotion rules encode Crucible's graded-type discipline: anything that would cause loss of precision against the declared tensor type fails A.2 with a diagnostic; anything safe proceeds. Mixed-precision (producer FP16, consumer FP32 via FMA) is allowed because the FMA widens to FP32 in the accumulator naturally; this is a common pattern and A.2 recognizes it as an in-place promotion, not a materialized cast.

### A.3 AlgebraicSimplify

A small, finite rule table — targeted **40 rules**, no more. The rules are the ones that matter for ML:

| Category | Rules |
|---|---|
| Identities | `x+0 = x`, `x*1 = x`, `x/1 = x`, `x^1 = x`, `x*0 = 0`, `x^0 = 1`, `x-x = 0`, `x/x = 1 (x≠0)` |
| Transpose | `transpose(transpose(x)) = x`, `transpose(reshape(x)) = reshape(transpose(x)) when legal`, `matmul(x^T, y^T) = transpose(matmul(y, x))` |
| Commutativity normalization | `add/mul/and/or/xor args sorted by hash` |
| Dead broadcasts | `broadcast(x, same_shape) = x`, `sum(broadcast(x, d), d) = x * d.size` |
| Reduction composition | `sum(sum(x, a), b) = sum(x, a∪b)`, `max(max(x, a), b) = max(x, a∪b)` |
| Matrix fusion | `matmul(A, identity) = A`, `matmul(scale(A, c), B) = scale(matmul(A, B), c)` |
| Activation folding | `relu(relu(x)) = relu(x)`, `gelu(gelu(x)) ≠ gelu(x)` (not equal, don't fold), `sigmoid(−x) = 1 − sigmoid(x)` |
| Quantization | `quantize(dequantize(x)) = x when scales match`, `cast(cast(x, t), t) = cast(x, t)` |
| Memory | `view(contiguous(x)) = x when contiguous`, `gather(identity_indices, x) = x` |

Rules are applied in a single bottom-up pass using Expr's structural hash for pattern matching. Pattern matching is O(1) per rule per node — hash-cons the LHS, hash-match the subgraph. No rewriting-engine with priority; rules are disjoint and all fire when they apply.

**The discipline**: Algebraic rewrites run ONCE, as part of A.3. There is no "interleave simplification and constant folding in a fixpoint loop" — Crucible's IR is well-formed by construction, and the simplification rules are chosen to be confluent (they converge in one pass). This avoids the anti-pattern in Inductor and cicc where simplification passes run 5-10 times interleaved with other passes because the combined system has no local confluence guarantee.

### A.4 ConstantFold

For every Expr node whose operands are all `INTEGER`, `FLOAT`, `BOOL_TRUE`, or `BOOL_FALSE`, compute the result at compile time and replace the node with a constant. This extends across shape math (a stride expression `3 * 256 * 4` becomes `INTEGER 3072`), across constant tensor values when the region's inputs include compile-time constants (e.g., dequantization scales, positional encodings), and across integer identities after A.3 has normalized forms.

FP correctness: NaN and denormal handling follows IEEE 754 semantics strictly. The target FTZ flag is captured in TargetCaps and affects fold behavior (FTZ → denormals become zero, non-FTZ → preserved). This is important: cicc has a separate `OptimizeNaNOrZero` pass specifically to handle this, and we fold it into A.4 rather than carrying it as a separate phase.

Integer overflow: follows Crucible's graded type semantics (explicit wrap/saturate/trap). No undefined behavior.

### A.5 DeadCodeElim

Backward liveness from output tensors. Any node that does not reach an output is dead and is removed. Runs post-A.3/A.4 because those passes create dead nodes.

Memory effects: a node with a side-effect (an output tensor's store, an all-reduce, a scatter) is never dead. Pure ops with no live consumers are dead.

### A.6 CanonicalOrder

After commutativity normalization in A.3, apply topological sort with a canonical tiebreaker (hash-based). Identical graphs produce identical node orderings, which enables A.7's subgraph CSE to be O(n).

### A.7 CseSubgraph

Within the current RegionNode, hash each subgraph by its Merkle-style structural hash and merge structurally-identical subgraphs. This is cheap because Expr interning has already unified subtrees within the Expr namespace; A.7 extends the interning to the tensor-op level. Duplicate subgraphs are common after A.3 (e.g., two separate paths computing the same reduction).

**Output of Phase A**: a canonical, specialized, deduplicated, fully-typed IR001 subgraph. Node count typically shrinks 10-30% from input. Expression graph is fully interned. Ready for Phase B analysis.

## 7. Phase B — ANALYZE

Purpose: compute the analyses that downstream passes will consume. No IR mutation; only analysis results stored in side tables keyed by node ID. Budget: **20ms**.

### B.1 DominatorTree and PostDominatorTree

Lengauer-Tarjan in practice; for ML-shaped graphs (mostly DAGs with few back edges) a simple two-pass algorithm is as fast. Dominator info is used by Phase C (LICM, GVN scope), Phase D (fusion legality across control flow), Phase E (layout propagation through branch-merge points), and Phase H (CSSA insertion points).

### B.2 LivenessAnalysis

Classical backward dataflow. `LiveOut(B) = ⋃ LiveIn(S)` over successors, `LiveIn(B) = gen(B) ∪ (LiveOut(B) \ kill(B))`. Fused transfer `dst |= gen | (in & ~kill)` via SSE2-accelerated bitvector ops (Crucible's infra already has the right primitives; if not, port from ptxas's `sub_BDD560` pattern — the algorithm is textbook).

Per-tensor-class tracking: separate bitvectors for general tensors, reductions-in-progress, fragment registers (once we're in IR002 territory), predicate vectors. ptxas maintains 4-5 classes; we match this.

Liveness is computed **once per compile** and cached. Passes that need it request it; passes that would dirty it (C, D, H) re-run B.2 at their exit point.

### B.3 UseDefChains

For each SSA value, the list of consumers. For each consumer, the list of operands. Built once, updated incrementally by passes that mutate the graph. Consumed by strength reduction, copy propagation, DCE, CSE, rematerialization.

### B.4 DependenceGraph

Beyond SSA use-def: memory dependencies (RAW/WAW/WAR over shared storage), barrier dependencies, cluster-barrier dependencies, convergence dependencies (warp-sync ops). Edges are labeled with the kind of dependence. Phase D consumes this to check fusion legality; Phase H consumes this for scheduling.

Chain tokens — the semantics where every memory op carries an ordering edge — are implemented here. Mimic's event-driven simulator also consumes dependence edges at the SASS level, but B.4 produces the IR001-level graph Forge's scheduling uses.

### B.5 CostEstimate

Per-node analytical cost via Mimic's tier-1 (fast) simulator. This is a single-pass bottom-up walk over the graph; each node's cost is a function of its op type, input sizes, output sizes, and target capabilities. Nodes that don't fit Mimic's model (rare for ML) get a default conservative estimate.

The cost is not a final answer — Phase H will refine it via full simulation. B.5's cost is the fast estimator used by Phase D's fusion cost function and Phase F's tile proposal pruning.

### B.6 PersistenceAnalysis

For each intermediate value, compute the smallest storage class it can live in:
- **Register** — if its live range is bounded and fits within per-thread register budget
- **Shared memory (smem)** — if it's used across threads within a CTA
- **Cluster shared memory (DSMEM)** — if it's used across CTAs in a thread-block cluster (sm_90+)
- **Tensor memory (TMEM)** — if it's a tensor-core accumulator on sm_100/103/110
- **L2 / L1** — for streamed access patterns
- **HBM** — the fallback

This analysis drives Phase E's address-space resolution. It is computed from data-flow reachability and tile shape constraints. A tile that fits in 512 bytes of smem stays in smem; a tile that needs 32KB goes to HBM.

### B.7 CommunicationAnalysis

Cross-device edges: if the graph represents multi-GPU or multi-node work, this analysis identifies which edges cross device boundaries. Drives Phase K (DISTRIBUTE) decisions about collective operations and topology-aware routing.

### B.8 UniformityAnalysis

**Not an iterative fixpoint** — Crucible's graded types already encode uniformity at IR construction. Every value carries a `warp_shape` grade: `uniform` (all threads in a warp have the same value), `lane_varying` (varies across lanes but uniform across CTAs), `thread_varying` (fully varying). B.8 is a structural check that propagates this grade through ops with known uniformity semantics (e.g., `threadIdx.x` produces `thread_varying`; `c[bank][offset]` loads produce `uniform`).

This is the single largest Forge win over ptxas's `OriPropagateVarying` (phases 53, 70 — iterative fixpoint, expensive, prone to oscillation). Crucible has the information at IR construction time; Forge just reads it. Zero cost, perfect accuracy.

Uniformity drives: UR (uniform register) promotion in Phase H, address-space decisions in Phase E (constant-bank loads must be uniform), vectorization (uniform loads can coalesce into wider transactions), and scheduler decisions (uniform predicates don't cause warp divergence).

---

## 8. Phase C — REWRITE

Purpose: exhaustively canonicalize and optimize the IR001 graph using a fixed set of rewrite rules and a bounded iteration budget. This is Forge's analog of cicc's GeneralOptimize mega-pass (which runs 6× because cicc can't prove local confluence); we run at most 3 rounds because our rules are disjoint and confluent.

Budget: **25ms, 3 rounds max**. Rounds 2 and 3 gated at O1+.

### C.1 AlgebraicSimplification

Same as A.3 but with a larger rule table expanded for ML-specific patterns:
- Attention patterns (QK^T softmax V) recognized and canonicalized
- LayerNorm patterns (subtract mean, divide stddev, scale, bias) recognized
- RMSNorm patterns recognized
- Gated activations (SwiGLU, GeGLU) recognized
- MoE routing patterns recognized
- Positional encoding patterns (RoPE, ALiBi) recognized

The recognized patterns are tagged with `KernelPatternHint` annotations that Phase D's fusion uses to emit optimal templates (e.g., FlashAttention-3 for attention).

### C.2 ConstantFolding

Same as A.4 but now fires on values created by C.1's rewrites. Redundant — most folding happens in A.4, but C.1 can create new constant subtrees.

### C.3 StrengthReduction

The critical pass for affine addressing. Every ML kernel computes addresses of the form `base + i*stride_i + j*stride_j + k*stride_k`, and in the inner loop body this becomes a chain of IMUL operations per iteration. Strength reduction converts `k*stride` (computed fresh each iteration) to `prev_k_stride + stride` (incremental). Over thousands of iterations, this saves hundreds of thousands of instructions.

Algorithm: identify induction variables via Expr's structural analysis (the Crucible symbolic engine already tells us which Expr leaves are loop counters). For each IMUL whose multiplier is loop-invariant and whose multiplicand is an induction variable, insert a pre-loop computation and replace the in-loop computation with an IADD.

Special patterns:
- **Integer division by a constant** → multiply-high + shift (using the constant's reciprocal)
- **Modulo by a power of 2** → bitwise AND
- **Bit extraction patterns** (`(x >> a) & mask`) → BFE instruction
- **Bit insertion patterns** (`y & ~mask | ((x & mask2) << b)`) → BFI instruction
- **SHR + SHL combination** with specific shift counts → BFE
- **BFE + ADD** chains common in address decode → kept together as one IR node

ptxas has this pass (phase 21, `OriStrengthReduce`); it matches exactly one opcode (Ori opcode 137, pre-lowered IMAD) and uses a worklist propagation. We do the same, but over IR001 (not post-lowered Ori).

### C.4 CopyPropagation

Forward single-hop MOV chain collapse. If `v2 = v1` (trivial copy / identity) and `v2` has no side effects, replace all uses of `v2` with `v1` and delete the copy. This is cheap and eliminates ~5-15% of nodes that A.3 / C.1 rewriting can leave behind.

### C.5 GlobalValueNumbering

Hash-consed value numbering with commutativity normalization (operands sorted by hash) and dominator-scoped validity. Produces a canonical "value number" for every SSA value; values with the same number are merged.

Scope: dominator-tree-scoped. A value computed in one branch is not the same as a value computed in another branch even if they have the same structural form, because they may fire at different times. This is the correct GVN semantics.

Forge runs GVN **once** in Phase C. ptxas runs GvnCse (phase 49, 64) plus OriReassociateAndCommon (phase 50) plus LateOriCommoning — three separate passes at different pipeline positions. Collapsing to one is correct because our IR is cleaner to start with.

### C.6 LICM — Loop-Invariant Code Motion

Hoist expressions that are invariant within a loop to the loop preheader. Only affine-analyzable loops (bounded trip counts, no early exit) are considered. ML loops are almost all affine.

Safety: LICM does NOT speculate across divergent control flow. If an invariant expression is conditionally executed and moving it to the preheader would execute it unconditionally, LICM refuses. This prevents hoisting a load that might have been guarded against a null pointer.

One LICM instance, not four (cicc's mistake). The four instances exist because cicc's intervening passes create new invariants; our passes are self-cleaning.

### C.7 IvNarrowing

Induction variables that demonstrably fit in 32 bits (via range analysis) are narrowed from int64 to int32. This is a substantial code-size and register-pressure win because address arithmetic dominates ML inner loops.

Range analysis uses Expr's assumption flags (is_nonnegative, is_positive, etc.) and bounded-integer analysis. Safe narrowing requires proving the IV's range fits; cicc has the `(val + 0x80000000) <= 0xFFFFFFFF` trick (unsigned range test) which we steal.

### C.8 BarrierElimination

Dead barrier / dead sync elimination. Bidirectional dataflow analyzes memory effects around each barrier:
- **reads_above** — does any thread read shared memory above this barrier?
- **writes_above** — does any thread write shared memory above this barrier?
- **reads_below** — does any thread read shared memory below this barrier?
- **writes_below** — does any thread write shared memory below this barrier?

A `__syncthreads()` with `writes_above && reads_below == false` is dead (no writer waiting for a reader, no reader waiting for a writer). Remove it.

Cascading: removing a barrier can expose other dead barriers. Iterate until fixpoint (bounded by knob — typically converges in 1-3 passes).

This is worth ~20-100 cycles per eliminated barrier on Hopper, so in a kernel with 10+ barriers (common for multi-stage pipelines), we save hundreds of cycles of aggregate stall.

### C.9 DeadCodeElim (post-rewrite)

Another sweep after C.1 through C.8 to clean up anything newly dead. Same algorithm as A.5.

### C.10 PeepholeFusion

Finite-pattern table-driven peepholes that run on IR001 before lowering. Key patterns:
- `(x*a) + (y*b)` pattern → fused multiply-add node (pre-lowering hint)
- `max(x, 0)` → `relu(x)` semantic recognition
- `sigmoid(x) * x` → `silu(x)` (SiLU pattern)
- `LOP3` truth-table composition for predicate chains (e.g., `p && q && r` into a single 3-input LOP3 node)
- Constant-folded `cvt(cvt(x, t1), t2)` → `cvt(x, t2)` when valid

Rules: ~20. All deterministic, all cheap.

**Iteration**: Phase C runs C.1 through C.10 as a bundle, then re-runs the bundle up to 3 times if any rewrite fired. Convergence is detected by hashing the graph at the start and end of each round; if the hash is stable, we exit early. The bound of 3 is conservative; in practice most graphs converge in 1-2 rounds.

**Opt-level gating**: at O0, Phase C runs exactly 1 round (minimum cleanup). At O1+, up to 2 rounds. At O2+, up to 3. At O3+, Phase C can be re-invoked after Phase D fusion makes new opportunities visible (rare; typically not worth the budget).

---

## 9. Phase D — FUSE

This is the killer pass. XLA and Inductor do greedy pairwise fusion. Triton does no graph-level fusion at all (the user writes one kernel per fusion region). Forge does **global fusion via DP on linear chains with ILP escape for DAGs**, with explicit support for **multi-layer persistent-CTA fusion** that can pack an entire transformer block into one kernel when the register and smem budget allow.

Budget: **35ms** on a typical ML graph. Scales sub-linearly with graph size because most edges are pruned early.

### D.1 BuildFusionLattice

For each edge in the IR001 graph, enumerate the set of compatible `FuseKind` values (from Crucible's existing taxonomy plus extensions):

| FuseKind | Semantics | Storage for intermediate |
|---|---|---|
| `NONE` | Cannot fuse — intermediate goes through HBM | HBM |
| `REGISTER` | Same iteration space, same thread — intermediate in registers | Register |
| `SMEM` | Same CTA, different iteration space (e.g. broadcast, reduction epilogue) | Shared memory |
| `EPILOGUE` | GEMM / conv accumulator + elementwise, activation stays in accumulator | Tensor core accumulator (register or TMEM) |
| `PROLOGUE` | Input transformed in registers before GEMM / conv | Register |
| `BROADCAST` | Reduction result broadcast to pointwise via smem | Shared memory |
| `LAYER` | Full layer output → next layer input, whole block persistent | Register + smem budget |
| `CGA` | Cross-CTA via DSMEM (sm_90+) | Distributed shared memory |
| `TCGEN05_TMEM` | Tensor memory staged between tensor-core ops (sm_100+) | TMEM |

Each edge's compatibility set is determined by:
- The producer's output layout and storage location
- The consumer's input requirements (iteration space, access pattern, precision)
- The ops on both sides (elementwise fuses more easily than reduction, which fuses more easily than GEMM)
- The shape divisibility (tile-compatible shapes fuse; mismatched shapes require reshape first)
- Target capabilities (LAYER requires register headroom; CGA requires sm_90+; TCGEN05_TMEM requires sm_100+)

Forge enumerates the compatibility set per edge. A typical graph has ~5-15K edges and each has 1-6 compatible kinds. Total lattice size: ~100K edge-kind pairs. Stored as a compact per-edge bitmask (9 bits).

### D.2 CostFusionGroups

The cost of fusing two nodes under a given FuseKind has five components:

```
cost(group, kind) =
    + HBM_traffic(group, kind) × hbm_cost_per_byte    # what fusion avoids
    + launch_overhead × (1 if separate kernels)       # saved per fusion
    - saved_HBM_bytes × hbm_cost_per_byte             # primary benefit
    + register_pressure_penalty(group, kind)          # may cost occupancy
    + smem_pressure_penalty(group, kind)              # may exceed 228KB
    + fusion_structural_overhead(kind)                # pipeline depth, sync cost
```

Each term computed analytically from the graph shape and Mimic's cost estimates (Phase B.5). No measurement, no benchmarking. Target: <50μs per edge.

For LAYER fusion, the register and smem pressure terms dominate. The calculus: adding a layer to a persistent kernel saves one HBM round trip (net-win on memory-bound layers; sometimes loss on compute-bound ones if it forces occupancy down). The DP/ILP solver sees this cost function and decides how far to go.

### D.3 SolveFusionDP

For linear chains (the common case in ML: encoder block → FFN → next block), the fusion problem is a 1D DP over the chain:

```
f[i][k] = minimum cost of fusing nodes [i, i+k) into one region
f[i][k] = min over j in [1, k) of (f[i][j] + f[i+j][k-j] + split_cost(j)) 
       or cost_of_fused(nodes[i..i+k))
```

Complexity: O(N·k_max) where k_max is bounded by ~50 (max fusion group size). On a 1000-node chain, k_max=50, this is 50K table entries, each computed in O(1). Total: ~1ms. Trivial.

### D.4 SolveFusionILP

For DAGs (when a node has multiple consumers with different fusion preferences), the problem is a min-cost graph partitioning. Formulate as ILP:
- Binary variable per edge: `x_e ∈ {0, 1}` = "edge e is fused"
- Constraint: each node's incoming and outgoing fused edges must be consistent with its FuseKind compatibility
- Constraint: register pressure ≤ budget per fused region
- Constraint: smem ≤ 228KB per fused region (Hopper) or appropriate target cap
- Objective: minimize sum of edge costs given x_e values

Use CLP or CBC (open-source mixed-integer solvers). For graphs of <5000 edges, solve times are ~10-50ms. For larger graphs, fall back to DP on linear chains + greedy for cross-chain edges (warm-start ILP from DP solution if budget allows).

**Gated at O3+**. O2 uses DP only. O1 uses greedy pairwise. O0 skips fusion entirely.

### D.5 MultiLayerFuse

The special case worth its own pass: recognizing multi-layer patterns where the entire block fits in a persistent CTA. Attempted when:

```
estimated_regs_per_cta(layers_1..N) ≤ 0.6 × reg_budget
estimated_smem_per_cta(layers_1..N) ≤ 0.7 × smem_budget
at least one dimension loops over all tokens/batch items (persistent scheduling)
```

Patterns that fuse well at this scale:
- **Attention block**: Q/K/V projection + attention + output projection + residual + layer norm
- **FFN block**: GEMM + activation + GEMM + residual
- **Transformer block**: Attention + FFN (entire block in one kernel)
- **Convolution block**: Depthwise + pointwise + BN + activation + residual

FlashAttention-2/3 is the reference for how large a fusion can be. Forge generalizes it: any block whose working set fits in the cache hierarchy becomes a single kernel.

The cost function for LAYER fusion is sensitive to pipeline depth and TMA stage count. Mimic provides the analytical estimate; Phase H's MAP-Elites refines the pipeline structure within the fusion choice.

### D.6 EmitFusedRegions

Materialize the decisions into `FusedRegion` structs — views into the IR001 graph annotated with:
- The member node IDs (topologically ordered)
- The fuse kinds on each internal edge
- The input tensors (external inputs to the region)
- The output tensors (external outputs)
- Tile shape hints from Phase F (computed later)
- Register and smem budget estimates
- Target SM capability required
- A ContentHash for KernelCache lookup

These are what Phase H sends to Mimic.

**Output of Phase D**: a list of FusedRegions, each self-contained and independently compilable. Typical ML graph shrinks from 1000 IR001 nodes to 5-50 FusedRegions. Most regions are small (1-3 ops); a few are large (20-200 ops, especially after multi-layer fusion). The big regions dominate runtime and get the most compilation effort.

---

## 10. Phase E — LOWER_TO_KERNELS (the IR001 → IR002 boundary)

Purpose: take each FusedRegion from Phase D and lower it into one or more `KernelNode`s by matching to the kernel catalog, committing to layouts at a semantic level (row / col / tiled / strided — **not** vendor address spaces), pinning a `NumericalRecipe`, and materializing per-kind attribute structs. This is the phase where the DAG transitions from op-level semantics to kernel-level concrete shapes.

Budget: **20ms** on a typical ML graph.

### E.1 MatchKernelTemplate

For each FusedRegion, walk a catalog of kernel-template matchers. Match order is priority-sorted: more-specific templates first, `CUSTOM` fallback last.

The catalog has ~21 templates:

```
GEMM · BMM · CONV · ATTENTION · SOFTMAX · NORM · REDUCE · SCAN · 
POINTWISE · GATHER_SCATTER · EMBEDDING · RNG · COLLECTIVE · SSM · 
DEQUANT_GEMM · MOE_ROUTE · RAGGED_ATTN · PAGED_ATTN · 
FUSED_COMPOUND · OPAQUE_EXTERN · CUSTOM
```

Each matcher is a structural pattern: "this region's op sequence is `aten::mm`, maybe followed by `add` (bias), maybe followed by pointwise tail" → `FUSED_COMPOUND(GEMM + bias + activation)`. The matcher reads the IR001 nodes, CKernelIds, and fuse-edge kinds; it writes a `KernelKind` plus per-kind attrs (GemmAttrs, AttentionAttrs, NormAttrs, ...).

Match order matters: a region that's both "matmul + activation" and "matmul" should match the fused form, not the bare GEMM. Matchers declare priorities; first-match wins.

Unmatchable regions fall through to `CUSTOM`, which wraps the IR001 nodes directly and lets the backend produce a generic loop-nest kernel. This is the escape valve for research workloads, novel fused patterns, user-defined ops.

### E.2 LayoutCommit

Every KernelNode has `layout_in` and `layout_out` fields at IR002 level. Legal values are `Strided`, `RowMajor`, `ColMajor`, `Tiled` (backend-defined blocked layout), `Swizzled` (backend-defined bank-conflict-free layout), and `Broadcast` (stride-0 degenerate). These are *semantic* layouts; they map to concrete vendor layouts inside Mimic.

Decision is by dataflow over the KernelGraph:
- Producer-constrained: a GEMM produces `Tiled` output naturally on any backend
- Consumer-constrained: a reduction over axis=0 wants stride-1 on that axis
- Convention-constrained: CONV defaults to NHWC on all current ML backends; it's not a choice, it's the accepted shape
- Meet conflicts: insert explicit `transpose` or `layout_cast` IR002 nodes with nonzero cost

IR001-level `CKernelId::VIEW/RESHAPE/PERMUTE/TRANSPOSE` operations that are pure metadata survive through Phase E as layout-change KernelNodes with `kind = POINTWISE` and zero computational cost but nonzero scheduling effect.

### E.3 RecipeSelect

This is the phase's most important decision. For each KernelNode, pick a `NumericalRecipe` from the global registry that:

1. Matches the kernel's required numerical semantics (accumulator dtype, reduction algorithm, rounding mode, scale policy, softmax recurrence variant).
2. Is natively supported by every target in the current fleet (intersection query against the registry's `native_on` bitmap per recipe).
3. Minimizes estimated cost (from Mimic's `fast_cost` at tier-1) among compatible recipes.

If the current fleet has mixed chip types (H100 + MI300X + trn2), the picker returns the best recipe supported on all three natively. If no recipe satisfies determinism guarantees on all chips, the picker raises a diagnostic and Forge either:
- `fleet_policy = STRICT` (default for training): refuses, logs, asks the runtime to reconsider fleet membership
- `fleet_policy = ADAPT`: picks the best recipe available on the weakest member, warns, proceeds

Recipes are interned via `RecipePool` (Swiss table, same pattern as ExprPool). Same recipe → same `const NumericalRecipe*` → pointer equality. Typical ML model uses 5-15 distinct recipes total.

### E.4 TileSpecSeed

For each KernelNode, seed a `TileSpec*` with:
- `dims[]` — from the kernel's attribute struct. Can be concrete (pointer to interned `Expr::INTEGER`) or symbolic (pointer to a `SYMBOL` with known range bounds in `SymbolTable`).
- `threads_per_cta`, `regs_per_thread`, `shared_bytes` — abstract estimates, refined in Phase F.

This is just a seed; Phase F refines it via `mimic::propose_tiles(kernel, abstract_caps)`. But the seed is needed so memory planning can have slot sizes available for lifetime intervals.

### E.5 EmitKernelGraph

Materialize the final IR002 KernelGraph:

```cpp
class KernelGraph {
    Arena           arena;            // owns everything
    KernelNode**    nodes;            // topologically ordered
    uint32_t        num_nodes;
    RecipePool      recipes;          // interned NumericalRecipes
    TilePool        tiles;            // interned TileSpecs
    AttrsPoolUnion  attrs;            // per-kind attr pools
    SlotId*         slot_types;       // per-slot dtype/layout metadata
    const Expr**    external_inputs;  // IR001 nodes referenced as kernel inputs
    const Expr**    external_outputs;
    // ...
};
```

All arena-allocated. No heap. Same axiom discipline as `Graph` (non-copyable, non-movable, InitSafe NSDMI, TypeSafe strong IDs).

### E.6 VerifyIr002

Structural invariants the verifier checks at Phase E exit:
- Every KernelNode has a valid `recipe` pointer (non-null, interned)
- Every KernelNode has a valid `tile` pointer (non-null, interned)
- `layout_in` compatible with each input's producer `layout_out` (or explicit cast inserted)
- Attrs struct matches `kind` (GemmAttrs for GEMM, etc.)
- Content hash computes deterministically from (kind, attrs, recipe, tile, input slot types)

Failure here is a bug in E.1-E.5; diagnose loudly.

**Output of Phase E**: a KernelGraph of 5-50 KernelNodes (typical), each with committed kind, recipe, tile seed, layout, attrs, and slot bindings. Typical ML model: ~1000 IR001 nodes → ~30 IR002 KernelNodes after fusion and lowering.

---

## 11. Phase F — TILE

Purpose: refine the seeded TileSpecs on each KernelNode into a ranked pareto set of concrete tile shapes that Mimic's per-vendor backend will search over during Phase H.

Budget: **40ms**. Scales with number of large kernels.

### F.1 ProposeTiles

For each KernelNode, call:

```cpp
std::span<const AbstractTile> mimic::propose_tiles(
    fx::Bg, Arena&, const KernelNode&, const TargetCaps&);
```

Mimic's per-vendor backend generates tile candidates that are hardware-realizable for the current target. The returned `AbstractTile`s carry only semantically-portable fields:

```cpp
struct AbstractTile {
    const Expr* dims[4];          // M, N, K, ... — concrete or ranged symbolic
    uint16_t    threads_per_cta;
    uint16_t    regs_per_thread_estimate;
    uint32_t    shared_bytes_estimate;
    uint32_t    pipeline_depth;   // abstract stage count
    uint8_t     flags;            // persistent, split_k, streaming, ...
};
```

No WGMMA shapes, no MFMA shapes, no MXU shapes leak into Forge. The per-vendor backend maps `AbstractTile{M=128, N=128, K=64, ...}` to `WGMMA m64n128k16 × tile_k_iter=4` on Hopper, or `MFMA 32×32×8×4` on MI300, or `MXU 128×128` on TPU v5p internally. Forge sees numbers; Mimic sees silicon.

Typical: 16-64 candidates per kernel, pre-filtered by Mimic for occupancy/register/smem feasibility on the target.

### F.2 RankByFastCost

Score each candidate via `mimic::fast_cost(kernel_with_tile, caps) → cycles_estimate`. Tier-1 simulator, ~10μs per call, ~16-64 calls per kernel = 160-4000μs total per kernel, fits in the 40ms budget comfortably.

Rank by estimated cycles; prune to the top 8-16 for Phase H's MAP-Elites seed population.

### F.3 FuseTileConstraints

Fused compound kernels (FUSED_COMPOUND, ATTENTION, NORM-with-epilogue) have per-component tile constraints that must agree. Enforce them:
- GEMM's output tile must match the consuming pointwise op's input tile
- Attention's softmax tile must match the Q@K^T tile's N dimension
- Reduction's accumulator tile must match the post-reduction output tile

Drop candidates that would require cross-kernel tile disagreement.

### F.4 ShapeRangeBucket

If a kernel has a symbolic tile dim (e.g., `seq_len ∈ [32, 4096]` from SymbolTable ranges), partition the range into buckets. Typical bucketing: 4-8 log-spaced buckets per dim. MAP-Elites archive keys include the bucket; at runtime, the dispatch picks the bucket containing the actual value and uses that compiled kernel.

This is how Forge handles dynamic shapes without recompile-per-shape: specialize per bucket, not per value.

**Bucket partitioning policy.** Log-spaced with per-kernel-family overrides:

| Kernel family | Default buckets | Reason |
|---|---|---|
| GEMM (M, N, K) | 5-8 per dim, log-spaced | tile-shape-sensitive; small shapes benefit from distinct kernels |
| ATTENTION (seq_len) | 7 buckets: [1,64], [65,512], [513,2048], [2049,8192], [8193,32768], [32769,131072], [131073,524288] | prefill/decode boundary around 2048; long-context scaling beyond 8192 |
| ATTENTION (batch) | 4 buckets: [1,2], [3,8], [9,32], [33,256] | decode typically batch<=4; training typically batch>=8 |
| CONV (H, W) | 3-4 buckets per dim | fewer bucket splits; shapes tend to be fixed per model |
| EMBEDDING (vocab) | 4 buckets log-spaced | rarely varies within a model; usually 1 bucket |

**Parametric kernel emission.** For the long-tail bucket (typically the top bucket whose upper bound is the symbolic dim's declared max) or when the user annotates `parametric=True`, Forge signals Mimic to emit a kernel that accepts shape as a runtime argument:

```cpp
struct ParametricTileSpec : TileSpec {
    // In addition to the base TileSpec fields, declare:
    // - which dims are runtime parameters
    // - the SymbolTable ranges for each
    uint16_t    runtime_dim_mask;       // bit per dim that's parametric
    uint16_t    runtime_tile_mask;      // bit per tile dim that's parametric
};
```

Mimic's lowering emits a kernel with shape as an argument; the outer loop bounds use runtime values; the inner tile loop uses compile-time tile dims (still concrete). Per-op overhead: ~1-3% from the runtime-shape arithmetic and bounds checks.

Used for:
- The top bucket of an infrequently-hit range (avoid compiling a rarely-used specialized kernel)
- Shapes the user annotated as "truly dynamic" (streaming inference with arbitrary prompt lengths)
- Fallback during background compile of a novel bucket

**Online bucket sub-specialization.** Per CRUCIBLE.md §11.7, Augur tracks per-bucket hit counts and the in-bucket distribution of actual shape values. If an existing bucket sees traffic concentrated in a narrow sub-range, Phase F can be re-invoked in the background for the sub-range, compiling a new specialized kernel and inserting it into the dispatch table before the parent bucket's entry. Sub-specialization is write-once: both entries coexist; the sub-bucket preempts when its range matches.

### F.5 CommitTilePareto

Write the pareto set into the KernelNode's `tile_candidates` field. Phase H's Mimic invocation gets the full pareto set and runs MAP-Elites within it.

**O-level gating**: O0/O1 picks top-1 from ranked candidates (no search). O2 takes top-4 into Phase H. O3 takes top-16 into Phase H for full MAP-Elites exploration.

**Output of Phase F**: per KernelNode, a ranked pareto set of ≤16 AbstractTile candidates. Seeds Phase H.

---

## 12. Phase G — MEMPLAN

Purpose: static memory plan for all intermediate tensors. Given the IR002 KernelGraph and committed tile shapes, compute lifetimes, detect aliases, assign offsets into a single pre-allocated pool per device.

Budget: **15ms**. Same Crucible L3 PoolAllocator model that already exists, applied to IR002 KernelNode input/output slots.

### G.1 LifetimeIntervals

For each slot in the KernelGraph (every KernelNode's input_slots and output_slots):
- Birth = producer's topological index
- Death = last consumer's topological index
- Size = computed from TileSpec dims + dtype — concrete when tile dims are concrete, max-over-range when symbolic with bounds

Intervals are computed from KernelGraph use-def chains; no new analysis.

### G.2 AliasDetection

Views, transposes, reshapes, in-place ops (accumulator write) alias the producer's buffer. Detected via AliasMap — a union-find over SlotIds. The base storage's interval covers all its aliases.

Crucible's TraceGraph already populates ALIAS edges at IR001; E.2 LayoutCommit preserves them into IR002; G.2 lifts them to the plan level.

### G.3 OffsetAssign

Classic interval-graph coloring problem. Greedy first-fit sorted-by-birth:

```
sorted_slots = sort(slots, by = slot.birth)
for s in sorted_slots:
    free_offset = first position where (offset + s.size) does not overlap 
                  any live slot's (offset, offset+size)
    s.offset = free_offset
total_pool_bytes = max(s.offset + s.size for all s)
```

In practice yields ~85-95% packing efficiency. For kernel graphs with fine-grained lifetimes, competitive with optimal.

### G.4 CheckpointDecide

For each potentially-stored-for-backward activation:

```
if store_cost / recompute_cost > threshold:
    recompute at use
else:
    store in pool
```

`store_cost` = size × HBM_bandwidth_cost × 2 (write + read), from abstract `TargetCaps.hbm_bw_bytes_per_cycle`.
`recompute_cost` = `mimic::fast_cost` prediction for the forward subgraph that produces it.

Threshold is adaptive to memory pressure: plenty of memory → store everything (fastest); tight → recompute aggressively. Crucible L3's automatic activation checkpointing.

### G.5 OomCheck

`plan.pool_bytes ≤ device_memory - reserved`. If not, adapt:
1. Force more checkpointing (G.4)
2. Reduce batch size (emit `BATCH_TOO_LARGE` diagnostic to runtime)
3. Offload optimizer state to host memory (L12 distribution)

Never just-fail. Crucible invariant: OOM is structurally impossible.

### G.6 DeterministicPlan

Plan is the same for the same KernelGraph — no history-dependent allocator non-determinism. Crucible Axiom 8 DetSafe.

**Output**: `MemoryPlan { total_bytes, slots[]: (SlotId, offset, size, alignment) }`. A single pool allocation at iteration start (via the per-vendor runtime library's equivalent of `cudaMalloc`; Mimic's runtime provides this). Every "allocation" at runtime is `base_ptr + offset`, ~2ns.

---

## 13. Phase H — COMPILE (delegate to Mimic per-vendor backend)

Purpose: hand each IR002 KernelNode to Mimic's per-vendor backend, receive back an opaque `CompiledKernel` (vendor-binary bytes + predicted cycles + insights + archive). **Forge does no compilation itself at this phase.** Everything below IR002 is Mimic's domain.

Budget: **60ms per kernel, parallelizable across kernels**. A kernel that exceeds budget is killed and the runtime falls back to the Genesis Kernel Pack seed for that shape. Background thread retries on next opportunity.

### H.1 DispatchToBackend

```cpp
CompiledKernel mimic::compile_kernel(
    fx::Bg,
    Arena& arena,
    const KernelNode& kernel,              // IR002, portable
    const TargetCaps& caps,                 // abstract, with opaque vendor_specific tag
    const CompileConfig& cfg = {}
);
```

Mimic's `compile_kernel` is the dispatcher. Internally it looks up `caps.vendor_id`, routes to the per-vendor backend (`mimic::nv::compile_kernel`, `mimic::am::compile_kernel`, `mimic::tpu::compile_kernel`, `mimic::trn::compile_kernel`, `mimic::cpu::compile_kernel`, etc.), and returns an opaque `CompiledKernel`.

What happens inside the per-vendor backend (documented in MIMIC.md; summarized here for reference only):

1. **L1 cache check** — `(hash_ir002, vendor_caps_hash) → IR003* snapshot`. Hit → skip to step 6.
2. **Lower IR002 → IR003*** — kernel template + recipe + tile → vendor machine IR. Includes CSSA, address-space tagging, register allocation, scheduling.
3. **Peephole + templates** — vendor-specific IR003* transforms.
4. **MAP-Elites search** — mutate IR003* candidates, simulate via vendor simulator (three-tier: fast/medium/accurate), accept by fitness, archive.
5. **Encode to bytes** — IR003* → vendor binary format (cubin / HSACO / NEFF / TPU-exec / CSL / ELF).
6. **L3 cache check** — `(hash_ir003, chip_caps_hash) → binary bytes`. Hit (from step 1 warm-start) → reuse. Miss → persist the newly-compiled binary.
7. **Return CompiledKernel** — opaque bytes + predicted cycles + structured insights.

All of this is opaque to Forge. Forge's Phase H just calls `compile_kernel` per kernel on its thread pool.

### H.2 CollectResults

Collect the `CompiledKernel` from Mimic per kernel. Link each into the emerging `ExecutionPlan` (Phase J will serialize). Record the kernel's `(content_hash_ir002, vendor_caps_sig)` in the region-to-kernel mapping for L2 cache hits on subsequent compiles.

The insights array Mimic returns (up to 512 structured diagnostics per kernel) is written to the Cipher alongside the binary — not consumed by Forge directly, but available to Augur for drift-attribution analysis later.

**Parallelism**: Phase H is the primary parallelizable phase. Multiple KernelNodes compile concurrently on a thread pool sized by `std::thread::hardware_concurrency()`. No shared mutable state except the KernelCache (three-level, lock-free). Speedup is near-linear up to core count. A graph of 30 KernelNodes on a 32-core machine compiles in ~1 kernel's worth of wall time.

---

## 14. Phase I — SCHEDULE

Purpose: decide launch order, stream assignment, cross-stream barriers, and emit abstract launch records. Launch capture into vendor-specific graph objects (CUDA Graph, HIP Graph, TPU plan, NEFF submission) happens inside the per-vendor Mimic runtime, not here.

Budget: **10ms**.

### I.1 StreamAssign

Independent KernelNodes — those with no data dependency — can launch on separate abstract streams for concurrent device execution. Assignment by topological sort + earliest-start heuristic:

```
streams = [[] for _ in range(stream_count)]
for kernel in topological_order:
    earliest_stream = stream with latest-finishing predecessor of kernel
    if earliest_stream has finished: reuse
    else: pick least-loaded alternative stream
    streams[picked].append(kernel)
```

Typical: 2-4 streams sufficient. Stream IDs are abstract — Mimic's per-vendor runtime maps them to CUDA streams / HIP streams / TPU execution queues / NeuronCore launch queues as appropriate.

### I.2 BarrierInsert

Cross-stream dependencies become abstract barrier records:

```cpp
struct BarrierRec {
    uint16_t producer_stream;
    uint16_t consumer_stream;
    uint32_t producer_kernel_idx;   // in that stream's launch list
    uint32_t consumer_kernel_idx;
};
```

Each barrier is a fixed-cost record. Per-vendor runtime realizes it as cudaEvent / hipEvent / TPU DMA completion / NeuronCore sync_engine event. Minimize barrier count via fan-out coalescing (one event releases multiple dependents).

### I.3 LaunchOrder

Within each stream, kernels launch in topological order with earliest-start-time prioritization. Order is fully determined here; no runtime scheduling.

### I.4 GraphCaptureHint

If all KernelNodes have concrete tile dims (no runtime-variable shapes), set `plan.captureable = true`. The per-vendor runtime will capture the launch sequence into its native graph format (CUDA Graph on NVIDIA, HIP Graph on AMD, TPU precompiled plan, NEFF submit list on Neuron) at runtime, collapsing N launches into one replay. Forge just records the hint.

Dynamic shape handling: bounded cache of captured plans per shape bucket. Bucketing rule comes from F.4 ShapeRangeBucket.

---

## 15. Phase J — EMIT

Purpose: serialize the ExecutionPlan and commit it to Cipher. The plan holds opaque `CompiledKernel` handles from Mimic (one per kernel), abstract launch records, memory plan, guards, and hashes. Vendor binary bytes live inside the CompiledKernel and are opaque to Forge.

Budget: **25ms**.

### J.1 SerializePlan

Pack the ExecutionPlan into a memory-mapped-friendly binary format:

```cpp
struct ExecutionPlan {
    MemoryPlan                       memory;           // SlotId → offset + size
    std::span<const CompiledKernel*> kernels;          // opaque per-vendor blobs
    std::span<const LaunchRec>       launches;         // (stream_id, kernel_idx, arg_slot_refs)
    std::span<const BarrierRec>      barriers;         // cross-stream events
    uint8_t                          stream_count;
    bool                             captureable;      // Phase I hint
    std::span<const KernelContentHash> kernel_hashes;  // one per kernel, for L2 cache
    std::span<const ContentHash>     region_hashes;    // one per IR001 region
    std::span<const Guard>           guards;           // schema, shape, dtype, device
    ContentHash                      plan_hash;        // Merkle over everything
    const TargetCaps*                caps;             // abstract; vendor_specific inside
    uint32_t                         compile_version;
};
```

All arena-allocated. Typical plan size: 100KB-few MB for a full model. `CompiledKernel*` is an opaque handle; its contents (vendor binary bytes) are managed by Mimic's runtime.

### J.2 GenerateDispatchTable

Native C++ dispatch table that the foreground runtime's COMPILED-mode uses:

```cpp
struct DispatchEntry {
    const CompiledKernel* kernel;   // opaque
    uint16_t stream_id;
    uint16_t barrier_wait_mask;
    uint16_t barrier_signal_mask;
    uint16_t num_args;
    const SlotRef* args;            // {slot_id, offset_within_slot}
};

extern const DispatchEntry g_dispatch[];
extern const uint32_t g_dispatch_count;
```

Generated as a single `.cpp` file. Compiled via cached clang invocation, ~200-500ms for a 100-kernel model. Compile output cached in Cipher; cost amortizes across runs.

The foreground dispatch is: advance op index, look up `DispatchEntry`, call Mimic's runtime to launch. No vendor SDK involvement from Forge — Mimic's runtime wraps the kernel driver directly.

### J.3 RelocateSlotPointers

The dispatch table carries `SlotRef {slot_id, offset}` pairs, not absolute addresses. At runtime init, `runtime::resolve_plan(plan, pool_base)` walks once to materialize absolute pointers into a side-table. This makes the ExecutionPlan position-independent (same plan works at any pool base) and cacheable.

### J.4 CommitToCipher

Persist the plan + embedded CompiledKernel handles + MAP-Elites archives to Cipher (L14). Event-sourced; survives reincarnation. The `plan_hash` indexes the Cipher entry; lookups are O(log N) via Cipher's internal B-tree.

On load from Cipher into a different process, the CompiledKernel handles rehydrate by reloading the vendor binaries through Mimic's per-vendor runtime (our own loader, not vendor SDK `cuModuleLoad` / `hsa_executable_load`).

### J.5 Plan chaining and semaphore lifecycle

Multi-plan sequencing is driven by semaphore-based ChainEdges (CRUCIBLE.md §11.9.3), allocated from per-Keeper pools at init and composed through ExecutionPlan's `chain_in` / `chain_out` spans. Phase J.5 emits the appropriate wait/signal pushbuffer instructions at plan boundaries.

#### J.5.1 ChainEdge composition

Each Plan carries:
- `chain_in`: list of `ChainEdge{sem_id, expected_value, kind=WAIT_IN}` — Plan waits for these at its start
- `chain_out`: list of `ChainEdge{sem_id, signal_value, kind=SIGNAL_OUT}` — Plan writes these at its end

Phase J inserts per-vendor pushbuffer instructions:

| Vendor | WAIT_IN primitive | SIGNAL_OUT primitive |
|---|---|---|
| NV (Hopper+) | `SEM_ACQUIRE ACQ_CIRC_GEQ, ACQUIRE_SWITCH_ENABLED` | `SEM_RELEASE RELEASE_WFI_DIS` |
| AMD | `WAIT_MEM_ZERO` / `WAIT_REG_MEM_GEQ` | `RELEASE_MEM` (PM4) |
| TPU | `WAIT_FOR_EVENT` (scalar processor) | `SIGNAL_EVENT` |
| Trainium | `BARRIER_WAIT` (sync_engine) | `BARRIER_SIGNAL` |
| CPU | atomic compare + cond var | atomic store |

NV's `ACQUIRE_SWITCH_ENABLED` bit is important: it allows the SM to context-switch to another kernel during wait, so long waits don't tie up execution resources.

#### J.5.2 Semaphore pool allocation

Semaphores are pinned-memory 8-byte slots in a Keeper-wide pool allocated at `Keeper::init()`. Pool size is `2 × max_chain_depth × num_concurrent_plans`, typically 64-256 entries per Keeper (~2-8 KB pinned sysmem).

```cpp
struct SemaphorePool {
    std::span<uint64_t>      slots;              // pinned sysmem, 8B each
    std::span<SemaphoreMeta> metadata;            // owner_tag, refcount, last_committed
    BitVector                free_mask;            // lock-free CAS alloc
};

SemaphoreId SemaphorePool::acquire(fx::Init, std::string_view owner_tag);
void        SemaphorePool::release(SemaphoreId);  // returns slot to pool
```

Pool metadata is Raft-committed; Cipher persists `last_committed` per slot so chain continuity survives process restart.

#### J.5.3 Lifecycle phases

**Compile time**: Forge Phase J allocates needed semaphores via `SemaphorePool::acquire(plan_hash)`. SemaphoreIds embed into the Plan's chain_in/chain_out spans. Phase J emits the pushbuffer wait/signal instructions at correct byte offsets.

**Submit time**: The runtime patches `SEMAPHORE_VALUE` PatchPoints with the expected-for-this-epoch values, issues one doorbell. The device-side pushbuffer executes waits/signals autonomously.

**Step completion**: SIGNAL_OUT writes advance the semaphore; WAIT_IN satisfied in subsequent plans. No host involvement for intra-chain progression.

**Plan eviction**: When a Plan is evicted from Cipher (L3 pressure), `SemaphorePool::release` returns its semaphores to the pool if refcount drops to zero. Raft-committed so a Plan's semaphore can't be reclaimed while any replica's Cipher still references it.

**Failure recovery**: Per CRUCIBLE.md §10.7, rollback loads the checkpoint's last-committed semaphore values. Semaphore IDs stable across restarts; only values reset. Mid-chain failures don't leave orphan signals — the wait on step T+1 includes T's checkpoint, not T's in-flight state.

#### J.5.4 Canonical chain patterns

**Training step** (6 plans composed via 6 semaphores):

```
forward         ∅                  → sem_step=N
backward        sem_step>=N        → sem_grad=N
allreduce       sem_grad>=N        → sem_reduced=N
optimizer       sem_reduced>=N     → sem_weight=N+1
dataload        ∅                  → sem_data=N+1
forward_N+1     sem_weight>=N+1
                 AND sem_data>=N+1  → sem_step=N+1
```

All six plans submit concurrently at epoch start. Device sequences via semaphores; host observes only the terminal `sem_step` advance. Per-step progression: ~0 CPU time.

**Pipeline parallelism** (N stages × M micro-batches with zero-bubble scheduling per CRUCIBLE.md §12.6):

```
stage_k forward(mb_m)     sem_stage_k_in>=m_prev → sem_stage_k_out=m,
                                                   sem_stage_{k+1}_in=m
stage_{k+1} forward(mb_m) sem_stage_{k+1}_in>=m   → sem_stage_{k+1}_out=m
```

N+M-1 chain depth; forward/backward plans interleave to fill bubbles via the same primitives.

**Speculative decoding**:

```
draft_N(step=S)       ∅                → sem_draft=S
target_verify(step=S) sem_draft>=S     → sem_accept=S
accept_branch(step=S) sem_accept>=S    (predicate patched=1)
reject_branch(step=S) sem_accept>=S    (predicate patched=0)
```

Either accept_branch or reject_branch runs based on a CONDITIONAL PatchPoint patched by the verifier.

#### J.5.5 Host-side cost

Plan chain submission is O(plans) SFENCE-separated MMIO writes at init, zero per-step thereafter. Device executes the chain autonomously; host CPU is <1% of one core spin-polling the terminal semaphore.

Typical numbers on an 8-plan training chain:
- Init (all 8 plans submit): ~1-2μs CPU time
- Per-step progression: ~0 CPU (device-driven)
- Terminal poll: ~100 ns per step

### J.6 Pushbuffer layout per KernelKind

Phase J emits pushbuffer bytes per-vendor; the layout depends on KernelKind. This section specifies expected byte counts on NV Hopper+ as the reference. Other vendors have comparable command streams but different encodings (AMD PM4, TPU scalar-proc bytecode, TRN NEFF submit; see MIMIC.md §15.4).

#### J.6.1 Per-launch byte counts (NV Hopper)

| KernelKind | Methods | Pushbuffer bytes | Notes |
|---|---|---|---|
| GEMM (single-launch) | SEND_PCAS_A + SIGNALING_PCAS2_B | 16 | QMD pre-built; only schedule-invalidate fires |
| BMM | 2 methods × num_batches (split) | 16-128 | Batched launches may merge into one QMD |
| CONV | 2 methods + shape setup | 24-40 | Implicit GEMM via QMD configuration |
| ATTENTION (FLASH3) | 2 methods, persistent kernel | 16 | One launch covers whole attention |
| NORM | 2 methods | 16 | LayerNorm/RMSNorm as one kernel |
| SOFTMAX | 2 methods | 16 | |
| POINTWISE | 2 methods | 16 | |
| REDUCE | 2 methods | 16 | |
| SCAN | 2 methods | 16 | |
| RNG | 2 methods + RNG_COUNTER patch | 24 | 8 B patch writes Philox counter |
| COLLECTIVE (AR 8-way ring) | ~16 methods (one per hop × 2 dir) + barriers | 100-150 | CNTP pushbuffer-embedded, MIMIC.md §37.5 |
| SSM (Mamba) | 2 methods, persistent kernel | 16 | State scan internal |
| EMBEDDING | 2 methods | 16 | |
| FUSED_COMPOUND | 2 methods per fused chunk | 16-64 | Depends on fusion depth |
| MOE_ROUTE | dispatch + per-expert launch + combine | 200-500 | Uses EVENT_TENSOR PatchPoints |
| LoopNode body | 2 + counter-decrement + conditional jump | 40-80 | Looped pushbuffer with device-side jump |
| BranchNode | 2 per arm + predicate read | 40-80 | Both arms present, predicate selects |

A typical transformer forward pass (24 layers × attention + norm + MLP + residual) emits ~2-4 KB of pushbuffer. A full training step (forward + backward + optimizer update + all-reduce) emits ~8-16 KB.

#### J.6.2 QMD layout per compute kernel

Each compute kernel has a QMD (Queue Method Descriptor) pre-built in the Plan's QMD pool. QMD is 256 bytes on Hopper+ (layout in `cla0c0qmd.h`) and contains all kernel-launch fields:

- Program entry address (SASS bytes address in VRAM)
- Grid dimensions (`CTA_RASTER_WIDTH/HEIGHT/DEPTH`) — PatchPoint-addressable for SHAPE_DIM
- Block dimensions (`CTA_THREAD_DIMENSION_0/1/2`)
- Shared memory size
- Register count
- Constant buffer bindings (c[0]..c[7]) with addresses
- Semaphore release fields (optional, for auto-completion signaling from SKED)

The c[0] bank addressed by the QMD points at a region in the Plan's `constbank_arena`. That arena holds the scalar bytes that PatchPoints mutate; the kernel reads via `LDC` at well-known offsets.

**Layout discipline**:
- Plan `constbank_arena` is a single contiguous buffer with kernel-specific sub-regions
- Per-kernel region: reserved driver params (0..0x1a0), user kernel params (0x1a0..N), patch-addressable scalars (N..M)
- PatchPoints declared with offsets relative to the constbank_arena base
- At runtime, `resolve_plan(pool_base)` computes absolute addresses once, caches them; subsequent patches write to cached absolute addresses

#### J.6.3 Inter-kernel barrier emission

Same-stream dependencies are implicit (GPFIFO order). Cross-stream or cross-CTA dependencies emit explicit barrier instructions:

- NV: mbarrier-based with arrive/wait, `MBAR.SYS.ACQ`/`MBAR.SYS.REL` methods; ~16 bytes per barrier
- AMD: `s_waitcnt_vmcnt` + `s_barrier` equivalents
- TPU: scalar processor sync events

Phase I.BarrierInsert (§14.2) places these at minimum positions; typical transformer step has 5-20 barriers, ~300 bytes of barrier overhead total.

#### J.6.4 Empirical plan-size distribution

For a 7B-param transformer at FP16, seq_len=8192, batch=32:

| Plan type | Pushbuffer | QMDs | PatchPoints | Constbank |
|---|---|---|---|---|
| Forward step | ~4 KB | ~80 | ~30 | ~16 KB |
| Backward step | ~6 KB | ~120 | ~40 | ~20 KB |
| All-reduce (DP) | ~800 B | ~8 | ~5 | ~2 KB |
| Optimizer step | ~2 KB | ~40 | ~10 | ~8 KB |
| Data-advance | ~200 B | ~2 | ~3 | ~1 KB |
| **Total per step** | **~13 KB** | **~250** | **~88** | **~47 KB** |

One Plan-group per training step occupies ~60 KB of Cipher space including metadata. A 500K-step training run occupies ~30 GB of plan storage — all content-addressed, all deduplicated. Most steps are byte-identical except for `RNG_COUNTER` and `SEMAPHORE_VALUE` patches, so actual unique plan count is ~5-50 per run (one per shape bucket × schedule transition).

---

## 16. Phase K — DISTRIBUTE

Only runs if `world_size > 1`. Purpose: partition the KernelGraph across multiple devices, insert abstract collective primitives at partition boundaries, optimize collective patterns. The actual collective implementation is per-vendor (inside Mimic; see MIMIC.md §26), but the placement is Forge's decision.

Budget: **15ms**.

### K.1 PartitionGraph

Compiler-level 5D parallelism auto-tune. Five dimensions:

| Dim | What it splits | Abstract collective |
|---|---|---|
| TP (tensor parallelism) | Individual GEMM by K dim or N dim | all-gather, reduce-scatter |
| DP (data parallelism) | Batch across devices | all-reduce |
| PP (pipeline parallelism) | Layers across devices | point-to-point, bubble overhead |
| EP (expert parallelism, MoE) | Experts across devices | all-to-all |
| CP (context parallelism) | Sequence across devices for long-context attention | point-to-point, cross-attention gather |

Each has a cost from Meridian's N×N bandwidth/latency probe matrix. The probe runs via CNTP's measured topology (see Crucible-native networking design; no NCCL involvement). Partitioning objective: minimize total time subject to per-device memory constraint.

For typical 8x accelerator setups on a single fabric, the ILP is ~50 variables, solves in <1s. Larger topologies (64 nodes, multi-tier) partition hierarchically.

### K.2 InsertCollectives

Insert `KernelKind::COLLECTIVE` nodes at partition boundaries. Each carries a `CollectiveAttrs` struct:

```cpp
struct CollectiveAttrs {
    CollectiveOp  op;             // ALLREDUCE, ALLGATHER, REDUCE_SCATTER,
                                   // ALL_TO_ALL, BROADCAST, SEND, RECV, ...
    ReduceOp      reduce_op;       // for reductions: SUM, PROD, MIN, MAX, ...
    ReduceGroup   group;           // logical group: DP, TP, PP, EP, CP, or user-defined
    uint32_t      shard_bytes;
    ScalarType    dtype;
    bool          deterministic_required;
    const NumericalRecipe* recipe;  // pinned reduction order
};
```

Collective KernelNodes are content-addressable like any other. The per-vendor Mimic backend realizes the collective via its own transport (NVLink + NVSHMEM, XGMI, ICI, NeuronLink + EFA, TCP — never NCCL/RCCL/libnccom/hcoll).

### K.3 OptimizeCollectives

- **DiLoCo fold**: replace per-step all-reduce with periodic outer-step all-reduce (configurable H interval)
- **Hierarchical**: fabric-tier-aware (intra-node every step / inter-node every N / cross-datacenter every M)
- **Selective sync**: skip small-delta parameter updates (60%+ bandwidth savings)
- **Compressed pseudo-gradients**: top-K + int8 quantization (50-100× reduction)
- **Algorithm selection**: ring / tree / halving-doubling / hierarchical — per-collective choice based on message size + topology. The choice is recorded in CollectiveAttrs; Mimic's per-vendor implementation honors it.

### K.4 TopologyAware

Re-route based on Meridian's measured N×N matrix. Degraded links avoided; hot paths preferred. CNTP's routing layer handles this at runtime based on the static placement Phase K committed.

---

## 17. Phase L — VALIDATE

Continuous, background, sample-based. Not a compile phase; a runtime observer that drives Phase H recompiles when drift exceeds threshold.

Budget: **10ms per sample at 1% default sampling rate**. Negligible runtime impact.

### L.1 ProbeCounters

Per-vendor hardware counter probe:

```cpp
Measurements mimic::probe_counters(
    fx::Bg, const CompiledKernel& k, const TargetCaps& caps);
```

Mimic's per-vendor backend wraps the native profiling interface (NVIDIA uses CUPTI equivalent; AMD uses rocprof; TPU uses its PJRT profiler; Trainium uses neuron-profile; CPU uses perf_event_open). Returns a vendor-agnostic `Measurements` struct: `cycles, ipc, pipe_utilization[], cache_hit_rates[3], dram_bw, stall_reasons[12], peak_regs, peak_local_bytes`.

### L.2 CompareToModel

For each sampled kernel, compare measured against Mimic's stored prediction (recorded at compile time inside `CompiledKernel`). Compute per-signal residuals.

### L.3 DriftDetect

Running P95 of residuals per signal per chip. Trigger recalibration when P95 > 10% for >N consecutive samples. Recalibration is `mimic::recalibrate(caps)` — per-vendor microbenchmark sweep that updates `TargetCaps` constants.

### L.4 RegressionDetect

Cross-iteration consistency: same kernel on same hardware should produce same timing within tolerance. Deviations indicate thermal throttling, ECC errors, clock-domain changes, or firmware drift. Log to Augur; let Canopy decide whether to reshard away from the unhealthy node.

### L.5 CacheInvalidate

When hardware state changes materially (detected by L.4 or by Crucible health monitoring), invalidate affected L3 cache entries. Next execution triggers Phase H recompile. L2 cache entries may survive (same IR003* still valid); L1 cache entries definitely survive (IR002 portable).

**Closed loop**: Phase L drives Phase H recompiles for drifted kernels. The compiler stays accurate over the lifetime of the hardware.

---

## 18. IR002 Specification

IR002 is Forge's portable kernel IR. It sits between op-level IR001 and vendor-specific IR003* owned by Mimic. Each `KernelNode` is a matched template (GEMM, ATTENTION, NORM, ...) with concrete dtype configuration, committed semantic layout, pinned numerical recipe, and a tile spec that may be concrete or symbolic-with-range. IR002 is vendor-agnostic: the same IR002 kernel produces numerically-equivalent results on any supported chip.

### 18.1 KernelKind — 21 kernel families

```cpp
enum class KernelKind : uint8_t {
    // Matrix / tensor ops
    GEMM,                  // 2D × 2D matmul
    BMM,                   // batched 3D × 3D
    CONV,                  // 1D / 2D / 3D convolution
    DEQUANT_GEMM,          // INT4 / FP8 dequant fused with matmul

    // Attention family
    ATTENTION,             // standard SDPA with flash recurrence
    PAGED_ATTN,            // page-table-indirect KV gather
    RAGGED_ATTN,           // THD-layout packed variable-length

    // Normalization / activation / pointwise
    NORM,                  // LayerNorm / RMSNorm / BatchNorm / GroupNorm / InstanceNorm
    SOFTMAX,               // online or naive recurrence per recipe
    POINTWISE,             // arbitrary elementwise chain (holds ComputeBody)

    // Reductions and scans
    REDUCE,                // sum / mean / max / min / argmax / argmin / topk
    SCAN,                  // prefix scan (cumsum / cumprod / assoc-scan)

    // Indexing
    GATHER_SCATTER,
    EMBEDDING,             // lookup + optional pool

    // Stochastic
    RNG,                   // Philox-seeded

    // Distributed
    COLLECTIVE,            // allreduce / allgather / reduce_scatter / all-to-all / ...

    // Recurrent / state-space
    SSM,                   // Mamba / Mamba-2 SSD / RWKV / RetNet / xLSTM

    // Composition / fusion
    FUSED_COMPOUND,        // kernel-kind-level fusion (GEMM + activation + bias, etc.)
    MOE_ROUTE,             // top-k routing + permute + grouped GEMM

    // Optimizer (new — holds per-parameter update body as extension point)
    OPTIMIZER,             // Adam / Lion / Muon / user-defined via update_body

    // Escape hatches
    OPAQUE_EXTERN,         // link against a precompiled library kernel (vendor-neutral name)
    CUSTOM,                // wraps IR001 nodes directly; backend emits a generic loop nest

    COUNT                  // = 22
};
```

No vendor opcodes. `ATTENTION` is "scaled-dot-product-attention on these tensors," period. The backend decides whether to realize it as FlashAttention-3 on Hopper, Efficient Attention on AMD, or bespoke MXU sequencing on TPU. The distinct `OPTIMIZER` kind carries the update rule as an extension-point `ComputeBody*` (see §18.3); Adam, AdamW, Lion, Muon, Shampoo, SOAP, and user-defined variants all share the same structural kernel (per-parameter iteration, state read/update/write) with the arithmetic supplied per-variant.

### 18.2 KernelNode — 64B, one cache line

```cpp
CRUCIBLE_STRONG_ID(KernelId);
CRUCIBLE_STRONG_HASH(KernelContentHash);
CRUCIBLE_STRONG_HASH(RecipeHash);

struct alignas(64) KernelNode {
    // ── Identity (8B) ─────────────────────────────
    KernelId     id;                                          // 4B
    KernelKind   kind        = KernelKind::CUSTOM;            // 1B
    uint8_t      flags       = 0;                             // 1B  (DEAD/VISITED/FUSED/...)
    int8_t       device_idx  = -1;                            // 1B
    uint8_t      ndim        = 0;                             // 1B  (rank of primary output)

    // ── Dtype + semantic layout (8B) ──────────────
    ScalarType   in_dtype    = ScalarType::Undefined;         // 1B
    ScalarType   out_dtype   = ScalarType::Undefined;         // 1B
    Layout       layout_in   = Layout::Strided;               // 1B  (Strided/RowMajor/ColMajor/Tiled/Swizzled/Broadcast)
    Layout       layout_out  = Layout::Strided;               // 1B
    uint16_t     num_inputs  = 0;                             // 2B
    uint16_t     num_outputs = 0;                             // 2B

    // ── Kind-specific attrs (8B) ──────────────────
    void*        attrs       = nullptr;                        // GemmAttrs* / AttentionAttrs* / ...
                                                                // cast by `kind`, arena-allocated

    // ── Pinned numerical recipe (8B) ──────────────
    const NumericalRecipe* recipe = nullptr;                   // interned in RecipePool

    // ── Tile spec (8B) ────────────────────────────
    const TileSpec*        tile   = nullptr;                   // interned in TilePool

    // ── I/O slots (16B) ───────────────────────────
    SlotId*      input_slots  = nullptr;                       // arena array, num_inputs entries
    SlotId*      output_slots = nullptr;                       // arena array, num_outputs entries

    // ── Provenance + hash (8B) ────────────────────
    KernelContentHash content_hash;                            // 8B  (memoized)
};
static_assert(sizeof(KernelNode) == 64);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(KernelNode);
```

### 18.3 Per-kind attribute structs

Arena-allocated, pointed to via `KernelNode::attrs`, cast based on `kind`. Each attrs struct has two classes of fields:

- **Structural fields** (shape, layout, dtype, algorithmic variant): drive MAP-Elites tile search and vendor-specific lowering. Fixed per-kernel-template per-target.
- **Extension-point fields** (`ComputeBody*` pointers): user-supplied IR001 fragments that inline at IR002→IR003* lowering. Realize the FlexAttention pattern generalized to every template. See §18.7.

```cpp
struct GemmAttrs {
    // Structural
    const Expr* m;                 // concrete or symbolic-with-range
    const Expr* n;
    const Expr* k;
    ScalarType  a_dtype;
    ScalarType  b_dtype;
    Layout      a_layout;          // Row / Col / Tiled
    Layout      b_layout;
    uint8_t     transpose_out : 1;
    uint8_t     has_bias      : 1;
    uint8_t     split_k       : 1;
    uint8_t     accumulate    : 1;  // D = A@B + D rather than D = A@B
    uint8_t     pad_bits      : 4;
    uint8_t     pad_rest[3]{};

    // Extension points
    const ComputeBody* prologue_body = nullptr;  // applied to A or B tile post-load, pre-MMA
    const ComputeBody* epilogue_body = nullptr;  // applied to accumulator tile post-MMA
    const ComputeBody* bias_body     = nullptr;  // custom bias fusion (default: add)
};
static_assert(sizeof(GemmAttrs) == 64);

struct AttentionAttrs {
    // Structural
    const Expr* batch;
    const Expr* seq_q;
    const Expr* seq_kv;
    const Expr* head_dim;
    uint16_t    num_heads;
    uint16_t    num_kv_heads;       // GQA / MQA
    uint8_t     causal       : 1;
    uint8_t     paged        : 1;
    uint8_t     block_mask   : 1;
    uint8_t     ragged       : 1;
    uint8_t     ring         : 1;   // CP / ring attention variant
    uint8_t     pad_bits     : 3;
    uint8_t     window_size;        // 0 = full attention
    ScalarType  qk_dtype;
    ScalarType  v_dtype;
    uint8_t     algorithm;          // FLASH2 / FLASH3 / PAGED / RING / STREAMING / LINEAR / RETENTION
    uint8_t     pad_rest[7]{};

    // Extension points (the FlexAttention generalization)
    const ComputeBody* q_preproc_body     = nullptr;  // applied to loaded Q tile (e.g. RoPE)
    const ComputeBody* k_preproc_body     = nullptr;  // applied to loaded K tile
    const ComputeBody* score_mod_body     = nullptr;  // (score, b, h, q_idx, kv_idx) → score'
    const ComputeBody* mask_mod_body      = nullptr;  // (b, h, q_idx, kv_idx) → bool
    const ComputeBody* softmax_recurrence = nullptr;  // custom online accumulator
    const ComputeBody* value_accum_body   = nullptr;  // runs during V accumulation
    const ComputeBody* output_post_body   = nullptr;  // applied to output tile post-reduction
};

struct ConvAttrs {
    // Structural
    const Expr* n, *c_in, *c_out, *h, *w;
    uint8_t     kh, kw, kd;          // kernel dims (kd=0 for 2D)
    uint8_t     stride_h, stride_w, stride_d;
    uint8_t     pad_h, pad_w, pad_d;
    uint8_t     dil_h, dil_w, dil_d;
    uint8_t     groups;
    uint8_t     layout;              // 0=NHWC, 1=NCHW, 2=NDHWC, ...
    uint8_t     transpose : 1;
    uint8_t     has_bias  : 1;
    uint8_t     pad_bits  : 6;
    uint8_t     pad_rest[2]{};

    // Extension points
    const ComputeBody* prologue_body = nullptr;
    const ComputeBody* epilogue_body = nullptr;
};

struct NormAttrs {
    // Structural
    uint8_t     kind;                // 0=LayerNorm, 1=RMSNorm, 2=BatchNorm, 3=GroupNorm, 4=InstanceNorm
    uint8_t     axis;
    uint8_t     num_groups;           // group norm only
    uint8_t     affine : 1;
    uint8_t     pad_bits : 7;
    uint32_t    eps_bits;             // f32 bitcast
    const Expr* channels;
    const Expr* reduction_size;

    // Extension points
    const ComputeBody* stat_compute_body = nullptr;  // default: mean+variance (for LayerNorm)
    const ComputeBody* normalize_body    = nullptr;  // default: (x - mean) / sqrt(var + eps)
    const ComputeBody* affine_body       = nullptr;  // default: gamma*x + beta
};

struct ReduceAttrs {
    // Structural: axes bitmap, keepdim
    uint32_t    axes_mask;
    uint8_t     keepdim : 1;
    uint8_t     pad_bits : 7;
    uint8_t     pad_rest[3]{};

    // Extension points (default: add / mul / max / min via built-in enum)
    const ComputeBody* reduce_op_body = nullptr;  // custom (a, b) → c
    const ComputeBody* init_body      = nullptr;  // custom identity value
    const ComputeBody* finalize_body  = nullptr;  // applied to reduced value (e.g. sqrt for L2 norm)
};

struct ScanAttrs {
    uint8_t     axis;
    uint8_t     exclusive : 1;
    uint8_t     reverse   : 1;
    uint8_t     pad_bits  : 6;
    uint8_t     pad_rest[2]{};

    // Extension points
    const ComputeBody* assoc_op_body = nullptr;  // default: add; custom (a, b) → c for assoc scan
    const ComputeBody* init_body     = nullptr;  // custom identity
};

struct PointwiseAttrs { const ComputeBody* body; };  // body IS the kernel's content

struct EmbeddingAttrs {
    const Expr* vocab_size;
    const Expr* embed_dim;
    ScalarType  weight_dtype;
    uint8_t     pool_kind;      // 0=none, 1=sum, 2=mean, 3=max
    uint8_t     pad_rest[2]{};

    const ComputeBody* lookup_body     = nullptr;  // default: table load
    const ComputeBody* aggregation_body = nullptr;  // default: pool_kind
};

struct SsmAttrs {
    const Expr* batch, *seq_len, *channel, *state_dim;
    uint8_t     kind;   // 0=Mamba, 1=Mamba2-SSD, 2=RWKV, 3=RetNet, 4=xLSTM
    uint8_t     pad_rest[3]{};

    const ComputeBody* state_transition_body = nullptr;  // h' = f(h, x)
    const ComputeBody* output_body           = nullptr;  // y = g(h)
    const ComputeBody* selective_gate_body   = nullptr;  // Mamba's selective gate
};

struct RngAttrs {
    uint8_t     distribution;    // 0=uniform, 1=normal, 2=bernoulli, 3=custom
    uint8_t     pad_rest[3]{};

    const ComputeBody* distribution_body = nullptr;  // custom transform on Philox bits
};

struct CollectiveAttrs {
    // Structural
    CollectiveOp op;
    ReduceOp     reduce;
    ReduceGroup  group;
    uint32_t     shard_bytes;
    ScalarType   dtype;
    uint8_t      deterministic_required : 1;
    uint8_t      algorithm : 7;           // RING / TREE / HD / HIERARCHICAL / INNETWORK_OFFLOAD
    uint32_t     bucket_size_bytes;        // for bucketed async; 0 = not bucketed
    uint32_t     timeout_ms;               // for in-flight failure detection

    const NumericalRecipe* recipe;         // pinned reduction order

    // Extension points
    const ComputeBody* custom_reduce_body = nullptr;  // user-defined reduce op (e.g. quantile)
};

struct MoeRouteAttrs {
    const Expr* n_tokens;
    const Expr* n_experts;
    const Expr* expert_capacity;
    uint8_t     top_k;
    uint8_t     pad_rest[3]{};

    const ComputeBody* routing_body   = nullptr;  // gate_logits → top_k routes
    const ComputeBody* capacity_body  = nullptr;  // capacity-factor enforcement
};

struct OptimizerAttrs {
    uint8_t     kind;       // 0=Adam, 1=AdamW, 2=Lion, 3=Muon, 4=Shampoo, 5=SOAP, 6=custom
    uint8_t     pad_rest[3]{};

    // Structural hyperparameters (const Expr* for symbolic values)
    const Expr* lr;
    const Expr* beta1;
    const Expr* beta2;
    const Expr* eps;
    const Expr* weight_decay;

    // Extension points — the update rule itself
    const ComputeBody* update_body      = nullptr;  // (p, g, s, hparams) → (p', s')
    const ComputeBody* state_init_body  = nullptr;  // state initialization per param
    const ComputeBody* preconditioner   = nullptr;  // Shampoo/SOAP matrix preconditioner
};
```

Each attrs struct is ≤128B when extension-point fields are included, trivially relocatable, arena-allocated. Extension-point fields default to `nullptr`; the lowering pass uses the structural variant unless a body is supplied.

#### 18.3.1 ExecutionAttrs — warp specialization and SM assignment

Every kernel attrs struct carries an 8-byte ExecutionAttrs tail controlling warp-role split, register allocation, and SM partitioning. This realizes the DeepSeek-V3 pattern (CRUCIBLE.md §14.9) as a first-class compile output.

```cpp
enum class RegAllocPolicy : uint8_t {
    STATIC = 0,               // compile-time reg count, all warps equal
    DYNAMIC_SETMAXNREG = 1,   // runtime setmaxnreg rebalancing (NV sm_90+; vendor analogs)
};

struct ExecutionAttrs {
    uint8_t         warp_spec_split;    // values from MAP-Elites axis:
                                         //   0x00 = 1-0-0 (no spec)
                                         //   0x01 = 2-0-0
                                         //   0x02 = 0-0-4 (all consumer)
                                         //   0x03 = 1-1-2 (one-producer one-barrier two-consumer)
                                         //   0x04 = 2-1-1
                                         //   0x05 = 2-2-0
                                         //   0x06 = 1-2-1
                                         //   0x07 = 2-1-2
    RegAllocPolicy  reg_alloc_policy;
    uint16_t        sm_mask_handle;      // index into per-Keeper green-context table:
                                         //   0 = compute context (default)
                                         //   1 = dispatch context
                                         //   2 = combine context
                                         //   3 = scheduler context
    uint16_t        regs_per_producer;   // relevant when reg_alloc_policy == DYNAMIC_SETMAXNREG
    uint16_t        regs_per_consumer;
};
static_assert(sizeof(ExecutionAttrs) == 8);
```

Added as the last 8 bytes of every kind-specific attrs struct. `GemmAttrs` grows from 64B to 72B; `AttentionAttrs` from ~48B to 56B. Trivial relocatability preserved.

**Defaults**:
- `warp_spec_split = 0x00` — no specialization, baseline single-role kernel
- `reg_alloc_policy = STATIC` — register count fixed at compile time
- `sm_mask_handle = 0` — compute context, standard dispatch
- `regs_per_producer / regs_per_consumer` — ignored when policy is STATIC

**MAP-Elites integration**. `warp_spec_split` × `reg_alloc_policy` × `(regs_per_producer, regs_per_consumer)` extend the Mimic MAP-Elites behavior grid (MIMIC.md §19). Mutation operators: `SWAP_WARP_SPEC_SPLIT`, `TOGGLE_REG_ALLOC_POLICY`, `ADJUST_PRODUCER_REGS ± 16`, `ADJUST_CONSUMER_REGS ± 16`.

**Lowering**. Phase J emits per-attr metadata; per-vendor backend realizes via its native primitive:

- NV (sm_90+): `setmaxnreg.inc.sync.aligned.u32 N` at consumer-warpgroup entry, `.dec.sync.aligned.u32 N` at producer-warpgroup exit. Detailed in MIMIC.md §15.5.
- AMD (CDNA3+): `s_setreg` REGMAP equivalents at warp-role boundaries
- TPU: scalar processor manages execution-unit assignment per dispatch; no user-visible equivalent of setmaxnreg
- Trainium: per-engine assignment in NEFF metadata; no PTX-equivalent primitive

**SM mask resolution**. `sm_mask_handle` indexes into the per-Keeper green-context table (CRUCIBLE.md §14.9). Runtime `resolve_plan()` translates handle → channel/queue id + SM partition mask. Handle 0 (compute context) is the default; non-zero handles require green-context init at Keeper startup.

**Fitness impact** on FlashAttention-3 benchmark, H100:

| Config | Measured MFU |
|---|---|
| `warp_spec = 0x00, reg_alloc = STATIC` | 52% |
| `warp_spec = 0x03, reg_alloc = STATIC` (128 regs all warps) | 61% |
| `warp_spec = 0x03, reg_alloc = DYNAMIC_SETMAXNREG` (40 prod / 240 cons) | 76% |

Dynamic register rebalancing contributes ~+15 MFU points on MMA-heavy kernels. MAP-Elites finds the optimal `N_prod`/`N_cons` split for each (shape, chip) combo.

### 18.4 TileSpec — 32B, interned

```cpp
struct alignas(32) TileSpec {
    const Expr* dims[4]            = {nullptr, nullptr, nullptr, nullptr};
                                          // concrete INTEGER or SYMBOL with range in SymbolTable
    uint16_t    threads_per_cta    = 0;    // abstract parallelism budget
    uint16_t    regs_per_thread    = 0;    // estimate, refined by Mimic
    uint32_t    shared_bytes       = 0;    // abstract fast-local-storage budget
    uint32_t    pipeline_depth     = 0;    // abstract stage count
    uint16_t    flags              = 0;    // persistent, split_k, streaming, ...
    uint64_t    hash               = 0;    // for interning in TilePool
};
static_assert(sizeof(TileSpec) == 32);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TileSpec);
```

`dims[i]` is vendor-neutral — never carries WGMMA shape tables, MFMA intrinsic shapes, or MXU native sizes. The per-vendor backend maps abstract tiles to native hardware tiles internally during IR002→IR003* lowering.

### 18.5 KernelGraph

Analog of `Graph` at the IR002 level, arena-owned:

```cpp
class CRUCIBLE_OWNER KernelGraph {
public:
    explicit KernelGraph(fx::Alloc, ExprPool*, SymbolTable*);
    KernelGraph(const KernelGraph&)            = delete("arena ownership");
    KernelGraph(KernelGraph&&)                  = delete("interior pointers would dangle");

    // Factories, one per KernelKind family
    [[nodiscard]] KernelNode* add_gemm(fx::Alloc, GemmAttrs*, ...);
    [[nodiscard]] KernelNode* add_attention(fx::Alloc, AttentionAttrs*, ...);
    [[nodiscard]] KernelNode* add_pointwise(fx::Alloc, PointwiseAttrs*, ...);
    [[nodiscard]] KernelNode* add_collective(fx::Alloc, CollectiveAttrs*, ...);
    [[nodiscard]] KernelNode* add_custom(fx::Alloc, std::span<const GraphNode* const>);
    // ... one per kind with parameters

    // Transforms
    void                   eliminate_dead_kernels();
    void                   topological_sort(fx::Alloc);
    [[nodiscard]] uint32_t fuse_compound_kernels(fx::Alloc);   // GEMM + POINTWISE epilogue → FUSED_COMPOUND
    [[nodiscard]] uint32_t eliminate_common_kernels(fx::Alloc);// kernel-level CSE via content_hash

    // Content hash: composes with IR001 region hashes
    [[nodiscard]] KernelContentHash compute_hash() const;

private:
    Arena         arena_;
    ExprPool*     pool_;
    SymbolTable*  tab_;
    KernelNode**  nodes_;
    uint32_t      num_nodes_;
    uint32_t      capacity_;
    RecipePool    recipes_;
    TilePool      tiles_;
    AttrsPools    attrs_;                                      // one arena-backed pool per kind
};
```

Same axiom discipline as `Graph`: InitSafe NSDMI, TypeSafe strong IDs, MemSafe arena-only, non-copyable/non-movable.

### 18.6 IR002 content hashing

```
KernelContentHash = hash(
    kind,
    attrs_hash(kind, attrs),                      // includes body_hashes[] for extension points
    recipe->hash,
    tile->hash,
    input_slot_types[],
    output_slot_types[],
    layout_in, layout_out,
    parent_ir001_region_content_hashes
)

attrs_hash(kind, attrs):
    structural_hash    = hash(structural fields of attrs)
    body_hashes[]      = [ body->content_hash for body in extension_point_fields(attrs) ]
    return combine(structural_hash, body_hashes)
```

Composes with IR001 `ContentHash` so the full chain is: `hash_ir001 → hash_ir002 → hash_ir003*`. This is the three-level cache key (see §23).

Extension-point bodies are IR001 `ComputeBody` fragments, interned in the ExprPool. Two researchers who write identical `score_mod` implementations (e.g., ALiBi with slope 0.1) produce byte-identical body hashes, the same `KernelContentHash`, and share the same L3 cache entry. Tweaking a constant (slope 0.1 → 0.08) produces a new body hash and a new L3 entry; L1 (IR002 snapshot) is federation-compatible because bodies remain vendor-neutral.

### 18.7 Extension-point semantics — structure and content as orthogonal axes

Every templated kernel is two orthogonal decisions: *structural* (tile sizes, pipeline depth, warp specialization, memory layout, reduction topology) and *content* (the arithmetic that runs inside the tile loop). Traditional ML compilers conflate them, so every new research variant (FlexAttention, custom norm, novel optimizer) either degrades to a slow generic path or demands a hand-written kernel. IR002 separates the two.

#### 18.7.1 The split

| Axis | Owner | Search space | Cache level |
|---|---|---|---|
| Structural (tile shape, pipeline, memory layout) | Mimic per-vendor backends | MAP-Elites × MAP-Elites behavior grid × hardware | L2 (IR003* snapshot) cross-chip within vendor family; L3 (bytes) per-chip |
| Content (extension-point `ComputeBody*`) | User (captured from Python) | Interned via ExprPool; structurally equal bodies share a hash | L1 (IR002 snapshot) cross-vendor; L2 intra-vendor; L3 per-chip |

Mimic's backends own the structural optimization for each `KernelKind`. The ATTENTION template in `mimic/nv/` for Hopper knows about WGMMA m64n128k16, TMA async bulk copy, warp specialization, online softmax with FP32 LSE, per-tile output rescaling. MAP-Elites searches over `(tile_q, tile_kv, pipeline_stages, warp_spec_split, softmax_variant, shared_mem_swizzle, reg_allocation_strategy)` per specific `(shape, chip)`. All this structural machinery is shared across every attention variant regardless of the user's `score_mod`.

The user supplies the content via extension-point bodies. The bodies inline at IR002→IR003* lowering, fuse with the structural kernel code via peephole, and participate in register allocation alongside it. No function-call overhead, no indirect dispatch, no partial vectorization — the body becomes part of the straight-line code the vendor backend emits.

#### 18.7.2 Inlining discipline

Every extension-point body is an IR001 `ComputeBody` fragment: arena-allocated, content-hashed, free of side effects, free of control flow (arithmetic + `where` / `select` only). Per-backend lowering (MIMIC.md §40.X):

```
Per KernelNode (for each extension point present):
    1. Translate body's IR001 micro-ops to IR003* instructions
       (MUL / ADD / FMA / WHERE / SELECT / CAST / SIN / COS / EXP / LOG / ...)
    2. Splice the translated instructions into the appropriate point in the
       structural tile loop (pre-MMA, post-MMA, softmax recurrence, etc.)
    3. Run peephole: fuse MUL+ADD to FMA; merge constants; eliminate dead moves
    4. Run register allocation over the composite kernel's live ranges
    5. Run instruction scheduler over the composite's dependency graph
```

The body's cost is captured in Mimic's three-tier simulator (fast/medium/accurate) identically to structural-kernel cost. MAP-Elites fitness takes the body into account when ranking candidates.

#### 18.7.3 Identity, equivalence, and caching

Two kernels with identical structural attrs and identical body hashes produce identical `KernelContentHash` values. Consequences:

- Kernel-level CSE (Phase C.5, Phase E.5) deduplicates them.
- L1 cache (IR002 snapshot) entries are reused across sites that compile the same variant.
- L2 cache (IR003* snapshot) entries are reused across chips within the same vendor family.
- L3 cache (compiled bytes) is per-chip, but content-addressing means compile work is proportional to *distinct* (structure, body) pairs, not per-call-site.

Structurally similar bodies (same IR001 micro-op topology, different constants) have different hashes but can warm-start MAP-Elites from each other's archives. MIMIC.md §19 documents body-hash-similarity metrics (graph edit distance on the `ComputeBody` tree) used to find the nearest archive to warm-start from. Typical warm-start cuts MAP-Elites generations by 5-10× for a novel-constant variant of a familiar-structure body.

#### 18.7.4 Interaction with peephole and register allocation

Extension-point bodies interact with peephole rules at the IR003* level. Representative rewrites:

- **Body MUL + structural ADD → FMA.** A `score_mod` body that multiplies the score by a bias tensor, followed by the structural softmax's exp preprocessing, may collapse to a single FMA on vendors that expose fused multiply-add at the correct precision.
- **Body WHERE → predicated instruction.** A `mask_mod` body emitting `where(mask, score, -inf)` collapses on NV to a predicated MUFU.RSQ + ISETP pattern rather than materializing the -inf constant.
- **Body CAST fusion.** A body that casts its input to FP32 for the computation and back to FP16 may elide the casts when the surrounding structural kernel already holds the value in FP32 (accumulator register).

Register allocation treats body live ranges alongside structural live ranges. On Hopper with 64 warps per SM and 255 regs per thread, a body requiring 8 scratch registers reduces occupancy if structural pressure was already at the limit. MAP-Elites behavior-axis "regs_per_thread" captures this; mutations that reduce structural reg pressure are preferred when body pressure is high.

#### 18.7.5 Failure modes

An extension-point body that violates template assumptions fails at capture with a diagnostic:

- **Cross-tile dependency in `score_mod`**: the body references a value outside the current (q_idx, kv_idx) tile. Rejected; user must express the pattern as a separate kernel.
- **Unbounded recursion in a body**: rejected; bodies must be pure straight-line `ComputeBody` fragments.
- **Incompatible dtype**: the body's output dtype must match the extension point's declared contract (e.g., `score_mod` must return the same dtype as the score). Explicit cast required from the user.
- **Cost exceeds the structural kernel's per-tile budget**: the body would spill registers or exceed L1 cache. Lowering succeeds but MAP-Elites may prefer candidates with reduced tile dimensions to accommodate the body; fitness reflects the adaptation cost.

#### 18.7.6 What the pattern unlocks

FlashAttention-class perf for any attention variant expressible as extension-point bodies: ALiBi, sliding window, RoPE at Q/K preprocessing, custom masking, causal × sliding combinations, speculative-decoding acceptance, Linformer-style low-rank projections, multi-query / grouped-query, cross-attention with custom scoring. Target MFU on Hopper:

| Pattern | Baseline (vendor lib) | Crucible template + user body |
|---|---|---|
| FlashAttention-3 standard causal | 75% | 72-78% |
| FlashAttention + ALiBi bias | 55% (falls off fast path) | 72-78% |
| Sliding window 512 | 65% | 72-78% |
| Custom mask (user-defined) | no fast path (graph break) | 72-78% |
| Speculative decoding verify | 40% (separate kernels) | 65-72% |

Same pattern applies to NORM, REDUCE, SCAN, EMBEDDING, SSM, OPTIMIZER: structural speed via the Mimic backend's MAP-Elites-tuned template, arbitrary content via the user's `ComputeBody`.

#### 18.7.7 Separation from `CUSTOM` and `OPAQUE_EXTERN`

`CUSTOM` kind wraps IR001 nodes directly when no template matches. The backend emits a generic loop nest; perf is 20-40% MFU typically. Reserved for genuinely novel patterns that don't match any template family.

`OPAQUE_EXTERN` links against a precompiled external kernel by vendor-neutral name. Bypasses Forge optimization entirely. Reserved for third-party libraries where the signature is exposed but the implementation is opaque.

Extension-point bodies are neither; they are the primary path for research flexibility without perf loss.

### 18.8 PatchPoint taxonomy

Every runtime-mutable value in an ExecutionPlan is a typed, named PatchPoint. Mutation is one atomic byte-width write at a known offset; no plan recomposition, no kernel recompile. The runtime view of PatchPoints appears in CRUCIBLE.md §11.9; this section specifies the compile-time contract that Forge Phase J honors when emitting pushbuffer.

#### 18.8.1 Kinds and widths

```cpp
enum class PatchKind : uint8_t {
    SCALAR,              // scalar constant in c[0]: lr, dropout_p, temperature
    SLOT_PTR,            // absolute VRAM address for a memory-plan slot
    SHAPE_DIM,           // runtime shape dim for a parametric kernel
    RNG_COUNTER,         // Philox counter base
    CONDITIONAL,         // BranchNode selection predicate
    COUNTER_BUMP,        // LoopNode iteration counter
    SEMAPHORE_VALUE,     // ChainEdge or barrier threshold
    EVENT_TENSOR,        // multi-dim counter array, data-dependent sync
    COUNT
};

struct alignas(32) PatchPoint {
    const char*         name;                 // interned, user-visible
    PatchPointHash      hash;                  // identity (32B-aligned)
    uint32_t            pushbuffer_offset;     // byte offset in pushbuffer_bytes
    uint16_t            width_bytes;           // per-element width
    PatchKind           kind;
    uint8_t             dim_count;             // 0 for scalar kinds; N for EVENT_TENSOR
    const uint16_t*     dim_strides;           // arena, byte strides per dim
    const uint32_t*     dim_extents;           // arena, extent per dim
};
static_assert(sizeof(PatchPoint) == 32);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(PatchPoint);
```

Interned in PatchPointPool per Plan; attached to the ExecutionPlan as `std::span<const PatchPoint>`.

#### 18.8.2 Per-kind semantics

**SCALAR** — a constant in the kernel's c[0] constant buffer. Width ∈ {1, 2, 4, 8}. Patch semantics: one atomic write of scalar bytes at `pushbuffer_offset`. Hyperparameters (`learning_rate`, `beta1`, `eps`), per-op constants (`dropout_p`, `norm_eps`), flags. Width must match the kernel's declared type; mismatch → Phase J compile-time error.

**SLOT_PTR** — 8-byte pointer to a memory-plan slot. Populated by `runtime::resolve_plan(plan, pool_base)` at Keeper init; all subsequent patches use the absolute address. Unlike other kinds, SLOT_PTR is written once at plan load and rarely mutated thereafter (only on pool rebind, e.g., reshard after membership change).

**SHAPE_DIM** — 4-byte runtime shape dim for a parametric kernel (§F.4). The kernel reads it via `SEND_PCAS` indirection through c[0]. One SFENCE-protected patch per dim. Parametric kernels tolerate 1-3% arithmetic overhead vs specialized variants in exchange for dynamic-shape flexibility.

**RNG_COUNTER** — 8-byte Philox counter base. Per CRUCIBLE.md §10.3, each RNG draw is `philox(seed_key, counter_base + op_idx*2^16 + thread_idx)`. The counter_base patch advances per step (`counter_base = seed + step_idx * 2^32`); deterministic from checkpoint state. Under `BITEXACT_*` recipes, this patch MUST be written before submission — guards enforce.

**CONDITIONAL** — 4-byte predicate for a BranchNode. Written by the producer of the predicate value (typically a tiny reduce-or-eval kernel on SM) or by the host. The BranchNode's pushbuffer reads the predicate into a GPU register and conditional-skips one arm. Both arms are emitted; the runtime selects via patch.

**COUNTER_BUMP** — 4-byte LoopNode iteration counter. Patched before the loop's pushbuffer entry; the loop's micro-kernel decrements each iteration; host engine jumps back while counter > 0. Combined with `UNTIL(predicate)` variant, CONDITIONAL + COUNTER_BUMP can convert a count loop into a convergence loop mid-plan.

**SEMAPHORE_VALUE** — 4 or 8-byte threshold for a ChainEdge or intra-plan barrier. Patched per-step to advance the epoch; device-side `SEM_ACQUIRE ACQ_CIRC_GEQ` (NV) / `WAIT_MEM_ZERO` (AMD) waits until the memory location reaches the patched value. Crucial for Plan chaining without host round-trips (§J.5).

**EVENT_TENSOR** — multi-dimensional counter array for data-dependent dependencies, following the Event Tensor pattern (Jin et al. 2026). Patch value is an `std::span<int32_t>` of shape `[extent[0], extent[1], ...]`; runtime writes strided per `dim_strides`. Consumers issue device-side atomic decrements (`notify()`) and spin-waits (`wait()`). Primary use cases: MoE expert routing (update per-expert task count by token-routing decisions), speculative decoding (update acceptance-tree dependencies), iterative reasoning (update convergence-count per branch), dynamic agentic control flow.

#### 18.8.3 Content hashing

A patched Plan is a new content-addressed entity:

```
plan_hash_patched = hash(plan_hash_base, patch_manifest.hash)
patch_manifest.hash = hash((pp.hash, pp_value_bytes) for pp in patches applied)
```

Cipher stores Plans keyed by `plan_hash`. Patching produces a new hash; both patched and unpatched versions coexist. Re-submitting the same `(base_plan, patches)` hits cache at ~30ns lookup. For training loops where only `RNG_COUNTER` and `SEMAPHORE_VALUE` change per step, effective cache miss rate is zero after first submission.

#### 18.8.4 Emission discipline

Forge Phase J is responsible for allocating PatchPoint offsets:

1. During pushbuffer composition (§J.6), Mimic's per-vendor backend emits methods referencing c[0] offsets for scalars, runtime-variable operands for parametric kernels, and semaphore addresses for chain edges.
2. Phase J.emit_patch_points walks each kernel's mutable-value metadata and allocates a PatchPoint with the exact byte offset in pushbuffer_bytes or constbank_arena.
3. PatchPoint offsets land in the Plan's `patch_points` span; names and widths drive runtime validation.

Mimic backends MUST declare each mutable value in kernel metadata. Missing a PatchPoint declaration produces a Plan whose behavior is pinned at Phase J emission time (baking the compile-time default into pushbuffer). Under `BITEXACT_*` recipes this must not happen without explicit design — CI catches it by verifying Plan replay yields identical bytes regardless of patch values for un-patched kernels.

#### 18.8.5 Typical PatchPoint counts per KernelKind

| KernelKind | Typical PatchPoints |
|---|---|
| GEMM | 3 SLOT_PTR (A/B/D), 0-2 SHAPE_DIM, 0-2 SCALAR (alpha/beta) |
| ATTENTION | 4 SLOT_PTR (Q/K/V/O), 2 SHAPE_DIM (seq_len/batch), 1 SCALAR (softmax_scale) |
| NORM | 2 SLOT_PTR, 1 SHAPE_DIM, 2 SCALAR (eps, gamma/beta if fused) |
| COLLECTIVE | 1-2 SLOT_PTR, 1-8 SEMAPHORE_VALUE (per hop), 0-1 SHAPE_DIM |
| MOE_ROUTE | 2 EVENT_TENSOR (per-expert counts, routing map), 2-4 SLOT_PTR |
| OPTIMIZER | 3 SLOT_PTR (params/grads/state), 5 SCALAR (lr/beta1/beta2/eps/wd) |
| RNG | 1 RNG_COUNTER, 0-1 SCALAR (seed_offset) |
| LoopNode body | 1 COUNTER_BUMP, 0-1 CONDITIONAL |
| BranchNode | 1 CONDITIONAL |

A full training step plan carries 50-100 PatchPoints; all are byte-addressable writes at ~10 ns each.

---

## 19. NumericalRecipe — the portability contract

The recipe pinned on a `KernelNode` is what makes "same IR002 → same result across every backend" work. Every IR003* backend reads the recipe and realizes exactly the pinned algorithm — never a vendor library's implementation whose behavior varies by SDK version.

### 19.1 Recipe struct — 16B, interned

```cpp
enum class ReductionAlgo       : uint8_t { PAIRWISE, LINEAR, KAHAN, BLOCK_STABLE };
enum class RoundingMode        : uint8_t { RN, RZ, RM, RP };
enum class ScalePolicy         : uint8_t { NONE, PER_TENSOR_POST, PER_TENSOR_PRE,
                                            PER_BLOCK_MX, PER_BLOCK_NVFP4, PER_CHANNEL };
enum class SoftmaxRecurrence   : uint8_t { NAIVE, ONLINE_LSE, FLASH2, FLASH3 };

// Four-tier determinism. CRUCIBLE.md §10.5 documents the user-facing semantics;
// MIMIC.md §40 documents per-backend realization.
enum class ReductionDeterminism: uint8_t {
    UNORDERED,           // no guarantee; fastest; ~100 ULP cross-vendor in practice
    ORDERED,             // reduction topology pinned; ≤4 ULP cross-vendor; ~3-5% tax
    BITEXACT_TC,         // K≤8 tensor-core fragments + pinned outer scalar reduction;
                         //   0-1 ULP cross-vendor; ~5-8% tax
    BITEXACT_STRICT,     // scalar FMA chain throughout (no tensor cores);
                         //   0 ULP byte-identical on any silicon; 10-50× slower
};

struct alignas(16) NumericalRecipe {
    ScalarType           accum_dtype     = ScalarType::Float;   // 1B
    ScalarType           out_dtype       = ScalarType::Undefined; // 1B
    ReductionAlgo        reduction_algo  = ReductionAlgo::PAIRWISE; // 1B
    RoundingMode         rounding        = RoundingMode::RN;     // 1B
    ScalePolicy          scale_policy    = ScalePolicy::NONE;    // 1B
    SoftmaxRecurrence    softmax         = SoftmaxRecurrence::ONLINE_LSE; // 1B
    ReductionDeterminism determinism     = ReductionDeterminism::ORDERED; // 1B
    uint8_t              flags           = 0;                    // 1B
                                                                  //   bit 0: flush_to_zero
                                                                  //   bit 1: split_k_atomic_ok
                                                                  //   bit 2: allow_denormal
                                                                  //   bit 3: attn_mask_add_in_fp32
                                                                  //   bit 4-7: reserved
    RecipeHash           hash;                                   // 8B (computed at intern time)
};
static_assert(sizeof(NumericalRecipe) == 16);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(NumericalRecipe);
```

**Tier semantics and cost.** The four tiers define an escalation ladder; each is strictly more deterministic than the tier below and slightly more expensive:

| Level | Perf (vs UNORDERED) | Cross-vendor ULP | How a backend realizes it |
|---|---|---|---|
| `UNORDERED` | 1.00× | ≤100 (no bound) | any algorithm, any order |
| `ORDERED` | 0.95-0.98× | ≤4 | reduction topology pinned by canonical index order; tile shapes free |
| `BITEXACT_TC` | 0.92-0.96× | 0-1 | tensor cores with K≤8 fragments only; pinned outer scalar reduction |
| `BITEXACT_STRICT` | 0.02-0.10× | 0 byte-identical | no tensor cores; scalar FMA chain in pinned pairwise order |

`BITEXACT_TC` is the pragmatic sweet spot for cross-vendor training: it keeps tensor cores engaged (small perf tax) while eliminating summation-topology divergence (each K≤8 fragment has an unambiguous pairwise tree on every supported vendor). `BITEXACT_STRICT` is reserved for reference validation, legal/compliance reproducibility, and the CPU oracle.

Recipes using mixed-precision block-scaled formats (FP8 MX, FP4 MX) cannot declare `BITEXACT_*` because scale application divergence exceeds software correction. Highest available for those recipes is `ORDERED` with per-recipe `tolerance_ulp_cross_vendor`.

### 19.2 RecipePool interning

Swiss-table pool, same design as `ExprPool`. Same recipe → same pointer. Typical ML model uses 5-15 distinct recipes; `RecipePool` is ~100 entries in practice.

Pointer equality at the `const NumericalRecipe*` level is used by:
- `KernelContentHash` computation
- Kernel-level CSE (two kernels with identical attrs and identical recipe pointer → same content hash → dedupable)
- L1 cache key generation

### 19.3 What the recipe pins, per-backend realization

| Recipe field | NV backend honors by | AM backend honors by | TPU backend honors by | TRN backend honors by |
|---|---|---|---|---|
| `accum_dtype = Float` | FP32 MMA accumulator on WGMMA/tcgen05 | FP32 MFMA accumulator | f32_accum attr on MXU dot | FP32 PSUM accumulator |
| `reduction_algo = PAIRWISE` | Emits pairwise-tree reduction over K axis | Same | Same, via explicit nested HLO `reduce` | Same, via explicit Neuron reduce |
| `rounding = RN` | MUFU round-to-nearest-even | Same | Same | Same |
| `scale_policy = PER_BLOCK_MX` | MX-mode tcgen05 with explicit scale registers | MXFP8 MFMA variants | emulated if not native | scale engines before MMA |
| `softmax = ONLINE_LSE` | Online log-sum-exp in SM registers | Same, VGPR-resident | Same, via explicit HLO ops | Same, SBUF-resident |
| `determinism = ORDERED` | Pinned pairwise reduction; any TC shape; no split_k atomics | Same | Same; `xla.allow_excess_precision=false` | Same |
| `determinism = BITEXACT_TC` | Prefer WGMMA m64n128k8 (not k64); pinned outer scalar reduction; FTZ pinned | Prefer MFMA 32×32×8 (not k16); pinned outer; FTZ pinned | MXU with K=8 accumulator resets; pinned outer | PSUM with per-K=8 reduction; pinned outer |
| `determinism = BITEXACT_STRICT` | Scalar FMA only; no tensor cores; pinned pairwise tree | Same | Same | Same; CPU backend always operates in this mode |

Every backend exposes a per-vendor `realize_recipe(recipe, kernel, target) → vendor_instructions` function. The mapping is deterministic, documented in MIMIC.md per backend, and verified by the cross-vendor numerics CI (§20.4).

### 19.4 Where recipes come from

Three sources, in order of preference:

1. **User pinned**: `cr.Trainer(recipe="pairwise_f32_accum_rn")` names a recipe by registry key. Forge looks it up.
2. **Model default**: each CKernelId has a default recipe for its family. GEMM default: `pairwise_f32_accum_rn_ordered`. SOFTMAX default: `online_lse`. Etc.
3. **Fleet intersection**: if user-or-default-picked recipe isn't native on every fleet member, Forge degrades to the best intersection-native recipe (unless `fleet_policy = STRICT`).

Recipes are part of the IR002 snapshot and survive across runs via Cipher.

---

## 20. Recipe registry and fleet intersection

### 20.1 The registry file

Single source of truth: `crucible/data/recipes.json`. Checked into git, diffable.

```json
{
  "recipes": {
    "f32_strict_pairwise_rn": {
      "hash": "0x9E3779B97F4A7C15",
      "description": "FP32 scalar FMA with pairwise K-reduction, byte-identical cross-vendor",
      "accum_dtype": "Float",
      "reduction_algo": "PAIRWISE",
      "rounding": "RN",
      "scale_policy": "NONE",
      "softmax": "ONLINE_LSE",
      "determinism": "BITEXACT_STRICT",
      "flags": 0,
      "tc_shape_constraint": null,
      "native_on": [
        "nv_sm_90", "nv_sm_90a",
        "nv_sm_100", "nv_sm_100a", "nv_sm_100f",
        "am_gfx942", "am_gfx950",
        "tpu_v5p", "tpu_v5e", "tpu_v6e", "tpu_v7",
        "trn1", "trn2", "trn3",
        "cpu_x86_64_avx512", "cpu_x86_64_avx2",
        "cpu_aarch64_sve2", "cpu_aarch64_neon"
      ],
      "tolerance_ulp_cross_vendor": 0
    },
    "f16_f32accum_tc_pairwise_rn": {
      "hash": "0x5B1C3FA6C7D8E4A9",
      "description": "FP16 matmul with FP32 accumulator; tensor-core-speed cross-vendor at ≤1 ULP",
      "accum_dtype": "Float",
      "reduction_algo": "PAIRWISE",
      "rounding": "RN",
      "scale_policy": "NONE",
      "softmax": "ONLINE_LSE",
      "determinism": "BITEXACT_TC",
      "flags": 0,
      "tc_shape_constraint": {
        "require_k_le": 8,
        "nv_preferred_shape":  "m64n128k8",
        "am_preferred_shape":  "mfma_32x32x8",
        "tpu_preferred_shape": "mxu_128x128_k8",
        "trn_preferred_shape": "psum_k8"
      },
      "native_on": [
        "nv_sm_90", "nv_sm_90a", "nv_sm_100", "nv_sm_100a", "nv_sm_100f",
        "am_gfx942", "am_gfx950",
        "tpu_v5p", "tpu_v6e", "tpu_v7",
        "trn2", "trn3"
      ],
      "emulated_on": ["cpu_x86_64_avx512"],
      "tolerance_ulp_cross_vendor": 1
    },
    "f16_f32accum_ordered": {
      "hash": "0x7C15A3F2B91E6D8B",
      "description": "FP16 matmul FP32 accumulator; vendor-native tile shapes; ≤4 ULP cross-vendor",
      "accum_dtype": "Float",
      "reduction_algo": "PAIRWISE",
      "rounding": "RN",
      "scale_policy": "NONE",
      "softmax": "ONLINE_LSE",
      "determinism": "ORDERED",
      "native_on": [
        "nv_sm_90", "nv_sm_90a", "nv_sm_100", "nv_sm_100a", "nv_sm_100f",
        "am_gfx942", "am_gfx950", "tpu_v5p", "tpu_v5e", "tpu_v6e", "tpu_v7",
        "trn1", "trn2", "trn3",
        "cpu_x86_64_avx512"
      ],
      "tolerance_ulp_cross_vendor": 4
    },
    "fp8e4m3_f32accum_per_block_mx": {
      "hash": "0x3A9F1B7E8C2D6F45",
      "description": "FP8-E4M3 matmul with FP32 accumulator and per-block MX scaling",
      "accum_dtype": "Float",
      "reduction_algo": "PAIRWISE",
      "rounding": "RN",
      "scale_policy": "PER_BLOCK_MX",
      "softmax": "NAIVE",
      "determinism": "ORDERED",
      "native_on": ["nv_sm_100a", "nv_sm_100f", "nv_sm_103a", "nv_sm_103f",
                     "nv_sm_120a", "am_gfx950", "trn2", "trn3"],
      "emulated_on": ["tpu_v6e", "tpu_v7"],
      "tolerance_ulp_cross_vendor": 8,
      "note": "block-scale divergence exceeds software correction; BITEXACT tiers unavailable"
    }
  }
}
```

Each recipe: hash (identity), field values, `native_on` chip list, optional `emulated_on` list with perf tax, `tc_shape_constraint` (required for `BITEXACT_TC`), tolerance bound vs CPU reference.

The `tc_shape_constraint` field is mandatory on `BITEXACT_TC` recipes. `require_k_le: N` means the recipe may only be realized via tensor-core instructions with K ≤ N; per-vendor `*_preferred_shape` names the specific shape to emit. Mimic's per-vendor `realize_recipe` (MIMIC.md §40) honors the constraint; violators fail the cross-vendor CI (§20.4).

Typical starter registry: ~40 recipes covering the four-tier determinism × common ML patterns matrix (FP32 strict, FP16 tc, FP16 ordered, BF16 tc, BF16 ordered, FP8 ordered variants, normalization variants, deterministic vs nondeterministic all-reduce, softmax variants).

### 20.2 The fleet picker at Phase E

```cpp
const NumericalRecipe* RecipeRegistry::pick_for_fleet(
    const Constraints& c,                              // kernel kind + dtype + determinism need
    std::span<const ChipId> fleet_members,             // current Canopy membership
    FleetPolicy policy = FleetPolicy::STRICT
) const;
```

Implementation:

```
intersection = {recipe : recipe.satisfies(c) and
                         ∀ m ∈ fleet_members: m ∈ recipe.native_on}

if intersection is empty:
    if policy == STRICT:
        return diagnostic (fleet cannot satisfy recipe; bail or reshard)
    elif policy == ADAPT:
        intersection = {recipe : recipe.satisfies(c) and
                                 ∀ m ∈ fleet_members: m ∈ recipe.native_on ∪ recipe.emulated_on}
        warn

return argmin_over_intersection(estimated_cycles via mimic::fast_cost)
```

Called once per kernel at Phase E.3 RecipeSelect. Deterministic: same constraints + same fleet → same recipe.

### 20.3 Fleet membership + recipe advertising

Each Relay announces its native-recipe bitmap at Canopy join time:

```cpp
struct RelayAnnouncement {
    SourceUuid   uuid;
    ChipId       chip;                        // "nv_sm_100a", "trn2", etc.
    uint32_t     transport_endpoints_count;
    uint64_t     native_recipe_bitmap[8];     // 512-bit bitmap indexed by RecipeHash-mod-512
    uint32_t     epoch;                        // Canopy consensus epoch
};
```

Consensus layer (Raft) maintains the fleet-wide `native_recipe_intersection`. Membership changes trigger an epoch bump; recompute the intersection; invalidate kernels whose pinned recipe fell out.

### 20.4 Cross-vendor numerics CI

Build rule: every IR002 kernel template × every applicable recipe × every backend ↦ run, compare, enforce tolerance.

```
For each (KernelKind, NumericalRecipe) pair with recipe.native_on including target:
    For each backend in [nv, am, tpu, trn, cpu]:
        compile kernel on backend
        execute on real hardware (or simulator for backends without hardware access)
        record output bytes + measured cycles
    Compare pairwise:
        If determinism == BITEXACT: any byte difference → build FAILS
        If determinism == ORDERED: any output > tolerance_ulp → build FAILS
        If determinism == UNORDERED: no check (user opted in)
```

CI matrix is run pre-merge on every Forge/Mimic PR. A kernel that violates tolerance either fixes the backend or widens the recipe tolerance in the registry. Recipes silently degrading across vendors is not a thing that happens in Crucible.

---

## 21. Abstract TargetCaps

Forge sees only the abstract, vendor-neutral `TargetCaps`. The vendor-specific extension (`TargetCapsNv`, `TargetCapsAm`, `TargetCapsTpu`, `TargetCapsTrn`, etc.) is pointed to by the opaque `vendor_specific` field and cast only by Mimic's per-vendor backend.

### 21.1 Struct

```cpp
CRUCIBLE_STRONG_HASH(VendorCapsHash);

enum class VendorId : uint8_t {
    NVIDIA, AMD, GOOGLE_TPU, AWS_TRAINIUM, CEREBRAS, GRAPHCORE,
    CPU_X86_64, CPU_AARCH64, CPU_RISCV,
    UNKNOWN = 255
};

struct alignas(64) TargetCaps {
    // ── Identity (8B) ─────────────────────────────
    VendorId  vendor_id       = VendorId::UNKNOWN;
    uint8_t   chip_family;           // vendor-relative family index
    uint8_t   chip_variant;           // specific SKU within family
    uint8_t   suffix;                 // {Base, A, F} — NVIDIA; analogous on other vendors
    uint32_t  chip_id;                // global chip identifier (joins to vendor registries)

    // ── Abstract feature flags (8B) ───────────────
    uint64_t  caps_bits;              // bitmap over AbstractCap enum
    //   AbstractCap { HAS_TENSOR_CORES, HAS_ASYNC_BULK_MEM, HAS_CLUSTER_TIER,
    //                 HAS_TENSOR_FRAGMENT_STORAGE, HAS_ATOMIC_128, HAS_MX_SCALING,
    //                 HAS_NVFP4_SCALING, HAS_BLOCK_SCALING, HAS_SPARSITY_2_4,
    //                 HAS_INNETWORK_REDUCTION, HAS_PERSISTENT_STORAGE, ... }

    // ── Abstract resource budgets (16B) ───────────
    uint16_t  execution_units;        // SM / CU / MXU / NeuronCore count
    uint16_t  threads_per_exec_unit;  // warps × lanes / wavefront × lanes / etc.
    uint16_t  max_regs_per_thread;
    uint16_t  max_concurrent_threads_per_unit;
    uint32_t  fast_local_bytes;       // smem / LDS / VMEM / SBUF per exec unit
    uint32_t  tensor_fragment_bytes;  // TMEM / reg-file-for-MMA / equivalent per unit
    uint32_t  cacheable_bytes;        // L1+L2 per exec unit
    uint32_t  hbm_bytes;              // total HBM / HBM3 / HBM3e per chip

    // ── Bandwidth estimates (16B) ─────────────────
    uint32_t  hbm_bw_gbps;
    uint32_t  l2_bw_gbps;
    uint32_t  fast_local_bw_gbps;
    uint32_t  launch_overhead_us;
    uint32_t  interconnect_gbps_intra_node;
    uint32_t  interconnect_gbps_inter_node;
    uint32_t  pad0;
    uint32_t  pad1;

    // ── Hash for caching (8B) ─────────────────────
    VendorCapsHash caps_signature;   // hashes chip_id + caps_bits only (for L1 cache)

    // ── Vendor-specific extension (8B) ────────────
    const void* vendor_specific;      // Mimic's per-vendor TargetCaps<Vendor>
                                       // Forge never casts this; Mimic does.
};
static_assert(sizeof(TargetCaps) == 64);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TargetCaps);
```

### 21.2 Where the data comes from

- At startup, per-vendor `mimic::<vendor>::load_caps(chip_id)` populates both halves.
- Abstract fields: derived from the vendor-specific struct (e.g., NVIDIA's `sm_count` → `execution_units`; `smem_max_per_cta` → `fast_local_bytes`).
- Calibration fields: populated by Meridian microbenchmark runs, per-chip, stored in `crucible/mimic/<vendor>/data/<chip>.json`.

### 21.3 Forge uses abstract only

Phase D fusion cost: reads `fast_local_bytes`, `max_regs_per_thread`, `hbm_bw_gbps`, `launch_overhead_us`. Never reads `vendor_specific`.

Phase F tile constraints: reads `tensor_fragment_bytes`, `max_concurrent_threads_per_unit`, `caps_bits`. Never reads `vendor_specific`.

Phase H just passes `const TargetCaps&` through to `mimic::compile_kernel`, which casts `vendor_specific` internally.

---

## 22. The Mimic integration surface

Forge calls Mimic at five points. All five use abstract `TargetCaps`; Mimic dispatches internally by `caps.vendor_id` to the per-vendor backend.

### 22.1 `mimic::fast_cost`

```cpp
[[nodiscard]] Cycles mimic::fast_cost(
    fx::Bg, const KernelNode&, const TargetCaps&);
```

<10μs per call. Used during Phase B.5 (global cost estimation), Phase D (fusion cost), Phase F.2 (tile ranking). Dispatches to per-vendor tier-1 simulator. Ranking-accurate (10-20% absolute error) — good enough for fusion and tile decisions.

### 22.2 `mimic::propose_tiles`

```cpp
[[nodiscard]] std::span<const AbstractTile> mimic::propose_tiles(
    fx::Bg, Arena&, const KernelNode&, const TargetCaps&);
```

Used during Phase F.1. Per-vendor backend generates tiles that are hardware-realizable on the current target but expressed abstractly. Returns 16-64 candidates per kernel.

### 22.3 `mimic::compile_kernel`

```cpp
struct CompiledKernel {
    std::span<const uint8_t> bytes;            // opaque vendor binary (cubin / HSACO / NEFF / ...)
    Cycles                   predicted_cycles;
    float                    predicted_efficiency_ratio;  // achieved / theoretical_min
    KernelContentHash        ir002_hash;                  // cache key for verify
    uint32_t                 search_iterations;
    std::span<const Insight> insights;                    // structured diagnostics
};

[[nodiscard]] CompiledKernel mimic::compile_kernel(
    fx::Bg, Arena&, const KernelNode&, const TargetCaps&,
    const CompileConfig& cfg = {});
```

Used during Phase H. Tens to hundreds of ms per kernel (MAP-Elites inside Mimic). Returns opaque vendor-binary bytes + predicted cycles + insights. Forge never inspects `bytes`.

### 22.4 `mimic::predict`

```cpp
[[nodiscard]] SimResult mimic::predict(
    fx::Bg, Arena&, const CompiledKernel&, const TargetCaps&,
    SimTier tier = SimTier::MEDIUM);
```

Used during Phase L.2 by Augur for drift detection. Takes an already-compiled CompiledKernel (not a KernelNode) and returns fresh predictions without recompiling. Per-vendor backend runs its medium-tier simulator.

### 22.5 `mimic::probe_counters`

```cpp
[[nodiscard]] Measurements mimic::probe_counters(
    fx::Bg, const CompiledKernel&, const TargetCaps&);
```

Per-vendor hardware counter read. Wraps CUPTI equivalent on NVIDIA, rocprof on AMD, PJRT profiler on TPU, neuron-profile on Trainium, perf_event_open on CPU. Returns `Measurements` — 12 signals, vendor-agnostic.

### 22.6 What Forge never asks Mimic to do

- Kernel fusion decisions — Forge owns (Phase D, on IR001)
- Kernel template matching — Forge owns (Phase E, on IR001→IR002)
- Recipe selection — Forge owns (Phase E.3, using RecipeRegistry)
- Memory planning — Forge owns (Phase G, on IR002)
- Stream scheduling — Forge owns (Phase I, abstract)
- Collective placement — Forge owns (Phase K)

### 22.7 What Mimic owns that Forge never touches

- IR003* construction and mutation
- CSSA lowering (SIMT-semantics correctness pass; lives per-backend)
- Address space resolution (vendor-specific)
- Register allocation
- Instruction scheduling
- SASS / HSACO / NEFF / TPU-exec encoding
- Three-tier simulator
- MAP-Elites search and archive
- Math templates (DDIV, DSQRT, IDIV via Newton-Raphson, etc.)
- Peephole rewriters
- Runtime library (replaces libnrt / libcuda / libtpu / libhsa)
- Collective implementation (replaces NCCL / RCCL / libnccom / hcoll)
- Per-vendor calibration harness
- Genesis Kernel Pack precompilation

Clean cut. Forge is the optimizer. Mimic is the portability layer.

### 22.8 Unified CompileConfig — hybrid search modes

Previous sections describe Mimic's MAP-Elites (§22.3) + archive (§23.2) as the primary search strategy. External reference points: tinygrad's BEAM (beam search with real-hardware timing) and the Event Tensor paper's ETC (AOT-compiled megakernels). Crucible exposes the three strategies via a user-visible enum.

```cpp
enum class SearchMode : uint8_t {
    MAP_ELITES,      // default — simulator fitness, archive, deterministic
    HYBRID,          // MAP-Elites + real-hardware validation of top-K candidates
    BEAM,            // dev-only — tinygrad-style beam search with real-hw timing
};

struct CompileConfig {
    uint32_t    max_wall_time_ms       = 100;
    uint32_t    max_iterations         = 500;
    float       target_efficiency      = 0.92;
    SimTier     max_sim_tier             = SimTier::MEDIUM;

    // Search strategy
    SearchMode  mode                     = SearchMode::MAP_ELITES;

    // Hybrid / BEAM: real-hardware validation
    bool        validate_on_hardware     = false;
    uint32_t    hardware_budget_ms       = 200;
    uint32_t    hardware_cand_count      = 10;

    // Warm-start policy
    bool        enable_body_warm_start   = true;
    bool        enable_archive_warm_start = true;

    // Determinism
    uint64_t    seed                     = 0;      // 0 = derive from content_hash
};
```

#### 22.8.1 `MAP_ELITES` mode (default)

Pure simulator fitness, no hardware required. Archive-diverse, deterministic. 95-98% ranking accuracy from calibrated simulator. Hardware-free compile; CI and federation work offline. See MIMIC.md §19.

**When to use**: default case. Production compiles, federated cache builds, CI.

#### 22.8.2 `HYBRID` — MAP-Elites + hardware validation

MAP-Elites runs as primary search. Top `hardware_cand_count` candidates (default 10) from the archive are validated on real hardware within `hardware_budget_ms`. If hardware measurement disagrees with simulator prediction by >15%, the archive is corrected and a secondary MAP-Elites pass runs with updated fitness.

**When to use**: novel kernel families (no prior archive neighbor), production golden-model compiles, cross-vendor validation.

**Cost**: `mimic::compile_kernel` takes `max_wall_time_ms + hardware_budget_ms` (additive). In a training run, typically fires only on first compile of a new model; subsequent compiles hit cache. Total compile-time impact: <1% across a training run.

#### 22.8.3 `BEAM` — tinygrad-style beam search

Pure real-hardware search, no simulator. Beam of `beam_width` (default 4) kernel variants per generation; pruned by measured time. No archive; only the winner is kept.

**When to use**: dev / debug only, when investigating simulator accuracy discrepancies or when a specific workload needs aggressive hand-tuning. Not recommended for production — non-deterministic across runs due to timing noise.

#### 22.8.4 Warm-start policy

Two orthogonal switches:

- **Archive warm-start** (`enable_archive_warm_start = true`): load previous MAP-Elites archive for this exact kernel hash from Cipher; seed search from fittest cells. Cuts cold-start search by 50-80%.
- **Body warm-start** (`enable_body_warm_start = true`): for kernels with extension-point bodies (§18.7), find structurally-similar body archives via graph-edit-distance on `ComputeBody`; warm-start from the nearest. Converges 5-10× faster on research-variant kernels (e.g., `score_mod = ALiBi with slope 0.1` when `slope 0.08` archive exists).

Both enabled by default.

#### 22.8.5 Disk-cache keyed by (hash, device, mode)

Per tinygrad's pattern, all three modes write to a Cipher-backed disk cache keyed by `(kernel_content_hash, target_caps_signature, search_mode)`. Second compile of identical inputs hits at ~30 ns. Cache hit rate on repeat training steps: >99% after first run.

---

## 23. KernelCache — three-level content-addressed cache

Three-tier IR implies three-level caching. Each level keyed on the content hash of its IR plus the relevant caps signature.

### 23.1 The three levels

```
L1: (hash_ir001, abstract_caps_sig)     → IR002 snapshot     [PORTABLE CROSS-VENDOR]
L2: (hash_ir002, vendor_caps_sig)       → IR003* snapshot    [cross-chip within vendor family]
L3: (hash_ir003, chip_caps_sig)         → CompiledKernel     [chip-specific binary]
```

Each level has its own hash table, its own Cipher-backed persistence, its own invalidation triggers.

### 23.2 L1 — IR002 snapshot cache (the federation goldmine)

**Key**: `(KernelContentHash_ir001, VendorCapsHash_abstract)`.
**Value**: serialized `KernelGraph` — all KernelNodes, interned recipes and tiles, attrs, slot types, abstract tile proposals.
**Scope**: vendor-neutral. Cache entries are safe to share between NVIDIA-only installations, AMD-only installations, and mixed fleets. There is no hardware information in an IR002 snapshot.

**Hit shortcuts**: entire Phase E (IR001→IR002 lowering + recipe selection + tile seeding) on a match. Saves 15-20ms of compile per hit.

**Federation**: two Crucible installations running the same model produce identical IR002. Each independently compiles IR003* for its local hardware, but the IR002 lowering work only happens once globally. Meta compiling Llama-8B for H100 enriches Google's L1 cache for the same model on v5p.

### 23.3 L2 — IR003* snapshot cache (intra-vendor)

**Key**: `(KernelContentHash_ir002, VendorCapsHash_vendor)`.
**Value**: serialized IR003* program + MAP-Elites archive for that kernel on that vendor family.
**Scope**: vendor-specific but chip-generation-portable (e.g., the same IR003NV snapshot targets H100 and H200 if the recipe is family-compatible).

**Hit shortcuts**: IR002→IR003* lowering + MAP-Elites search on a match. Saves 20-50ms of compile per hit.

**Warm-start value**: a site that upgraded H100 → H200 hits L2 for most kernels; only L3 recompiles.

### 23.4 L3 — compiled binary cache (per-chip)

**Key**: `(KernelContentHash_ir003, VendorCapsHash_chip)`.
**Value**: vendor binary bytes (cubin / HSACO / NEFF / TPU-exec / CSL / ELF) + measured-performance metadata.
**Scope**: exact chip family + suffix (sm_100a vs sm_100f are separate L3 entries).

**Hit shortcuts**: all compile work, skip to module-load. ~1μs per hit (hash lookup + vendor-runtime module-load of cached bytes).

### 23.5 Storage + persistence

Each level uses the same Crucible infrastructure: lock-free open-addressing hash table in-process (Swiss table, CAS insert, acquire-load), backed by Cipher (L14) with three-tier storage:
- **Hot**: in-process RAM (99.9% of hits)
- **Warm**: local NVMe per Relay (survives process restart)
- **Cold**: S3/GCS (survives cluster failure; federation-shareable for L1 and L2)

Capacities: 1M slots at each level, ~128 bytes per entry pointer = ~380MB resident total per process. Practical fill is <1% even for large workloads.

### 23.6 Cache growth + invalidation

Every compilation writes to the levels it traversed. Every cache hit is free (~1μs lookup vs 10-600ms recompile depending on level).

Invalidation triggers per level:
- **L1**: IR001 content change (model code changed), abstract caps change (fleet composition changed), `forge_version` bump (rare, annual).
- **L2**: L1 invalidate cascades, vendor caps change (new vendor-SDK-equivalent in our runtime), `mimic_<vendor>_version` bump.
- **L3**: L2 invalidate cascades, chip caps change (firmware update), `calibration_version` bump from Phase L drift detection.

### 23.7 The Genesis Kernel Pack

Ships with each Crucible installation. Per chip family (`h100_sxm5`, `h200`, `b200`, `gb300`, `thor`, `rtx5090`, `mi300x`, `mi325x`, `tpu_v5p`, `tpu_v5e`, `tpu_v6e`, `trn2`, `trn3`, `cer_wse3`, `cpu_x86_64_avx512`, etc.), ~500 precompiled canonical kernels covering:

- GEMM / BMM / ADDMM at common tile families × 4 precisions (~200 kernels)
- Conv2D at common configs × 3 precisions (~60)
- FlashAttention at head_dim ∈ {64,128,256} × causal± (~40)
- LayerNorm / RMSNorm / BatchNorm (~50)
- Common reductions / activations / data-movement chains (~150)

Per chip pack: ~50-100MB shipped as `genesis-<chip>.bin` seed into L3 at install time. First-iteration compile for common shapes = cache hit from byte zero.

Three pack tiers: `genesis-minimal` (300 kernels, 30MB), `genesis-standard` (500, 100MB), `genesis-full` (1500, 300MB). Installations choose based on cold-start latency vs disk budget.

Built at CI time by running Forge+Mimic over the canonical workload list for each supported chip. Same code path the runtime uses; no "BLAS library" as a separate codebase.

---

## 24. Execution — mock-tensor capture + compiled launch

There is only one execution mode in Crucible: **mock-tensor capture followed by compiled launch**. The old RECORD / COMPILED distinction collapses — PyTorch's backend never executes during capture, and first-iteration correctness is delivered by Crucible's own compiled kernel, not by a vendor library warmup. This matters because:

- No cuBLAS/cuDNN/rocBLAS runs during iteration 1 — aligns with the no-vendor-libraries invariant
- Numerics from iteration 1 are identical to iteration 1000 (same compiled kernel)
- No "warmup drift" — you can validate bit-exactness from step 0

### 24.1 Capture path

```
// Per Vessel op (PyTorch, JAX, or native frontend):
// 1. advance op_idx                                     (~1ns)
// 2. build mock tensor:                                  (~2ns)
//    CrucibleTensorImpl with metadata only, arena-allocated,
//    no device storage yet
// 3. record op in TraceRing                              (~2ns)
// 4. return mock tensor to user code
//
// Total per-op cost: ~5-10ns.
// No vendor library invoked, no device memory allocated,
// no Python allocated; all op metadata lives in the arena.
```

The mock tensor is a full `torch.Tensor` / `jax.Array` / `cr.Tensor` from the frontend's perspective. It has shape, dtype, device, strides, requires_grad, and autograd history. It participates in framework control flow (views, reshapes, broadcasts). It has **no real storage** until Crucible compiles and executes.

### 24.2 Compile + execute on first encounter

When execution is required:
- **Sync points** (`tensor.item()`, `print(t)`, `bool(t)`, `tensor.numpy()`, `tensor.cpu()`) — force compile-up-to-here-and-execute.
- **Step boundaries** (explicit `s.step()`, `trainer.step()`, end of captured region) — compile entire region, execute, return materialized outputs.
- **Explicit materialize** (`cr.materialize(t)` / `cr.collect(t)`) — force compile + execute for one tensor.

On first encounter:
1. Flush accumulated mock-tensor trace to TraceGraph → Merkle DAG → RegionNodes
2. Check L1/L2/L3 cache by content_hash. Hit → skip to launch.
3. Miss → Forge compiles (Phase A through J) + Mimic compiles per vendor (Phase H) → populates cache
4. Per-vendor runtime loads the binary, resolves memory-plan pointers
5. Launches captured graph (or submits individually if not captureable)
6. Fills materialized storage into the mock tensors that are sync-point targets
7. Returns control to user code

First-iteration latency: ~10-100ms for a novel transformer forward pass, dominated by compile; ~1μs if cache hit.

### 24.3 Subsequent iterations

All cached. Guard-check on IR001 region hashes validates shape + schema didn't change. Guard passes → `runtime::launch_plan(cached_handle)` single call replays captured graph. Total foreground cost per iteration: ~5-10ns per op × N ops + ~0.5μs graph launch = microseconds, not milliseconds. No Python GIL contention; the launch is a single C++ call.

### 24.4 Guard divergence

If a guard fails (schema_hash mismatch, shape_hash mismatch):
- **schema_hash mismatch** — model code changed; retrace from scratch
- **shape_hash mismatch** — dynamic shape hit, bucket miss; retrace with new bucket entry
- **scope_hash / callsite_hash mismatch** — soft (same op, different call site); warn, reuse same compiled kernel

On hard diverge, foreground flushes the mock-tensor trace, Crucible retraces from the divergence point, and either finds a cached plan for the new shape or compiles one in the background while running reference-eager for this iteration.

### 24.5 Graph capture (vendor-agnostic)

When a plan has concrete tile dims (Phase I marked captureable), Mimic's per-vendor runtime captures the launch sequence into its native graph format:

```cpp
namespace crucible::runtime {
    GraphHandle capture_plan(fx::Bg, const ExecutionPlan& plan, StreamId stream);
    void         launch_plan(GraphHandle handle, StreamId stream);
    void         release_plan(GraphHandle handle);
}
```

Per-vendor implementation: CUDA Graph on NVIDIA, HIP Graph on AMD, precompiled TPU plan on Google, NEFF submit batch on Trainium, native job-list on CPU. Forge never touches `cuGraphLaunch` directly.

### 24.6 Dynamic shapes

Bounded cache of captured plans keyed by Phase F.4 shape bucket. Typical: 3-5 captured plans cover 99% of actual shapes (seq-len buckets [1-8, 9-32, 33-128, 129-512, 513+]). New bucket → compile in background + reference-eager for that iteration; captured plan available in ~hundreds of milliseconds.

Parametric kernels (symbolic tile dims, bounds in `SymbolTable` ranges) are the alternative for workloads with truly continuous shape distributions. One compile covers the range at 1-2% bounds-arithmetic tax per op. Mimic's per-vendor backend generates the parametric variant when Forge's TileSpec has range-bounded symbolic dims.

### 24.7 Replay determinism — the load-bearing invariant

Crucible's training state at step T is **fully determined by `(weights_T, optimizer_state_T, data_cursor_T, seed)`**. Nothing else. No hidden allocator state. No accumulated RNG state. No vendor-library internal state. This is the invariant that every other design commitment serves.

Why it works:

1. **Philox4x32 counter-based RNG.** At any op in any step, the RNG draws from `philox(seed, step_idx * 2^32 + op_idx * 2^16 + thread_idx)`. Pure function. Zero state. Same `(seed, step_idx, op_idx, thread_idx)` → same bits, any thread, any device, any run.
2. **Static content-addressed memory plan.** Every tensor's device-memory address is `pool_base + plan.slots[slot_id].offset`. No dynamic allocation, no history-dependent layout. Same graph → same plan → same addresses.
3. **Bit-exact per-backend kernels.** Recipe pinning + our own ISA emission means each backend reproduces the same algorithm byte-identically. See §4 and Mimic's recipe-realization tables.
4. **Canonical reduction topology.** All-reduce across DP group uses a pinned binary tree sorted by UUID, producing bit-identical sums regardless of peer arrival order (with `determinism = BITEXACT` recipes).

**Checkpoint format**: `(step_idx, weights, optimizer_state, data_cursor, seed, recipe_registry_epoch)`. All tensors stored in Cipher's binary-packed format (MessagePack or FlatBuffers); metadata in JSON. Checkpoint size ≈ weights + optimizer state (Adam: 2× weights); no allocator state, no RNG state, no kernel cache (rehydrates from L3).

**Recovery**: Node failure at step T in a run where last checkpoint was step T₀:
1. Raft commits new fleet membership at epoch E+1
2. Surviving Relays load `(weights_T₀, optimizer_T₀, cursor_T₀, seed)` from nearest Cipher tier (hot RAM if available from healthy peer, else warm NVMe, else cold S3)
3. Rewind DataNode to `cursor_T₀`
4. Replay steps T₀+1 through T deterministically
5. `weights_T` from replay is bit-identical to `weights_T` the dead run would have produced (by recipe BITEXACT)
6. Resume training

Cost bound: at most `N = (T - T₀)` steps of recompute per failure. With `N = 500` and 1s/step, ~500s wasted per failure. With aggressive `N = 100`, ~100s. Much better than the conventional "lose up to 1 step + pray the RNG state rebuilds".

**Replay Determinism CI** (mandatory from day 1 of implementation):

```
Test: bit_exact_replay_invariant
  1. Train 1000 steps from seed=42 on backend B. Snapshot weights every 100 steps.
  2. Kill after step 500. Rewind to step-0 checkpoint.
  3. Replay steps 1..1000.
  4. Assert: bytes_equal(run1.weights[step_i], run2.weights[step_i]) for i in [100,200,...,1000]
  5. Must pass for every BITEXACT recipe, every merged PR.

Test: cross_vendor_step_invariant  
  1. Train 100 steps on backend NV with seed=42.
  2. Train 100 steps on backend AM with same seed, same model, same data cursor.
  3. Assert: bytes_equal(nv.weights[i], am.weights[i]) for i in [10,20,...,100]  
  4. Must pass for BITEXACT recipes; ULP-bounded for ORDERED.
```

If either test goes red, hidden state has been introduced somewhere. Investigate immediately — this is the core invariant Crucible exists to deliver.

### 24.8 ExecutionPlan structure

See §15 Phase J for the full struct. Arena-allocated, ~100KB-few-MB, persisted to Cipher. Position-independent via `SlotRef` indirection: same plan works at any pool base, enabling reload across processes / reincarnation.

---

## 25. Distribution and 5D Parallelism as a Compiler Pass

Forge's Phase K is the analog of what XLA calls GSPMD and what PyTorch hand-rolls in DDP/FSDP — partitioning across multiple devices — but implemented as a proper compiler pass on IR002, not user annotations. The collectives it emits run over Crucible's native transport (CNTP), never NCCL / RCCL / UCX / libnccom / hcoll.

### 25.1 Five dimensions

| Dim | What | Communication pattern | Cost probe |
|---|---|---|---|
| **TP** tensor | Split individual GEMMs (K or N dim) | all-gather, reduce-scatter per GEMM | bandwidth-limited |
| **DP** data | Replicate model, split batch | all-reduce per gradient, per optimizer step | bandwidth-limited |
| **PP** pipeline | Assign layers to different devices, micro-batches flow through | point-to-point activations/gradients | latency-limited + bubble overhead |
| **EP** expert (MoE) | Route experts to devices | all-to-all per expert dispatch/combine | latency-limited for small experts, bandwidth for large |
| **CP** context | Split sequence dimension | point-to-point + cross-attention all-gather | mixed |

Each has a measured cost from Meridian's N×N bandwidth/latency probes, run over CNTP's measured topology.

### 25.2 Partitioning objective

```
minimize sum of all collective costs
subject to:
    per-device memory ≤ device capacity
    per-device compute ≤ estimated throughput
    no partition crosses a fused KernelNode boundary
    latency per iteration ≤ SLA
    chosen recipe natively supported across all members of DP group (STRICT policy)
```

Small ILP (~50 variables for 8-device setups; 500 for 64-device). Solves in <500ms, comfortably within Phase K's budget given compile is amortized.

### 25.3 DiLoCo integration

DiLoCo (Distributed Low-Communication) is a Crucible-native pattern: per-step local updates + periodic outer-step synchronization. Forge's Phase K treats DiLoCo's outer-step all-reduce as a separate KernelNode::COLLECTIVE scheduled on a slower interval, freeing inner-step bandwidth.

Configurable parameters:
- Inner H steps between outer syncs (default 8)
- Compression ratio (top-K + int8 quant, 50-100× bandwidth reduction)
- Hierarchical: fastest-fabric every step / mid-tier every N / cross-datacenter every M (auto-tuned from Meridian's multi-tier bandwidth matrix)

### 25.4 Topology-aware algorithm selection

Each CollectiveAttrs carries an `algorithm` field ∈ {RING, TREE, HALVING_DOUBLING, HIERARCHICAL, INNETWORK_OFFLOAD}:
- Ring for bandwidth-bound large messages
- Tree for latency-bound small messages
- Recursive halving-doubling for balanced mid-size
- Hierarchical for multi-tier topologies (intra-node fast + inter-node slow)
- `INNETWORK_OFFLOAD` when the fabric supports in-fabric reduction (Mellanox Quantum SHARP, Google TPU ICI aggregation, AMD Infinity Fabric reductions, Cerebras SwarmX) — Mimic's `NetworkOffload` plane decides per-call if eligible

Paths avoid degraded links; Canopy mesh continuously reports link health to Meridian.

### 25.5 Heterogeneous devices + numerical equivalence across vendors

The IR001 and IR002 graphs are one. Per-Relay compilation produces different IR003* binaries for different devices. A mixed H100 + MI300X + trn2 + v5p cluster compiles each Relay's plan against its local `TargetCaps`, realizing the **same pinned `NumericalRecipe`** on each backend.

Because the recipe pins accumulator dtype, reduction order, rounding mode, scale policy, and softmax recurrence, the gradients computed on each chip are ULP-bounded equivalent (or bit-exact when `determinism = BITEXACT_TC` or `BITEXACT_STRICT`). Cross-vendor all-reduce aggregates them without introducing numerical skew. Content-addressing ensures L1 (IR002) cache entries are shared across all vendor types; L2 and L3 caches are per-vendor and per-chip.

LOR batch distribution: micro-batches proportional to measured throughput. An H100 gets 3× more samples than an MI300X if measurements say so; both fully utilized. Gradients weighted by actual batch size at the all-reduce.

### 25.6 Z3 joint partition search and topology benchmarking

Phase K.PartitionGraph's objective function (§25.2) is non-trivial; a naïve ILP with ~50 binary variables on an 8-device setup solves in <1s, but a 64-node cluster with 5D × microbatch-size × bucket-granularity search exceeds the ILP's tractable frontier. Crucible's Z3 fork (see the Z3 performance fork project) provides SMT-optimizer support that solves partition problems to proven-optimal within 1-10s for cluster sizes up to 1024 nodes.

**Solver stack.** Three tiers:

1. **Hard constraints** (immediate rejection): `TP × DP × PP × EP × CP = N`, `weight_per_gpu ≤ device_mem * 0.7`, `recipe ∈ fleet_intersection`.
2. **Structural heuristics** (narrow the search): `TP ≤ max(intra_node_nvlink_peers)` (TP all-reduce is latency-critical), `PP ≤ num_nodes`, `DP × PP ≥ total_nodes / TP`, `PP bubble < 10%`, MoE → `EP = num_experts` if it fits, CP only when `seq_len × batch > per_gpu_activation_budget`.
3. **Z3 optimization**: joint solve over `(TP, DP, PP, EP, CP, microbatch_size, num_microbatches, bucket_size, pipeline_schedule)` minimizing predicted step time.

**Cost model inputs.** The solver consumes two tables populated at Keeper init:

- **Topology table** (`TopologyMatrix`): N×N matrix of `(latency_ns, bandwidth_gbps, algorithm_map)` entries per peer-pair, measured by Meridian via CNTP's transport probes. Refreshed on membership change or Augur drift.
- **Collective benchmark table** (`CollectiveBenchmarks`): per `(op, algorithm, sub_topology, msg_size)` measured latency and bandwidth, produced by Meridian running each collective primitive on representative subsets at init. Cached in Cipher with content-addressed key; rebuilt on topology change.

**Predicted step time:**

```
predicted_step_time(partition, schedule, benchmarks) =
    compute_time(partition, kernels_per_stage)                    ← from Mimic fast_cost
  + collective_time(partition, schedule, benchmarks)              ← from benchmarks table
  + pipeline_bubble(schedule, num_stages, num_microbatches)       ← schedule-dependent
  + overhead_per_step                                              ← launch overhead × ops
```

Z3's OPT module minimizes this subject to the constraints. Solutions are proven-optimal within the model; model error is bounded by the benchmarks' measurement precision (~5%).

**Measured-validation phase.** If Z3 proposes a configuration novel to the fleet, Phase K runs 10-50 training steps to measure actual MFU and compare to predicted. Within 5% → commit; outside 5% → fall back to second-best. Cost: one-time ~5 minutes at the start of training. Re-triggered only when topology changes significantly.

**Online learning from Augur.** Per §13.6 of CRUCIBLE.md, Augur samples per-collective timings at runtime and updates `CollectiveBenchmarks`. Systematic deviation >10% for 100+ samples → flag partition for re-solve; Z3 re-runs with updated table; if proposed config differs, reshard at next iteration boundary (typically ≤1s cost at modern weight sizes).

**Example solver input/output** (8×H100 NVLink node + 4×MI300X RoCE node, 70B Llama):

```
Input:
    N = 12, kinds = {8 × h100_sxm5, 4 × mi300x}
    recipe intersection = {f16_f32accum_tc_pairwise_rn, f16_f32accum_ordered, ...}
    topology: intra-NVLink 900GB/s within h100 group;
              intra-RoCE 400Gb/s within mi300 group;
              cross-group via Ethernet 100Gb/s
    policy: BITEXACT_TC, STRICT fleet

Z3 output:
    TP=8 (within h100 NVLink group — tensor-core intra-node)
    DP=2 (h100 group vs mi300 group, cross-group all-reduce)
    PP=1, EP=1, CP=1
    microbatch_size=4, num_microbatches=16
    bucket_size=32MB (tuned for cross-group RoCE BW)
    pipeline_schedule=1F1B (PP=1 so bubble=0)
    predicted MFU: 62%
```

### 25.7 Scheduling cost model — the MFU gap decomposition

Forge's Phase K output carries a cost-model report that Augur uses for continuous MFU tracking (CRUCIBLE.md §13.7). The model decomposes loss into attributable categories:

```
Peak_TFLOPs (from TargetCaps):             989 TFLOPs BF16 per H100
Achieved_TFLOPs (from measurement):        625 TFLOPs   (63.2% MFU)

Attribution of the 36.8% gap:
  Lost to comm (not overlapped):             9%  → bucket sizing / concurrent collective
  Lost to pipeline bubble:                    4%  → zero-bubble PP scheduling
  Lost to memory BW (attention):             11%  → FP8 mixed precision
  Lost to small-kernel launch overhead:       5%  → persistent-kernel fusion
  Lost to suboptimal kernels:                 4%  → MAP-Elites generations
  Lost to suboptimal tile shapes:             2%  → shape-bucket sub-specialization
  Lost to suboptimal partition:               2%  → Z3 re-solve with online data
```

Each attribution corresponds to a specific lever Phase K or a downstream pass can pull; Augur emits a ranked recommendation list (§13.5) pointing at the highest-expected-return lever.

### 25.8 Asymmetric-fleet Z3 partitioning

The Z3 partition solver (§25.6) handles homogeneous fleets as the primary case. For asymmetric fleets — mixed consumer/datacenter SKUs, mixed vendors, mixed generations — the solver's constraint set extends to express per-node heterogeneity explicitly.

#### 25.8.1 Asymmetric fleet patterns

Real-world deployments we support:

- **Mixed Blackwell SKU**: 1× RTX PRO 6000 (GB203, 96 GB) + 8× 5090 (GB202, 32 GB). MIMIC.md §42.6.
- **Mixed generation**: 4× H100 (80 GB HBM3) + 4× H200 (141 GB HBM3e). Different HBM sizes, same compute family.
- **Mixed vendor**: 4× H100 + 4× MI300X. Different architectures, different NVLink/XGMI topologies.
- **Mixed tier**: 8× B200 + 8× 5090 (datacenter + consumer Blackwell).
- **Geographic heterogeneity**: 8× H100 Dublin + 8× H100 Tokyo, different RTT tiers.

Each pattern has specific partition-solver implications.

#### 25.8.2 Z3 constraint extensions

Base constraints from §25.2 extended with per-node dimensions:

```
For each node n:
  vram[n] ≤ node_vram_capacity[n]          # asymmetric VRAM
  flops[n] ≤ node_flops_rating[n]          # asymmetric compute
  bandwidth[n, m] ≤ link_bw[n, m]          # asymmetric interconnect
  recipe[n] ∈ node_native_recipes[n]        # fleet intersection is conjunctive
```

Objective extended with heterogeneity terms:

```
minimize step_time(partition) + slowest_node_stall_cost
where slowest_node_stall_cost = max over nodes n of (effective_time[n])
```

The `max` captures that DP synchronization is gated by the slowest node. Without this term, Z3 proposes uniform partitions that waste fast-node capacity.

#### 25.8.3 Policies for heterogeneous weight distribution

Three modes, selected via `partition_policy.asymmetric`:

- **`STRICT_UNIFORM`** — every node gets identical work. Slowest node dictates throughput; fast nodes idle `(1 - slowest_flops/fastest_flops)` of each step. Simple, leaves performance on the table.
- **`CAPACITY_WEIGHTED`** — work proportional to each node's TFLOPs rating. Fast nodes do more per step; slow do less. Maximizes fleet throughput at the cost of gradient-aggregation complexity. Requires per-node batch-size variation.
- **`TIERED`** — nodes segregated into tiers (e.g., anchor = datacenter, worker = consumer). Anchor holds optimizer state + gradient sum; worker holds compute partitions. Loss of worker = partition reshard (~1-2 s); loss of anchor = checkpoint recovery.

Default: `CAPACITY_WEIGHTED` when `StdDev(node_flops) / Mean(node_flops) > 0.15`; otherwise `STRICT_UNIFORM`. `TIERED` is opt-in.

#### 25.8.4 Example: mixed Blackwell fleet

Fleet: 1× PRO 6000 (~90 TFLOPs FP16, 96 GB) + 8× 5090 (~82 TFLOPs FP16, 32 GB). Workload: Llama-70B BF16 training.

Z3 input:
```
N = 9 nodes
node_flops = [90, 82, 82, 82, 82, 82, 82, 82, 82]
node_vram  = [96, 32, 32, 32, 32, 32, 32, 32, 32]
recipe = f16_f32accum_tc_pairwise_rn (BITEXACT_TC, native on Blackwell)
topology: BAR1 P2P between all pairs at ~55 GB/s unidirectional
policy: TIERED
```

Z3 output (example):
```
TP=1, DP=9 (one replica per GPU)
EP=1, CP=1, PP=1
microbatch_size per GPU:
  PRO 6000 (anchor):   6     # smaller batch, holds optimizer state
  5090s (workers):     8     # compute proportional
bucket_size=32 MB
schedule=1F1B (no PP bubble since PP=1)
predicted MFU: 58%
```

PRO 6000 holds optimizer state because it has 96 GB. 5090s hold FP16 weights (~141 GB / 9 = 15.7 GB per replica) + gradients (~15.7 GB) = ~31 GB, fits in 32 GB. Work is distributed ~90:82 proportional to compute; effective step time is the 5090's time.

#### 25.8.5 Cross-vendor caveats

Cross-vendor fleets (NV + AMD) require recipe in the intersection of both vendors' `native_on` sets. On Blackwell + CDNA3+ most recipes work; edge cases (e.g., NVFP4 only native on NV) force STRICT fleet policy and omit non-native nodes from that partition dimension.

Cross-geographic fleets have RTT-dominated all-reduce; Z3 factors 100 ms+ round-trip into the cost model and prefers DiLoCo-style outer-step sync (§25.3, CRUCIBLE.md §12.3).

#### 25.8.6 CI validation

`test_asymmetric_fleet_partitioning`:

1. Z3 proposes partition for 4 canonical asymmetric fleets (mixed-Blackwell, mixed-gen, cross-vendor, cross-geo)
2. Run 10-50 training steps on each, measure actual MFU
3. Verify measured ≥ 0.9 × predicted
4. Regression guard: any future Z3 change that degrades asymmetric-fleet perf fails CI

Fleet asymmetry is a gap in NCCL's tuning tables (defaults to `minCompCap` uniformly, MIMIC.md §42 reference). Our solver fills that gap as a first-class feature.

### 25.9 Measured performance targets

Crucible commits to specific numerical targets across the stack. Each target is:

- A design goal (set at architecture time, this document)
- A CI-validated threshold (enforced per merged PR against Phase L measurements, MIMIC.md §30)
- A regression guard (Augur flags drift >10%, CRUCIBLE.md §13.3)

Numbers marked ✽ require real-silicon CI validation before declaration as achieved.

#### 25.9.1 Per-launch dispatch (NV Hopper+ reference)

| Metric | Target |
|---|---|
| CPU critical path (warm) | 80-100 ns ✽ |
| CPU critical path (cold, N scalar patches) | 120-200 ns + 10 ns × N ✽ |
| Doorbell → first-SM-cycle | 400-650 ns ✽ |
| Kernel-end → CPU-observed | 400-800 ns ✽ |
| Full round-trip for 200 ns compute kernel | 1.0-1.5 μs ✽ |
| Persistent kernel internal iteration overhead | ~200 ns |

Per-vendor equivalents:

- AMD CDNA3 AM-style: ~120-250 ns CPU critical path, ~500-800 ns end-to-end
- TPU v5p: ~1-2 μs per launch (scalar-proc overhead)
- Trainium trn2: ~500 ns - 1 μs
- CPU: ~50 ns (direct function call)

#### 25.9.2 Collective targets (intra-node NVLink P2P, 8 GPUs)

| Collective | Size | Target | NCCL baseline |
|---|---|---|---|
| all_reduce | 8 B | ~3-5 μs ✽ | 25 μs |
| all_reduce | 1 KB | ~5-8 μs ✽ | 25 μs |
| all_reduce | 1 MB (BW-bound) | ~25-40 μs ✽ | 40 μs |
| broadcast | 8 B (1-to-8) | ~2 μs ✽ | 15 μs |
| reduce_scatter | 1 MB | ~15-25 μs ✽ | 30 μs |
| all_gather | 1 MB | ~20-35 μs ✽ | 35 μs |

#### 25.9.3 Collective targets (cross-node IB NDR 400, 64 peers)

| Collective | Size | Target |
|---|---|---|
| RTT (64 B RDMA write) | - | 700-900 ns ✽ |
| all_reduce | 8 B | 8-12 μs ✽ |
| all_reduce | 1 MB | 80-120 μs ✽ |
| all_reduce | 100 MB | 1.5-3 ms ✽ |

#### 25.9.4 Init targets

| Phase | Target | PyTorch+NCCL baseline |
|---|---|---|
| Keeper startup (single node) | 500-2000 ms ✽ | ncclCommInit: 500-1500 ms |
| Canopy join (SWIM + Raft) | 200-500 ms ✽ | n/a |
| CNTP handshake (8 nodes, pre-warmed QPs) | 10-50 ms ✽ | NCCL: 200-500 ms |
| CNTP handshake (64 nodes) | 100-300 ms ✽ | NCCL: 1-3 s |
| Plan compile (cold, novel shape) | 100-300 ms ✽ | torch.compile: 1-5 s |
| Plan compile (L1/L2 cache hit) | 1-10 μs ✽ | n/a |
| Total cold fleet init (8 H100) | 3-5 s ✽ | 30-60 s |

#### 25.9.5 Recovery targets

| Event | Target |
|---|---|
| FLR + GSP re-upload | 1.5-2.5 s ✽ |
| SWIM confirmed-dead detection | 5 s (worst case) |
| Raft membership commit | 50-100 ms |
| Cipher checkpoint load (warm NVMe) | 50-200 ms |
| Plan reshard compute | 200-500 ms |
| Recovery excluding replay | 5-7 s ✽ |

#### 25.9.6 Training MFU targets on H100

| Model / config | Target MFU | Megatron+NCCL baseline |
|---|---|---|
| Llama-70B, 8× H100 BF16 | 60-65% ✽ | 38% |
| Llama-70B, 64× H100 BF16 | 55-60% ✽ | 35% |
| Llama-70B, 64× H100 FP8 | 65-75% ✽ | 50% (Megatron FP8) |
| GPT 405B, 256× H100 BF16 | 55-65% ✽ | 35-40% |
| Mixtral 8×7B MoE, 64× H100 BF16 | 50-60% ✽ | 40% |

Contributions per lever (rough attribution, non-linear compounding):

- Zero-bubble PP: +10-15 MFU points
- Bucketed async all-reduce: +8-12 points
- Persistent-kernel multi-layer fusion: +6-12 points
- FP8 mixed precision: +10-20 points (where viable)
- MAP-Elites kernel search: +3-8 points
- Direct SASS emission (no PTX): +4-7 points
- Z3 partition solver vs static: +5-10 points
- Compounded ceiling: +60-80 points on top of 30% baseline

#### 25.9.7 Compile time budgets

| Workload | Target |
|---|---|
| Single GEMM from cold | 30-100 ms |
| Single attention kernel (FLASH3, cold) | 50-200 ms |
| Full transformer block (24 layers, cold) | 500 ms - 2 s |
| Full model + training step (70B, cold) | 5-15 s |
| Full model + training step (L1 hit) | 100-500 ms |
| Full model + training step (L3 hit) | 1-10 μs (plan load only) |

#### 25.9.8 Memory targets

| Metric | Target |
|---|---|
| VRAM overhead (runtime vs weights+activations+grads+optim) | <3% ✽ |
| Pinned sysmem overhead per Keeper | ~16 MB ✽ |
| Cipher hot-tier overhead per Keeper | 100-500 MB (configurable) |
| Plan storage per training step | ~60 KB |
| 500K-step training run plan storage | ~30 GB raw → ~50-500 MB unique after dedup |

All numbers are **design commitments**. Actual measurements from Phase L feed Augur's drift detection; sustained deviations >10% trigger recalibration. Achievement claims for ✽-marked targets require CI validation with linked measurement tickets.

---

## 26. The Validate Loop and Continuous Calibration

Phase L is not a compile phase; it's a runtime observer. But it drives compile behavior through the invalidation loop, so it's architecturally part of Forge.

### 26.1 The loop

```
while (system_running):
    sample_kernel_executions(rate = 1%)
    for each sampled invocation:
        measured  = mimic::probe_counters(compiled_kernel, caps)   // per-vendor counter access
        predicted = mimic::predict(compiled_kernel, caps)
        residual  = measured - predicted
        append(residual, regression_dataset)
    
    if P95(residuals[signal] over last N samples) > threshold:
        trigger_recalibration()
    
    if any TargetCaps signature change detected (L2 or L3 level):
        trigger_cache_invalidation_for_affected()
    
    if drift_detection_identifies_specific_kernel:
        trigger_recompile(kernel)
```

Runs in Augur (L15) continuously. Not on the foreground hot path; sampling is infrequent and amortized.

### 26.2 Calibration triggers (vendor-agnostic)

- **P95 residual > 10% for 100+ samples** — Mimic's per-vendor backend's timing model has drifted; re-run per-vendor calibration microbenches
- **Driver or firmware version change** — re-calibrate (applies to all vendors)
- **Thermal throttling detected** — re-calibrate under current thermal conditions
- **ECC / correctable-error rate increase** — re-calibrate (error-correction overhead affects effective BW)

Recalibration produces updated `TargetCaps` (vendor-specific extension updated; abstract fields re-derived). All affected L3 cache entries marked for invalidation. L2 entries survive unless the vendor-caps signature also changed. L1 entries always survive.

### 26.3 Cost model confidence

Mimic reports confidence per prediction. Low-confidence predictions (novel kernel shapes, uncalibrated patterns) trigger extra measurement. High-confidence skip verification.

Confidence profile shifts toward high-confidence as the calibration dataset grows per-chip.

### 26.4 Model correction learning

Residuals are stored, analyzed offline, feed back into per-vendor calibration microbench harnesses. Not ML-based (yet). Future extension: learned corrections at the per-vendor simulator level, supervised by the CI numerics matrix.

---

## 27. What Forge Deliberately Does Not Do

Listed explicitly. Each item is ~10-500K lines of C++ that the traditional stack carries and Forge refuses to carry — either in Forge itself, or as a runtime dependency. The north star: **Crucible depends on the kernel driver, RDMA verbs, the Linux kernel, and the CPU. Nothing else vendor-proprietary.**

### 27.1 No vendor compiler frontends

No CUDA C++, no HIP C++, no SYCL, no OpenCL, no Triton, no PyTorch TorchScript. No parse of `__global__`, no template instantiation, no preprocessor pipeline. Inputs are Crucible's tensor-level Graph IR. Savings:
- EDG C++ frontend (~500K LOC, proprietary)
- Clang/LLVM C++ frontend (~800K LOC)
- Triton parser + JIT (~80K LOC)
- HIP clang-hip (~40K LOC delta over Clang)

The biggest single category. We don't write vendor-language kernels; Crucible synthesizes them from IR001.

### 27.2 No vendor ISA frontends

No PTX parser, no SPIR-V parser, no HLO parser (as Forge input), no StableHLO parser. Forge's input is always IR001. Other front-end IRs enter at the Vessel adapter layer and are lowered to IR001 there (Vessel is thin, ~2K LoC per framework).

### 27.3 No vendor code emission in Forge

Forge never emits SASS, PTX, HSACO, HIPKernel, NEFF bytecode, TPU executable, CSL, SPIR-V, WebGPU, or any other vendor binary format. All emission happens in `mimic/<vendor>/`. Every Mimic backend owns its emitter; Forge's source tree has zero `#include` of any vendor header.

### 27.4 No vendor library runtime dependencies

Forge (and the Crucible runtime it feeds) depend on **none** of:
- `libcuda.so` / `libcudart.so` (NVIDIA driver runtime)
- `cuBLAS`, `cuBLASLt`, `cuDNN`, `cuSPARSE`, `cuSOLVER`, `cuFFT`, `cuRAND`
- `libhsa-runtime` (AMD ROCm runtime), `rocBLAS`, `MIOpen`, `hipBLAS`, `hipSPARSE`, `hipFFT`, `hipRAND`
- `libtpu.so`, `xla-tpu`, PJRT shims (beyond ABI definition)
- `libnrt.so`, `libnccom.so`, `libncfw.so`, `libnrtucode.so` (AWS Neuron runtime)
- `NCCL`, `RCCL`, `hcoll`, `libsharp`, `UCX`, `openucc`
- Vendor-shipped BLAS/DNN/collective dynamic libraries of any kind

Mimic's runtime layer (`mimic/<vendor>/rt/`) wraps kernel driver ioctls directly and provides our own implementations of everything above. The sole vendor component we link is the kernel driver (via `/dev/<vendor>` ioctls), which is GPL / dual-licensed / stable-ABI on every vendor we target.

Savings vs traditional stack: ~3-4 GB of vendor-proprietary binary dependencies removed per installation.

### 27.5 No vendor collective library dependencies

No NCCL, no RCCL, no libnccom, no hcoll, no libsharp-as-runtime-dep, no MPI, no OpenMPI, no Gloo. CNTP (Crucible Native Transport Protocol) is our ~28K-LoC replacement: transport (NVLink / RDMA / TCP), gossip (SWIM), consensus (Raft-scoped), collectives (ring / tree / HD / hierarchical / offload-aware). Collective kernel launches go through `mimic::<vendor>::comm::` functions that wrap raw transport primitives.

In-network offload (Mellanox SHARP via the loadable module, TPU ICI, AMD XGMI, Cerebras SwarmX) is supported via the `NetworkOffload` plane but is a runtime-linked optional — never a build-time dependency.

### 27.6 No general-purpose SSA middle-end in Forge

Forge's REWRITE phase has 10 sub-passes. LLVM has hundreds. What we skip:
- Alias analysis framework (we have limited, targeted alias analysis in B.4)
- Inliner cost model (we fuse at the graph level, not the function level)
- Loop interchange (our loops are tile-shaped; interchange happens in Phase F tile selection)
- Loop unroll-and-jam, loop distribution (handled at tile level)
- Partial redundancy elimination (GVN covers this)
- Scalar evolution (Expr symbolic engine handles affine expressions natively)
- LazyCallGraph, MergeFunctions (no call graph; no externally-visible functions to merge)
- Structurize CFG (our control flow is structured by construction)

Savings: ~800K LOC of LLVM optimization passes never enter Forge. Per-vendor Mimic backends may reuse an LLVM middle-end *internally* (e.g., AMD backend reuses LLVM-AMDGPU for ISA selection only); Forge itself doesn't.

### 27.7 No profile-guided optimization indirection

PGO in LLVM requires instrumented build → profiling run → merge step → re-compile. Forge skips this — Crucible's Merkle-DAG-hash-matched profiles are continuously collected via Phase L. There is no "PGO build mode"; profiling is always on.

### 27.8 No multi-tenant compilation

Traditional vendor compilers handle multiple modules, cross-TU optimization, ABI compatibility across independently-compiled objects. Forge compiles one Crucible graph at a time, closed-world. Savings: ThinLTO module summary, cross-module inlining, weak symbol resolution, COMDAT sections, per-TU metadata harmonization.

### 27.9 No exceptions, RTTI, vtables, typeinfo

Accelerator code has none of these natively. Crucible is pure-functional at the graph level. Forge uses `kind` enums + `static_cast`, never `dynamic_cast`, never `typeid`.

### 27.10 No printf in kernels (first-class)

Forge can emit `printf` nodes if Crucible's Graph includes them (for debugging), but they're not a first-class citizen. The `printf` → vendor-specific debug-print lowering is a small specialty case per backend, not a whole subsystem.

### 27.11 No dynamic parallelism / device-side launches

Device-side kernel launches, device-side malloc, in-kernel graph construction — none of it. Crucible handles the entire launch hierarchy from host; kernels are leaves.

### 27.12 No coroutines, no device-side syscalls, no dynamic linking on device

### 27.13 No graphics / texture / surface handles

Texture filtering, normalized coordinates, border modes, surface stores, vertex/tessellation/geometry/pixel shaders, multi-view rendering — all legacy graphics infrastructure, all excluded. Tensor-memory-access primitives on modern accelerators replace the structured-memory-access use case for ML.

### 27.14 No legacy accelerator support

Forge starts at NVIDIA sm_90+, AMD CDNA3 / RDNA3+, TPU v5+, Trainium 1+, Cerebras WSE3+. Older generations have different cost characteristics, different primitives, different collective libraries; supporting them compounds backends without workload justification.

### 27.15 No pass-manager swiss-army-knife

Traditional vendor compilers carry organic-growth pass managers with hundreds of knob-tunable entries. Forge's PassManager is ~2K LoC with 12 phases and hard budgets.

### 27.16 No RTTI on IR nodes

Forge uses `kind` enums + `static_cast`, never `dynamic_cast`, never `typeid`. Crucible Axiom 2 TypeSafe.

### 27.17 No untrusted input validation

Crucible's Graph IR is well-formed by construction. Forge does not sanitize or validate structural properties that Crucible's graded-type system already enforces at IR001 level.

### 27.18 Aggregate: what we skip

~5-8 M LOC of C++ across the entire traditional stack that Forge and Crucible sidestep. The vendor-proprietary binary dependency savings alone are ~3-4 GB per installation. The sustained-engineering savings are larger: every vendor SDK update that would otherwise force Crucible patches now only affects us if the kernel driver's ioctl protocol changes (rare) — not if cuBLAS adds a new knob or NCCL changes its internal ring algorithm.

This is the leverage. Refusing vendor libraries + refusing vendor compilers + refusing vendor ISA frontends + refusing graphics / PGO / multi-tenant / exceptions / RTTI / legacy hardware collapses an enormous amount of machinery into a fit-in-head codebase we own forever.

---

## 28. Head-to-Head Comparison

| Dimension | XLA | Inductor | ptxas + cicc | Triton | CUTLASS | **Forge** |
|---|---|---|---|---|---|---|
| **IR layers** | HLO → LLVM → xla-tpu (opaque) | FX → Sched → Triton IR → LLVM → PTX | PTX → Ori → Mercury → SASS | Triton IR → LLVM → PTX | C++ template → PTX → SASS | **IR001 (ops) → IR002 (kernels, portable, recipe-pinned) → IR003\* (per-vendor, native ISA)** |
| **Ops supported** | ~100 HLO | ~2000 ATen via decomposition | all PTX | Triton primitives | template compositions | 146 CKernel + OPAQUE fallback |
| **Fusion strategy** | greedy pairwise | greedy vertical | N/A (not a graph compiler) | user-specified per kernel | user-specified per kernel | **DP/ILP global, 9 FuseKinds, multi-layer persistent** |
| **Fusion group size** | typ 2-5 ops | typ 2-5 ops | N/A | arbitrary (user) | arbitrary (user) | **typ 20-200 ops, persistent-capable** |
| **Cost model** | none, runtime measure | none, Triton autotune | none (scheduler heuristic) | none, runtime autotune | none, exhaustive profile | **analytical, calibrated, 95-98% accurate** |
| **Kernel search** | none | random autotune | hand-tuned heuristics | autotune config sweep | profile exhaustive | **MAP-Elites evolutionary** |
| **Cache key** | HLO hash (brittle) | FX Python hash (brittle) | no cross-program cache | source file hash | none | **ContentHash Merkle (cross-program, cross-user, cross-org)** |
| **Cache scope** | per-process | per-process | none | per-user | none | **Cipher: cross-run, cross-model, cross-org** |
| **Codegen target** | LLVM → HLO → xla-tpu / ptxas | Triton → LLVM → PTX → ptxas | SASS (NV-only) | LLVM → PTX → ptxas | PTX → ptxas | **direct vendor ISA per backend (SASS / HSACO / NEFF / TPU-exec / CSL)** |
| **Vendor libs required at runtime** | libtpu + cuBLAS + cuDNN + NCCL | cuBLAS + cuDNN + NCCL | cuBLAS + cuDNN + NCCL | cuBLAS + cuDNN + NCCL | cuBLAS + cuDNN | **none — kernel driver + RDMA verbs only** |
| **Memory plan** | per-kernel | per-kernel | N/A | per-kernel | per-kernel | **whole-graph static, aliasing-aware** |
| **Persistent kernels** | no | no | N/A | manual | manual | **first-class (LAYER FuseKind)** |
| **Symbolic shapes** | limited | via guards+retrace | N/A | limited | no | **first-class via Expr + SymbolTable ranges** |
| **Dynamic shapes** | retrace graph | retrace+guard invalidate | N/A | specialize+cache | none | **symbolic Expr + bucketed specialize cache** |
| **Distribution** | GSPMD annotations | none (DDP external) | N/A | none | none | **compiler pass, 5D auto-tune, CNTP collectives** |
| **Runtime overhead/op** | ~1-10μs wrapper | ~1-3μs Python | ~0 (kernel only) | ~0 | ~0 | **~2-5ns shadow handle** |
| **Compile time (1k nodes)** | 30-180s | 5-30s | 1-5s (per kernel) | 2-10s (per kernel) | seconds-hours | **~100-300ms cold; ~10ms warm** |
| **Determinism** | partial | no | yes (at SASS level, per-chip) | no | yes | **bit-exact cross-hardware via pinned NumericalRecipe** |
| **Cross-vendor numerics** | no (vendor-library drift) | no | N/A | no | N/A | **bit-exact or ULP-bounded, enforced by CI** |
| **Calibration** | none | none | none | none | profile-based | **continuous via mimic::probe_counters per backend** |
| **Training/inference split** | separate stacks | separate paths | N/A | same | N/A | **same IR, same compiler** |
| **Rollback / bisect** | no | no | N/A | no | no | **Merkle-hash bisect, DAG branch A/B** |
| **Ecosystem cache** | no | no | N/A | no | no | **three-level content-addressed (L1 IR002 federation-shareable)** |
| **Proof hook** | no | no | no | no | no | **F*X integration point (future, 2028+)** |
| **Mixed-vendor training** | no | no | no | no | no | **NVIDIA + AMD + TPU + Trainium in one cluster, bit-determinism** |
| **Multi-chip spin-up** | MPI-style, fixed world | manual DDP | N/A | manual | N/A | **Canopy gossip + dynamic membership** |

#### 28.0.1 Event Tensor (ETC) — closest prior art

The closest-comparable published system is Event Tensor Compiler (ETC), Jin et al., *MLSys 2026* (arXiv 2604.13327). ETC validates the megakernel-with-fine-grained-sync approach we're also pursuing, beating cuBLAS+NCCL by 1.4× on fused GEMM + Reduce-Scatter and vLLM by 1.48× on Qwen3-30B-A3B inference at batch=1. Warmup time: ETC 35s vs vLLM 123s vs SGLang 583s — AOT compilation of shape-symbolic megakernels vs JIT + CUDA-Graph recapture.

| Dimension | Event Tensor (ETC) | Crucible |
|---|---|---|
| Megakernel approach | Yes, TVM-based compiler | Yes, Forge+Mimic emit pushbuffer with embedded kernels |
| AOT compilation | Yes, shape-symbolic | Yes, content-addressed + shape buckets (§F.4) |
| Sync primitive | `Event Tensor` (multi-dim counter-semaphore array) | mbarriers + pushbuffer semaphores + pinned host semaphores (per vendor) |
| Static scheduling | Per-SM task queue + counter semaphores | Pushbuffer per green-context channel + PatchPoints |
| Dynamic scheduling | On-GPU scheduler with push/pop, centralized queue | LoopNode + BranchNode via device pushbuffer jumps; on-GPU scheduler as escape valve (CRUCIBLE.md §3.9) |
| Data-dependent (MoE) | `topk` tensor drives event count updates | MOE_ROUTE KernelKind + EVENT_TENSOR PatchPoint (§18.8) |
| Warmup | 35s AOT | <5s (cache hit from L1 IR002 + Genesis Kernel Pack) |
| Distributed | Single-node B200 benchmarks only | Multi-node via CNTP + Z3 5D partitioning (§25.6) |
| Collectives in megakernel | Fused in (GEMM + Reduce-Scatter) | First-class; CNTP pushbuffer-embedded (MIMIC.md §37.5) |
| Per-vendor | NVIDIA TVM via CUDA PTX only | NV + AM + TPU + TRN + CPU, each with native ISA |
| Cross-vendor numerics | Not addressed | Four-tier determinism (§19) with CI enforcement |

**Crucible commits**: match or exceed ETC's 1.4×/1.48× speedups via the multi-vendor + direct-ISA + CNTP extensions. Cross-node factor beyond ETC's single-node results is where our Z3 partition solver (§25.6, §25.8) delivers additional wins.

Honest performance claims (not yet validated at scale, stated as goals):

- **Compile time**: 30-100× faster than Inductor, 100-500× faster than XLA on cold cache. Cache hits: ~1μs lookup vs their 5-60s recompile.
- **Runtime (hot kernel)**: 20-40% faster than vendor-library kernels on Forge-emitted workloads. Direct ISA + MAP-Elites + analytical cost model. First-party numerics, no library-version drift.
- **Runtime (small kernel)**: 5-20× faster than Inductor. Shadow handles (2ns) vs Python wrapper (~2μs).
- **Runtime (full model end-to-end)**: 1.5-3× faster than vendor-optimized stack. Persistent fusion + captured graph + whole-graph memory plan.
- **Memory**: 15-35% less than vendor stack. Static plan + adaptive checkpoint + no vendor-allocator overhead.
- **Chip saturation**: 80-92% typical vs 45-65% for vendor-library-backed paths. MAP-Elites + calibration-driven.
- **Iteration reproducibility**: bit-exact where recipe permits, vs ~5% drift in current vendor-library stacks.
- **Cross-vendor equivalence**: ≤2 ULP at FP32, ≤4 ULP at FP16 across NV / AM / TPU / TRN / CPU.

These are the targets. The build plan has validation milestones per target.

### 28.1 Ten things Forge + Mimic do that no existing stack does

1. **Cross-program cross-vendor kernel cache** — IR002 at L1 is vendor-portable.
2. **Multi-layer persistent fusion** — transformer block as one kernel with register-resident weights.
3. **MAP-Elites kernel catalog per region per chip** — per-vendor MAP-Elites inside Mimic.
4. **Whole-graph fusion DP/ILP** — global optimum, not greedy.
5. **Analytical cost model at every decision** — per-vendor simulator, no runtime autotune.
6. **Direct vendor-ISA emission across all targets** — no PTX, no HLO delegation, no library calls, no vendor runtime libraries.
7. **Continuous calibration per vendor** — mimic::probe_counters drives cost-model updates; accuracy preserved over hardware lifetime.
8. **Distribution as a compiler pass over CNTP** — 5D parallelism auto-tuned, no NCCL/RCCL dependency.
9. **Bit-deterministic cross-vendor numerics** — NumericalRecipe pinned at IR002 honored by every backend, CI-verified.
10. **Sub-10ns per-op runtime** — shadow handles + captured plan. Zero Python, zero vendor-library overhead.

---

## 29. Size Budget and Build Plan

### 29.1 LoC budget — Forge (vendor-agnostic)

| Component | LoC |
|---|---:|
| Crucible integration bridge | 5K |
| Phase A (INGEST, IR001) | 3K |
| Phase B (ANALYZE, IR001) | 4K |
| Phase C (REWRITE, 10 sub-passes, IR001) | 8K |
| Phase D (FUSE, DP+ILP+multi-layer, IR001) | 8K |
| Phase E (LOWER_TO_KERNELS, IR001→IR002, kernel template matchers) | 8K |
| Phase F (TILE, IR002) | 3K |
| Phase G (MEMPLAN, IR002) | 3K |
| Phase H (COMPILE dispatcher to Mimic) | 2K |
| Phase I (SCHEDULE, abstract launch records) | 3K |
| Phase J (EMIT, ExecutionPlan serialize) | 4K |
| Phase K (DISTRIBUTE, 5D ILP + collective insertion) | 6K |
| Phase L (VALIDATE orchestration) | 2K |
| PassManager core | 2K |
| IR002 (KernelGraph + KernelNode + attrs pools + RecipePool + TilePool) | 6K |
| Recipe registry (loader + picker + JSON schema) | 2K |
| Abstract TargetCaps + dispatch to vendor-specific | 1K |
| **Forge subtotal** | **~70K** |

### 29.2 LoC budget — Mimic shared core

| Component | LoC |
|---|---:|
| Core (effect tokens, arena integration, SimResult, Insight, Tier) | 3K |
| Shared simulator framework (three tiers, scheduler, event queue, memory model skeleton) | 12K |
| MAP-Elites driver + archive + mutation dispatcher | 8K |
| CompiledKernel + opaque handle management | 2K |
| Cross-vendor CI harness (numerics equivalence verifier) | 3K |
| Calibration harness skeleton + Measurements struct + drift analysis | 5K |
| Shared math-template framework (Newton-Raphson DDIV/DSQRT/IDIV) | 2K |
| **Mimic core subtotal** | **~35K** |

### 29.3 LoC budget — per-vendor Mimic backend

Each supported chip family is a self-contained backend. Costs stack per vendor:

| Component (per vendor) | LoC | Notes |
|---|---:|---|
| IR003* spec + builder + verifier | 5K | |
| Vendor ISA emitter + per-chip tables | 10K | nv: Mercury SASS; am: AMDGPU ISA; trn: NeuronCore bytecode; tpu: TPU exec; cer: CSL |
| Vendor binary-format writer | 5K | cubin / HSACO / NEFF / TPU-exec / CSL |
| Decoder + disassembler | 6K | round-trip correctness verification |
| Per-vendor sim tiers (memory, scheduler, async ops) | 15K | reuses Mimic shared framework |
| Lower IR002→IR003* (CSSA, addr-space, regalloc, schedule) | 10K | |
| Peephole rules per vendor | 5K | ~150 rules typical |
| Math template instantiations | 3K | per-vendor realization |
| MAP-Elites mutations + insights (vendor-specific kinds) | 6K | extends shared |
| Calibration microbenchmarks | 5K | per-chip |
| Per-chip TargetCaps + data JSON files | 3K + 2K data | per SKU |
| **Runtime library** (replaces libnrt / libcuda / libtpu / libhsa) | 8K | kernel driver wrapper, mem mgmt, submission, completion |
| **Collective library** (replaces NCCL / RCCL / libnccom / hcoll) | 5K | ring/tree/HD/hier over transport |
| Genesis Kernel Pack builder (CI-compiles canonical workload set per chip) | 2K | |
| **Per-vendor subtotal** | **~85-100K** | |

### 29.4 CNTP (vendor-agnostic transport + collective)

| Component | LoC |
|---|---:|
| Transport abstract + TCP + io_uring | 3K |
| RDMA (libibverbs for RoCE v2 / InfiniBand) | 6K |
| NVLink + NVSHMEM wrapper | 3K |
| Router + topology state | 3K |
| Gossip (SWIM + Lifeguard) | 3K |
| Raft-scoped consensus | 5K |
| Collectives (ring / tree / HD / hierarchical primitives) | 4K |
| NetworkOffload plane (SHARP + ICI + XGMI + Cerebras SwarmX + software-baseline providers) | 5K |
| **CNTP subtotal** | **~32K** |

### 29.5 Grand total for multi-vendor Crucible

| Deliverable | LoC |
|---|---:|
| Forge | 70K |
| Mimic shared core | 35K |
| CPU backend (reference eager + fallback) | 25K |
| NVIDIA backend (sm_90/100/103/110/120/121) | 100K |
| AMD backend (CDNA3/RDNA3) | 85K |
| Trainium backend (trn1/trn2/trn3) | 90K |
| TPU backend (v5p/v5e/v6e/v7) | 95K |
| Cerebras backend (future) | 80K |
| CNTP | 32K |
| Reference eager (pure C++26, all 147 CKernels) | 20K |
| Tests (numerics CI matrix, pipeline tests, per-backend) | 30K |
| Examples + docs | 8K |
| **Grand total — first-wave multi-vendor** | **~670K** |
| **NVIDIA-only core first wave** (Forge + core + nv + CNTP + CPU ref + tests) | **~320K** |

Compare to: ~5-8 M LoC across the traditional vendor-compiler + runtime + collective stacks being replaced. Net savings: **20-25× smaller codebase owned forever, zero vendor-library runtime deps.**

### 29.6 Build plan — dependency-ordered

Target: within ~30 months a small team ships NV + AM + TRN + TPU + CPU backends with bit-exact cross-vendor numerics.

**M1-M2: IR002 scaffolding + CPU backend foundation**
- IR002 headers (`crucible/ir002/`), RecipePool, TilePool, attrs pools
- Forge Phase E: IR001→IR002 lowering with kernel template matchers for GEMM, POINTWISE, REDUCE, NORM, ATTENTION, SOFTMAX, EMBEDDING
- Recipe registry skeleton + 15 canonical recipes
- CPU backend (x86_64 AVX512 first): reference-eager implementations of all 147 CKernels + IR002→IR003CPU minimal emitter
- Forge Phase F-G on IR002 using CPU backend's tiles

**Milestone M1**: a PyTorch training run of a small MLP executes through Crucible + Forge + Mimic-CPU end-to-end, produces correct numerics.

**M3-M4: Mimic shared core + NV hello-world**
- Shared simulator framework (event queue, scheduler skeleton, memory model skeleton)
- MAP-Elites driver + archive
- NV backend starter: IR003NV spec, hand-coded SASS for one GEMM shape, cubin emission
- NV runtime library starter: `/dev/nvidia*` ioctl, pool allocator, kernel submit, completion

**Milestone M2**: a hand-coded IR002 GEMM kernel compiles to cubin via Forge+Mimic-NV, runs on H100, produces numerically-identical output to CPU reference.

**M5-M9: NV backend — production-grade**
- IR003NV full spec, emitter, decoder
- Three-tier NV simulator calibrated to 95% on GEMM / attention / norm kernels
- MAP-Elites mutations + insights for NV
- NV collectives over NVLink+RDMA (replaces NCCL)
- Calibration harness for Hopper + Blackwell

**Milestone M3**: Forge+Mimic-NV compiles GPT-2-class model, runs training on 8×H100 with NV-native collectives, beats Triton+NCCL baseline on ≥60% of kernel shapes, bit-deterministic across runs.

**M10-M14: AMD backend**
- IR003AM (uses LLVM-AMDGPU for ISA selection, our own HSACO writer + runtime)
- AMD simulator calibrated for CDNA3
- AMD collectives over XGMI+RoCE (replaces RCCL)
- Mixed NV+AM training demo

**Milestone M4**: Same GPT-2 model trains on mixed 4×H100 + 4×MI300X cluster; gradients bit-identical step-by-step vs NV-only; both backends hit ≥90% of their single-vendor throughput.

**M15-M20: Trainium backend**
- neuronx-cc RE pulls out the Neuron ISA, NEFF format, NeuronCore engine protocols
- IR003AWS direct-emission (no delegation)
- Trainium simulator calibrated against real trn2
- EFA + NeuronLink collectives (replaces libnccom)

**Milestone M5**: Crucible runs same model on trn2, bit-exact-equivalent to NV and AMD when recipe permits.

**M21-M26: TPU backend**
- libtpu RE pulls out TPU executable format + MXU sequencing
- IR003TPU direct-emission
- ICI-native collectives
- TPU pod support up to v7

**Milestone M6**: Four-way fleet training demo — same Llama-class model on H100 + MI300X + TPU v5p + trn2, gradient bit-match under BITEXACT recipe, perf within 10% of single-vendor baseline on each.

**M27+: Consolidation, Cerebras, Genesis Kernel Pack production**
- Fill in remaining kernel kinds (SSM, MoE, FlashAttention-3 patterns, sparse)
- Cerebras backend (optional)
- Genesis Kernel Pack CI for all supported chips
- Public release

**Parallel tracks integrated into the above**

Additional milestones covering post-original-thesis commitments (from conversation-driven design additions):

- **M3 end**: ExecutionPlan struct + initial PatchPoint kinds (SCALAR, SLOT_PTR, SHAPE_DIM, RNG_COUNTER) on CPU backend. Replay-determinism CI validates PatchPoint writes produce predictable plan hashes.
- **M5**: Full PatchPoint taxonomy (8 kinds including EVENT_TENSOR). ChainEdge semaphore pool. §J.5 / §J.6 pushbuffer-layout spec validated on NV.
- **M5**: Four-tier determinism (UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT) with recipe registry `tc_shape_constraint` field. CPU reference is STRICT oracle.
- **M6**: Extension-point body inlining (§18.7) lands on NV. FlashAttention-3 variant with user `score_mod` at ≥72% H100 MFU.
- **M7**: ExecutionAttrs warp-spec fields (§18.3.1). MAP-Elites archive extended with warp_spec_split × reg_alloc_policy axes. Mimic-NV emits setmaxnreg.
- **M7**: Hybrid search mode (§22.8) with real-hardware validation of top-K candidates. HYBRID runs in cross-vendor CI by default.
- **M8**: Z3 joint partition search (§25.6) with topology benchmarking. CNTP collective-benchmark table populated and online-updated.
- **M8**: Asymmetric-fleet Z3 policies (§25.8). STRICT_UNIFORM / CAPACITY_WEIGHTED / TIERED. Mixed Blackwell CI test (PRO 6000 + 5090).
- **M9**: Scheduling cost model (§25.7) with MFU gap decomposition. Augur-driven MFU attribution.
- **M10**: MFU CI gate — validate 55-65% target on Llama-70B 8×H100 (§25.9.6).
- **M11+**: Event Tensor (ETC) cross-validation. Reproduce published 1.4× / 1.48× speedups as lower bound.

### 29.7 Key risks and mitigations

1. **Per-vendor ISA RE timeline slippage** — Trainium or TPU RE takes longer than estimated. Mitigation: StableHLO delegation path as a transitional mode per backend (fallback, explicitly marked non-native so user can see it's vendor-compiler-in-the-loop). Ships earlier, retires when native emission lands.
2. **Cross-vendor tolerance violation on some recipe** — CI finds a recipe where ULP tolerance breaks. Mitigation: either (a) fix the implementation on the failing backend, (b) mark the recipe `emulated_on` that backend with a tax, or (c) document tolerance widening and require explicit user opt-in.
3. **Kernel driver ioctl protocol churn on a vendor** — a driver update breaks our runtime. Mitigation: thin adapter per driver major version; old versions stay supported via shim. Kernel-driver API stability is much better than vendor-library API stability historically; we monitor, adapt quickly.
4. **MAP-Elites doesn't converge on some kernel class** — fallback to pareto-set heuristic from the Genesis pack. Never fails to produce a kernel; worst case ships the seed.
5. **Cache miss cold start on new model** — Genesis Kernel Pack covers 80% of common shapes from iteration 0. Rare-shape misses compile in the background while foreground runs via reference eager.
6. **Numerical CI matrix explodes** — cap pairwise comparisons to (native_on ∩ priority_vendors); a recipe on all 6 backends is 15 pairs, tractable. Tiered priority: NV+AM+CPU always; TPU+TRN+CER on release gates only.

---

## 30. Open Questions Deferred

Questions not resolved in this document that need resolution before or during implementation:

1. **MAP-Elites behavior axes per vendor**. Each Mimic backend chooses 4-6 axes appropriate to its hardware (e.g., NV might use occupancy / register pressure / smem pressure / pipeline depth / MMA shape family / warpgroup split; TPU might use occupancy / VMEM pressure / ICI bucket / MXU utilization). Axes are per-backend configuration; Forge is unaware.

2. **Recompile throttling**. If Phase L triggers recompiles frequently (multi-tenant workload drift), the background thread could thrash. Throttle: max one recompile per kernel per 5 minutes unless drastic drift (>30%). Parameterize.

3. **Cross-region optimization**. Kernel fusion across IR001 FusedRegion boundaries (stream pipelining across independent subgraphs) is Phase D-ish but crosses boundaries. Defer to a future Phase D.7 or handle in Phase I.

4. **Differential compilation**. When a graph changes by one node, can Forge incrementally recompile only the affected kernels? Yes via content-addressed caching (unchanged hashes skip). For cascading changes, the theoretical minimum work is less than naive re-run. Defer.

5. **Dynamic shapes beyond bucketed capture**. True programs with unbounded dynamic shapes (variable-length captions, streaming) need more than bucketed specialization. Future extension: symbolic dispatch — compile a parameterized kernel once, pass shape parameters at launch. Per-vendor backends do this natively where supported; abstract scheme needs design.

6. **F\*X integration surface**. When F\*X becomes available (2028+), it provides SMT-proven proofs for memory-plan correctness, fusion legality, recipe soundness, and kernel-optimality within restricted spaces. Design Forge's hooks as nullable function pointers (`fx::layout_solve`, `fx::is_recipe_safe`, `fx::enumerate_safe_tiles`); ship v1 without F\*X.

7. **OPAQUE-class Crucible ops**. When a CKernelId is OPAQUE (not in the 147), Phase E emits a `CUSTOM` KernelNode that wraps the IR001 nodes directly. The per-vendor backend emits a generic loop nest. No vendor library fallback — we would rather be slow-but-correct on exotic ops than depend on cuBLAS et al.

8. **Mimic's accurate tier per vendor**. Cycle-accurate simulator for calibration validation, debugging, and small high-stakes kernels. 1-10 kernels/sec. Per-vendor backends implement their own; shared framework reduces duplication.

9. **Cross-architectural fusion**. Fusion across Mimic backends (NV kernel chained to TRN kernel via host RAM) is Phase K material. Nontrivial; rare in practice; defer.

10. **Precompiled fragment handling per vendor**. FP64 subnormal handling, integer divide-by-zero, NaN propagation — small specialized routines live per-vendor in `mimic/<vendor>/precompiled/`. Built during per-vendor calibration, linked as cold sections of compiled kernels.

11. **Fleet intersection on upgrading clusters**. When a cluster partially upgrades (half H200 → H300), recipe intersection changes mid-training. Design decision: atomic epoch swap at iteration boundary, Raft-committed. Open question is batch size during the swap interval.

12. **Offline Genesis Kernel Pack regeneration**. When a new vendor ISA variant appears (firmware update, new chip SKU), Genesis packs need rebuilding. CI workflow TBD; per-chip incremental rebuild preferred to full-matrix.

---

## 31. Glossary

**IR001** — Crucible's tensor-level IR. Graph + Inst + Expr + CKernel. Pure semantics, no hardware concepts. Described in Crucible CLAUDE.md L2, L6, L7.

**IR002** — Forge's portable kernel IR. KernelGraph of KernelNode, each with pinned NumericalRecipe, committed layout, TileSpec. Vendor-neutral; same IR002 produces equivalent results on any supported chip.

**IR003\*** — Mimic's per-vendor machine IR. One per backend: IR003NV (NVIDIA), IR003AM (AMD), IR003TPU (Google TPU), IR003TRN (AWS Trainium), IR003CER (Cerebras), IR003CPU (CPU reference). Contains vendor ISA + register allocation + schedule. Lives inside `mimic/<vendor>/`; Forge never touches it.

**KernelKind** — the 21 kernel families of IR002: GEMM, BMM, CONV, ATTENTION, SOFTMAX, NORM, REDUCE, SCAN, POINTWISE, GATHER_SCATTER, EMBEDDING, RNG, COLLECTIVE, SSM, DEQUANT_GEMM, MOE_ROUTE, RAGGED_ATTN, PAGED_ATTN, FUSED_COMPOUND, OPAQUE_EXTERN, CUSTOM.

**KernelNode** — one kernel instance at IR002 level. 64B struct. Kind + attrs + recipe + tile + slot bindings + content_hash.

**NumericalRecipe** — 16B interned struct pinning algorithmic choices: accumulator dtype, reduction order, rounding mode, scale policy, softmax recurrence variant, determinism guarantee. Every IR003* backend honors the recipe exactly. Cross-vendor numerical equivalence is guaranteed by recipe pinning + CI enforcement.

**Recipe registry** — `crucible/data/recipes.json`. Global catalog of recipes with per-chip `native_on` bitmaps. Checked into git. Used by Phase E's fleet-intersection picker.

**TileSpec** — 32B interned struct with concrete or symbolic tile dims and abstract resource estimates. Vendor-neutral; the per-vendor backend maps abstract tiles to native hardware tiles.

**FusedRegion** — a subgraph of IR001 that Phase D decided should become one kernel. Lowered to a (usually single) KernelNode by Phase E.

**CompiledKernel** — Mimic's opaque output per kernel. Vendor binary bytes + predicted cycles + insights. Forge never inspects bytes.

**ExecutionPlan** — Forge's final output (Phase J). Memory plan + CompiledKernel handles + abstract launch records + guards. Serializable, Cipher-backed.

**Abstract TargetCaps** — 64B struct with vendor-neutral capability and budget fields plus opaque `vendor_specific` pointer. Forge reads only abstract fields.

**Per-vendor TargetCaps<Vendor>** — Mimic's extended caps for a specific vendor. Lives inside `mimic/<vendor>/`.

**MAP-Elites** — the evolutionary kernel-search algorithm Mimic uses per-vendor. Behavior-descriptor grid; each cell holds the best candidate. Not used by Forge directly.

**Phases A–L** — Forge's 12 pipeline phases. A/B/C/D on IR001; E = IR001→IR002 boundary; F/G on IR002; H delegates to Mimic; I/J/K/L on ExecutionPlan / runtime.

**CSSA** — Conventional SSA. Resolves PHI semantics at SIMT reconvergence points. A correctness pass that runs inside Mimic during IR002→IR003* lowering (not in Forge).

**FuseKind** — the 7 IR002-level fusion categories: NONE, REGISTER, LOCAL (fast shared per-CTA), EPILOGUE, PROLOGUE, BROADCAST, LAYER. Vendor-specific extensions (CGA, TCGEN05_TMEM, etc.) live only inside `mimic/<vendor>/` and realize abstract LAYER/LOCAL kinds.

**CNTP** — Crucible Native Transport Protocol. Our ~28K-LoC replacement for NCCL+UCX+hcoll. Four layers: transport (NVLink/RDMA/TCP), gossip (SWIM), consensus (Raft-scoped), collectives (ring/tree/HD/hier). Plus optional `NetworkOffload` plane for SHARP/ICI/XGMI/etc.

**Genesis Kernel Pack** — per-chip precompiled seed of ~500 canonical kernels shipped with each Crucible installation. Loaded into L3 cache at startup. First-iteration compile for common shapes = cache hit from byte zero.

**KernelCache** — Crucible's three-level content-addressed kernel store. L1: IR002 snapshot (cross-vendor shareable). L2: IR003* snapshot (cross-chip within vendor family). L3: compiled bytes (chip-specific). Persistent via Cipher.

**ContentHash / KernelContentHash** — Crucible's 64-bit Merkle-style hashes. IR001 regions → ContentHash; IR002 kernels → KernelContentHash. Stable across refactors, deterministic across runs.

**Cipher** — Crucible's event-sourced persistent storage. Three storage tiers (hot RAM / warm NVMe / cold S3). Survives Relay reincarnation. Hosts all three kernel-cache levels.

**Canopy** — Crucible's mesh of Keepers. No master; Raft for critical consensus, SWIM for gossip. Fleet membership changes drive recipe intersection recomputation.

**Augur** — Crucible's continuous monitoring. Calls `mimic::predict` + `mimic::probe_counters` to measure drift; triggers cache invalidation + recompile.

**Meridian** — Crucible's startup calibration. Runs per-vendor microbenchmarks; populates abstract + vendor-specific TargetCaps.

**Vigil / Vessel / Relay / Keeper** — see Crucible CLAUDE.md.

**F*X** — graded dependent type theorem prover. Future (2028+). Forge carries nullable hook slots for SMT-proven layouts and kernel-optimality proofs.

**Mercury / SASS / WGMMA / TMA / tcgen05 / TMEM / DSMEM / MFMA / MXU / NeuronCore / CSL** — vendor-specific terms. Defined in MIMIC.md per backend. Forge has no awareness of them.

**KernelKind — 22 families** — post-Event-Tensor update adds OPTIMIZER to the 21-family set. Full list: GEMM, BMM, CONV, ATTENTION, PAGED_ATTN, RAGGED_ATTN, NORM, SOFTMAX, POINTWISE, REDUCE, SCAN, GATHER_SCATTER, EMBEDDING, RNG, COLLECTIVE, SSM, FUSED_COMPOUND, MOE_ROUTE, OPTIMIZER, OPAQUE_EXTERN, CUSTOM, DEQUANT_GEMM. See §18.1.

**PatchPoint** — a typed, named runtime-mutable value in an ExecutionPlan. Eight kinds: SCALAR, SLOT_PTR, SHAPE_DIM, RNG_COUNTER, CONDITIONAL, COUNTER_BUMP, SEMAPHORE_VALUE, EVENT_TENSOR. Patched via O(1) byte-width MMIO write; no recomposition. Emitted by Phase J.emit_patch_points. See §18.8.

**ChainEdge** — Plan-to-Plan semaphore link for training-step / pipeline / speculative-decoding sequencing. Allocated from per-Keeper SemaphorePool at Phase J.5; Raft-committed, Cipher-persistent. See §J.5.

**SearchMode** — enum in `CompileConfig`: MAP_ELITES (default, simulator fitness), HYBRID (MAP-Elites + real-hardware validation of top-K), BEAM (dev-only, real-hardware search). See §22.8.

**ExecutionAttrs** — 8-byte shared tail on every kind-specific attrs struct. Fields: warp_spec_split (8 values), reg_alloc_policy (STATIC / DYNAMIC_SETMAXNREG), sm_mask_handle, per-role register counts. Realizes DeepSeek-V3 warp specialization + green-context SM partitioning as first-class compile output. See §18.3.1.

**RegAllocPolicy** — enum: STATIC (fixed register count), DYNAMIC_SETMAXNREG (runtime rebalancing via NV setmaxnreg or vendor analog). Mimic-NV emission at MIMIC.md §15.5.

**Green context** — NV Hopper+ SM-partition handle indexed by `sm_mask_handle`. Crucible allocates per-role contexts (compute / dispatch / combine / scheduler) at Keeper init; kernels launched to different contexts run on disjoint SMs. See CRUCIBLE.md §14.9.

**BITEXACT_TC / BITEXACT_STRICT** — two of four ReductionDeterminism tiers (UNORDERED < ORDERED < BITEXACT_TC < BITEXACT_STRICT). TC: K≤8 tensor-core fragments + pinned outer scalar reduction; 0-1 ULP cross-vendor. STRICT: scalar FMA only; 0 ULP byte-identical; 10-50× slower. See §19.

**Asymmetric-fleet partitioning** — Z3 partition solver extended with per-node flops/vram/recipe heterogeneity. Policies: STRICT_UNIFORM, CAPACITY_WEIGHTED (default when StdDev/Mean flops > 0.15), TIERED (anchor + workers). See §25.8.

**Event Tensor (ETC)** — megakernel compiler from Jin et al., MLSys 2026 (arXiv 2604.13327). Closest published prior art. Crucible borrows EVENT_TENSOR pattern via PatchPoint kind; extends across vendors + distribution stack + direct-ISA emission. See §28.0.1.

**Megakernel** — single persistent GPU kernel handling multiple operators (forward pass or full inference step) with fine-grained intra-kernel synchronization instead of per-op launches. ETC and DeepSeek-V3 both target this pattern; Crucible's ExecutionPlan is the realization.

---

## Summary

Forge is the vendor-agnostic optimizer that takes Crucible's tensor Graph (IR001), lowers it through kernel-template matching and recipe pinning into portable kernel IR (IR002), and hands each kernel to Mimic's per-vendor backend for native ISA emission. Forge has **zero vendor code**. Mimic owns every line below IR002 — one self-contained subsystem per vendor, each writing native ISA, each running on top of a runtime library we built ourselves that talks to the kernel driver directly.

The design refuses every vendor-library dependency. No cuBLAS, no cuDNN, no NCCL, no RCCL, no hcoll, no libtpu, no libnrt, no libnccom. Crucible depends on the kernel driver, RDMA verbs, the Linux kernel, and the CPU. The vendor-proprietary surface area we inherit is the kernel driver ioctl protocol (stable, versioned) — nothing else.

The portability contract is **NumericalRecipe** pinned at IR002. Same IR002 kernel on H100, MI300X, TPU v5p, and trn2 produces bit-exact or ULP-bounded equivalent results because every backend realizes the same pinned algorithm. Mixed-vendor training is feasible, provably deterministic, and enforced by the cross-vendor numerics CI matrix.

Twelve design commitments beyond the original thesis, each documented in the sections cited:

1. **Structure and content are orthogonal IR axes** (§18.7). Extension-point `ComputeBody*` fields on every templated KernelKind let user-supplied arithmetic inline at IR002→IR003* lowering without sacrificing structural-kernel perf. FlashAttention-class speed for arbitrary attention, norm, optimizer, scan variants; research code compiles, not graph-breaks.
2. **Four-tier determinism** (§19). `UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT` covers the full spectrum from fastest to byte-identical cross-vendor. `BITEXACT_TC` with K≤8 tensor-core shapes is the pragmatic sweet spot for cross-vendor mixed-precision training at ~5-8% perf tax.
3. **Z3 joint partition search** (§25.6). Proven-optimal 5D partitioning with topology benchmarking, collective benchmark table, and online learning from Augur's per-collective timings. Scales to 1024+ nodes.
4. **Bucketed specialization + parametric fallback** (§F.4, §24.6). Dynamic shapes compile with ≤3% perf tax, never graph-break. Online bucket sub-specialization adapts to observed shape distributions.
5. **Scheduling cost model** (§25.7). Every partition decision carries an attribution of loss sources; Augur's MFU breakdown maps each loss category to a concrete lever Phase K or downstream passes can pull.
6. **OPTIMIZER as a first-class KernelKind** (§18.1). Adam, AdamW, Lion, Muon, Shampoo, SOAP, and user-defined optimizers share the same structural kernel with the update rule as extension-point body. Meta-gradients operate on hyperparameters via the standard backward-body mechanism.
7. **Federation-shareable L1 cache** including extension-point bodies (§18.6). Researchers who write identical bodies share compiled artifacts; the computation genome accumulates research variants across the ecosystem.
8. **PatchPoint taxonomy** (§18.8). Eight typed kinds (SCALAR / SLOT_PTR / SHAPE_DIM / RNG_COUNTER / CONDITIONAL / COUNTER_BUMP / SEMAPHORE_VALUE / EVENT_TENSOR) let every runtime-mutable Plan value become an O(1) MMIO write. No CUDA-Graph-style recapture; patch values change plan hash predictably; cache hits amortize training-loop costs.
9. **ChainEdge semaphore sequencing** (§J.5). Training-step → optimizer-step → data-advance plans compose via pinned-memory semaphores with zero host round-trip. Pool allocation Raft-committed, Cipher-persistent. Canonical patterns for training, pipeline parallelism, speculative decoding.
10. **Per-KernelKind pushbuffer layout** (§J.6). Byte-level specification of per-launch command stream. NV Hopper: 16 bytes per kernel launch (SEND_PCAS_A + SEND_SIGNALING_PCAS2_B). Full training step ≈ 13 KB pushbuffer + 47 KB constbank arena. Feeds Mimic's §15.4 per-vendor composers.
11. **Asymmetric-fleet Z3 partitioning** (§25.8). CAPACITY_WEIGHTED / STRICT_UNIFORM / TIERED policies for mixed consumer/datacenter, mixed generation, cross-vendor, cross-geographic fleets. CAPACITY_WEIGHTED default when `StdDev/Mean flops > 0.15`. Worked example: 1× RTX PRO 6000 + 8× 5090 = 58% predicted MFU on Llama-70B.
12. **ExecutionAttrs warp specialization + green-context SM partitioning** (§18.3.1). 8-byte shared tail on every attrs struct exposing `warp_spec_split` × `reg_alloc_policy` × `sm_mask_handle` as MAP-Elites search dimensions. Realizes DeepSeek-V3 setmaxnreg-class register rebalancing as first-class compile output. Measured +15 MFU points on FA-3.

What remains is:
- ~70K LoC Forge (vendor-agnostic optimizer)
- ~35K LoC Mimic shared core
- ~85-100K LoC per vendor backend (~3-4 backends as first wave)
- ~32K LoC CNTP transport + collectives
- ~20K LoC CPU reference eager

Total for NV-only first wave: ~320K LoC. Total for multi-vendor first wave (NV+AM+TPU+TRN+CPU): ~670K LoC. Compared to 5-8M LoC of traditional vendor-compiler + runtime + collective stacks replaced.

Build it in dependency order: CPU backend first (correctness oracle), then NV (leverage existing nvopen-tools RE), then AM (LLVM-AMDGPU open backend), then TRN + TPU (requires per-vendor ISA RE; SDKs pullable via pip). Ship CNTP in parallel starting M6. Cross-vendor numerics CI from day one.

Forge is the connector: Crucible's IR001 substrate above, Mimic's per-vendor silicon below, NumericalRecipe as the cross-vendor contract, KernelCache as the federation substrate. Build it in the order of the dependency chain. The first thing that works unlocks the rest.

---

*End of Forge design document. ~18,000 words. The vendor-agnostic optimizer half of Crucible's compilation plane; Mimic (per-vendor backends) is the matching doc.*
