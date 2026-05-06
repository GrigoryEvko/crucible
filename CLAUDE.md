# The Crucible Runtime
*Adaptive ML infrastructure.*

Three layers: **Hardware** — compute nodes, heterogeneous and replaceable. **The Model** — weights and computation graphs. **Crucible** — the runtime that abstracts hardware, persists state across node failures, and migrates to new devices.

Python describes. Crucible executes. The 492,000 lines of framework overhead between them become unnecessary. There is no training or inference — there is only a model in Crucible.

## Ontology

| Name | Role | Description |
|------|------|-------------|
| **Safety** | Foundation | The structural guarantee layer: 8 axioms (Init/Type/Null/Mem/Borrow/Thread/Leak/DetSafe), contracts (P2900), reflection (P2996), `safety/*.h` wrappers (Linear, Refined, Tagged, Secret, Permission, Session, ScopedView, Machine, Monotonic, AppendOnly, ConstantTime, WriteOnceNonNull, FinalBy/NotInherited), session-type stack, CSL permissions. Every higher layer inherits correctness from these primitives. |
| **Relay** | Body | Compute node inhabited by a Crucible daemon. Mortal. Replaceable. |
| **Keeper** | Spirit | Per-Relay daemon — self-healing, self-updating, autonomous. `crucible-keeper.service` starts at boot, discovers peers, joins mesh. Executes Augur's advice. |
| **Vigil** | Intellect | The model: DAG, weights, learned knowledge. Named for the Prothean AI. Never sleeps. |
| **Cipher** | Soul | Persistent state — DAG chain, weight snapshots, KernelCache (three-level: L1 IR002 vendor-neutral / L2 IR003\* per-vendor-family / L3 compiled bytes per-chip), RNG state, MAP-Elites archives, calibration data, recipe registry snapshots. Event-sourced. Survives death, reincarnates on new hardware. |
| **Canopy** | Collective | Mesh of Keepers — distributed awareness, gossip, consensus, self-healing. No master node. |
| **Vessel** | Interface | PyTorch — the 2,000+ ATen operators Crucible intercepts via the Dispatcher. |
| **Meridian** | Map | Startup calibration. Measured hardware truth. Discrete-search joint 5D partition optimization (topology, parallelism, communication, placement) over calibrated `CollectiveBenchmarks` + `TopologyMatrix`, driven by `mimic::fast_cost`. Re-solves on topology change or under Augur-detected congestion drift. No external SMT dependency — Crucible ships no Z3, no CVC, no proprietary solver; the partition optimizer is a bounded-depth branch-and-bound over the cost-model surface. |
| **Augur** | Sight | Continuous prediction, monitoring, model intelligence. Digital twin. Loss landscape analysis. Convergence bounds. Scaling laws. Bottleneck diagnosis. Recommendations engine. |
| **Crucible** | Whole | The organism. Everything together. |

---

## L0 — Safety Foundation

**The structural guarantee layer. Every higher layer inherits correctness from these primitives.**

Crucible has no proof-assistant source of truth. Correctness is won by three complementary disciplines: **contracts-enforced invariants** (P2900R14), **linear / refined / session-typed wrappers** over every resource, and **measurement** (Mimic MAP-Elites + calibrated simulators + cross-vendor CI — §L2, §L15). No SMT-proven optimal kernels; no proved-allocators-theorem. What we have instead:

**Eight safety axioms.** InitSafe, TypeSafe, NullSafe, MemSafe, BorrowSafe, ThreadSafe, LeakSafe, DetSafe. Every struct, every function, every edit audits all eight. Contracts (`pre`/`post`/`contract_assert`), erroneous behavior for uninit reads (P2795R5), reflection-driven hashing (P2996), strong IDs, `std::bit_cast`, saturation arithmetic. Detail catalog in §II of the Code Guide below.

**Safety wrappers — Graded foundation refactor (25_04_2026.md §2).** Eleven value-level wrappers split across `include/crucible/{safety,permissions,handles,sessions,bridges}/` and unified by a single algebraic substrate `Graded<Modality, Lattice, T>` in `include/crucible/algebra/Graded.h`. Every Graded-backed wrapper exposes a uniform diagnostic surface (`graded_type`, `lattice_type`, `value_type`, `modality`, `value_type_name()`, `lattice_name()`); the `GradedWrapper` concept in `algebra/GradedTrait.h` enforces the contract structurally. Adversarial cheat-detection harness at `test/test_concept_cheat_probe.cpp` (18 cheats, 4 architectural-limit admissions documented).

**Wrapper → substrate map** (canonical reference; full enumeration in `safety/Safety.h` umbrella):

| Wrapper | Substrate | Regime |
|---|---|---|
| `Linear<T>` | `Graded<Absolute, QttSemiring::At<One>, T>` | 1 (zero-cost EBO) |
| `Refined<Pred, T>` | `Graded<Absolute, BoolLattice<Pred>, T>` | 1 |
| `SealedRefined<Pred, T>` | same as Refined; minus `into()` (forces re-construct on mutate) | 1 |
| `Tagged<T, Source>` | `Graded<RelativeMonad, TrustLattice<Source>, T>` | 1 |
| `Secret<T>` | `Graded<Comonad, ConfLattice::At<Secret>, T>` | 1 |
| `Monotonic<T, Cmp>` | `Graded<Absolute, MonotoneLattice<T, Cmp>, T>` | 2 (T==element_type collapse) |
| `AppendOnly<T, Storage>` | `Graded<Absolute, SeqPrefixLattice<T>, Storage<T>>` | 3 (derived grade from container) |
| `Stale<T>` | `Graded<Absolute, StalenessSemiring, T>` | 4 (T + grade per instance) |
| `TimeOrdered<T, N, Tag>` | `Graded<Absolute, HappensBeforeLattice<N, Tag>, T>` | 4 |
| `SharedPermission<Tag>` | `Graded<Absolute, FractionalLattice, Tag>` (façade — atomic state in `SharedPermissionPool`) | 5 (proof-token, runtime carrier elsewhere) |

Five regimes (full taxonomy in `algebra/GradedTrait.h` doc-block): **regime-1** zero-cost EBO collapse · **regime-2** value-type-equals-element-type collapse · **regime-3** grade derived from container content · **regime-4** value + grade carried per instance · **regime-5** proof-token with runtime carrier elsewhere.

**Structural wrappers — deliberately not graded.** Nine wrappers follow non-graded disciplines (RAII, typestate, structural constraint) that don't fit the `Graded<M, L, T>` shape:
- `Permission<Tag>` + `permission_fork` — CSL frame-rule linear tokens (THREADING.md).
- `Session<Proto>` — type-state binary and MPST session types in `sessions/` (Honda 1998 / HYC 2008 / Gay-Hole 2005 / BSYZ22 crash-stop).
- `PermissionedSessionHandle<Proto, PS, Resource, LoopCtx>` (`sessions/PermissionedSession.h`, FOUND-C v1) — threads a CSL `PermSet<Tags...>` through session-protocol position; `Send<Transferable<T, X>, K>` consumes `Permission<X>` from PS, `Recv<Transferable<T, X>, K>` produces it, `close()` requires `PS == EmptyPermSet`. Loop body PS-balance + branch terminal PS convergence enforced statically.
- `producer_session<Channel>(handle&)` / `consumer_session<Channel>(handle&)` (`sessions/SpscSession.h`) — typed-session façade over `PermissionedSpscChannel`. First production-shape wiring of FOUND-C v1; covers TraceRing / CNTP-style streaming SPSC. EmptyPermSet by design (plain payloads, no wire-permission transfer); richer wirings use `establish_permissioned` directly with Transferable/Borrowed/Returned payloads.
- `PermissionedMetaLog<UserTag>` + `metalog_session::{mint_metalog_producer_session,mint_metalog_consumer_session}` (`concurrent/PermissionedMetaLog.h`, `sessions/MetaLogSession.h`) — role-typed foreground append / background drain façade over the production `MetaLog` TensorMeta side-channel; handles are pointer-sized and sessions use EmptyPermSet.
- `PermissionedChainEdge<Backend, UserTag>` + `chainedge_session::{mint_chainedge_signaler_session,mint_chainedge_waiter_session}` (`concurrent/ChainEdge.h`, `concurrent/PermissionedChainEdge.h`, `sessions/ChainEdgeSession.h`) — one-shot `SemaphoreSignal` Send/Recv facade over execution-plan ChainEdge semaphores; current `mimic::<vendor>::semaphore_signal/wait` surface is stubbed through the CPU oracle until real vendor backends land.
- `ScopedView<C, Tag>` — lifetime-bounded borrow for non-consuming inspection.
- `Machine<States>` — type-indexed state machines; illegal transitions are compile errors.
- `OwnedRegion<T, Tag>` — arena-backed exclusive region.
- `Pinned<T>` — address-stability marker (CRTP base).
- `Checked.h` — `checked_add`/`mul_sat`/etc. overflow-detecting primitives.
- `ConstantTime<T>` — branch-free primitives for crypto paths and Cipher key handling.
- `NotInherited<T>` / `FinalBy<T>` — structural non-extensibility.
- `Simd.h` — SIMD primitives.
- `Workload.h` — concurrency policy hint consumed by `AdaptiveScheduler`.

Plus `WriteOnce<T>` / `WriteOnceNonNull<T*>` / `BoundedMonotonic<T, Max>` / `OrderedAppendOnly<T, KeyFn>` / `AtomicMonotonic<T, Cmp>` — Mutation.h derivative wrappers documented as composable from migrated primitives or state-machine-shaped (full per-wrapper rationale in `safety/Safety.h` policy block).

**Verification harness.** `test/test_migration_verification.cpp` is a single TU asserting cross-cutting properties (sizeof preservation, forwarder fidelity, cross-composition, GradedWrapper concept satisfaction) for all 10 migrated wrappers + the SharedPermission façade. `test/test_concept_cheat_probe.cpp` runs 18 adversarial cheats against the concept; build only succeeds when every cheat is correctly rejected (or documented as architectural limit).

**Soundness via measurement, not proof.** Numerical correctness lives in the cross-vendor CI matrix (MIMIC.md §41): every IR002 kernel × recipe × backend runs on real silicon, outputs are compared pairwise against a CPU scalar-FMA oracle, tolerance enforced per the recipe's declared `ReductionDeterminism` tier (UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT). A backend that violates tolerance fails the build.

**No external SMT dependency.** Crucible ships no Z3, no CVC5, no third-party SMT solver. The `verify` CMake preset is reserved for an internal small-SMT solver (deferred — interim: contract-only enforcement at boundaries) that will discharge residual integer / Presburger obligations only — bounds, divisibility, modular arithmetic, the same narrow scope TVM Analyzer (PR #1367) uses. Default budget 5 ms per query; not on the hot path. Out of scope (and never planned): kernel-optimality proofs, floating-point reasoning, cost-model decidability. Those are measurement problems handled by the cross-vendor CI harness (MIMIC.md §41), not theorem proving.

**Capability tags (post-FOUND-B07 / METX-5 sweep).** `effects::Alloc / effects::IO / effects::Block` (the `cap::*` tags re-exported into the top-level `effects::` namespace) and `effects::Bg / effects::Init / effects::Test` context structs — capability tags on function signatures, zero runtime cost (one byte per cap, EBO-collapsed within contexts via `[[no_unique_address]]`). All defined in `effects/Capabilities.h`. These are NOT F\*X proof obligations; they are C++-level capabilities enforced at compile time. The legacy `fx::*` tree in `crucible/Effects.h` and the `compat/Fx.h` shim are deleted; production call sites use `effects::*` exclusively.

**Met(X) effect rows (FOUND-B, shipped).** `effects/EffectRow.h` ships `Row<Es...>`, `Subrow<R1, R2>` concept, `row_union_t / row_difference_t / row_intersection_t`. `effects/Computation.h` ships the `Computation<Row, T>` carrier with `mk / extract / lift / weaken / map / then` per Tang-Lindley POPL 2026 / 25_04_2026.md §3.2. `effects/Capabilities.h` defines the `Effect` enum (Alloc/IO/Block/Bg/Init/Test) AND the value-level `cap::*` tags AND the `Bg / Init / Test` context structs that mint them — one header serves every effect-system call site, hot or cold. Production-side `Subrow`-constrained-template signatures (replacing the cap-tag parameter form with `Computation<Row<...>, T>` returns) are the residual METX-AUDIT work; the cap-tag form is sufficient for every production hot path today. Tests: `test/test_effects.cpp` + `test/test_effects_compile.cpp`, both green.

---

## L1 — Hardware

**Compute hardware. Heterogeneous, replaceable.**

GPUs are ecosystems: tensor cores (1000 TFLOPS FP16 on H100), scalar ALUs (60 TFLOPS), four-level memory hierarchy (registers → shared memory → L2 → HBM), power envelopes. Gap between theoretical peak and achieved: 40-70%.

**Multi-vendor:** NVIDIA (sm_86/89/90/100), AMD (gfx1100/942), Intel XMX, Apple AMX, Google TPU MXU. Same computation described once in the Merkle DAG; different Mimic-compiled native-ISA kernels per (content_hash, device_capability). See MIMIC.md for per-vendor backends.

**Power management:** NVML exposes clocks, power, temperature, ECC errors. Memory-bound phases don't need full core clock — drop 30% for zero perf loss, significant savings.

**Health monitoring → Keeper:** ECC error trends, thermal throttling, clock degradation feed into the Keeper. A failing GPU gets load-reduced, data pre-replicated to healthy Relays (L13 Distribution, RAID). State is already replicated before failure completes. New hardware → fresh Keeper discovers mesh and Cipher → Mimic re-runs MAP-Elites (warm-started from the nearest-family archive in Cipher) for the new device → reshards for new topology → resumes exactly.

---

## L2 — Kernels

**Calibrated-optimal computation. Measured, not proved.**

Current frameworks: static lookup (op + dtype → library kernel). Same kernel for 64×64 and 8192×8192, A100 and 3090, contiguous and transposed. No adaptation.

**Forge + Mimic replace the vendor stack.** `Forge` (vendor-agnostic optimizer, FORGE.md) lowers IR001 tensor DAG to IR002 portable kernel DAG with pinned `NumericalRecipe` — same IR002 kernel produces ULP-bounded-equivalent or bit-exact results on every supported chip. `Mimic` (per-vendor backend, MIMIC.md) emits native ISA from IR002: `mimic/nv/` (Hopper/Blackwell SASS), `mimic/am/` (CDNA3+/RDNA3+ AMDGPU), `mimic/tpu/` (TPU executable), `mimic/trn/` (NEFF), `mimic/cpu/` (reference oracle). No vendor libraries: zero cuBLAS, zero cuDNN, zero NCCL, zero libtpu — only kernel-driver ioctls.

**MAP-Elites kernel search** replaces autotuning. Six behavior axes (occupancy, register usage, smem usage, pipeline depth, MMA shape family, warp-group split) × 8 buckets each = ~260K cells, typically 500-5K populated per kernel family. Per-vendor three-tier simulator (fast ~1-5 ms / medium ~10-30 ms / accurate ~100-500 ms) calibrated to 95-98% against real silicon via hardware-counter probes (CUPTI / rocprof / PJRT profiler / neuron-profile). Insight-driven mutations — structured diagnostics (WGMMA_UNDERUTILIZED, REGISTER_PRESSURE_HIGH, L2_QUEUE_SATURATED, ~40 kinds) map to concrete mutation operators. Hybrid mode validates top-K archive cells on real hardware.

**Cross-vendor numerics CI (MIMIC.md §41)** enforces the portability contract. Every (KernelKind × NumericalRecipe × target) triple compiled, executed, output-compared pairwise against CPU scalar-FMA oracle. `BITEXACT_STRICT` → 0 bytes diff; `BITEXACT_TC` → ≤1 ULP; `ORDERED` → per-recipe tolerance. A backend that violates tolerance fails the build.

**KernelCache:** maps (content_hash, device_capability) → CompiledKernel. Content-addressing: identical ops on identical shapes produce identical hashes. Reuse across iterations, runs, models sharing sub-computations, even organizations. Multiple variants coexist per hash; best selected per device, alternatives benchmarked during dead time. Cache grows monotonically across restarts. Lock-free open-addressing hash table — zero overhead on hot path.

**Stream parallelism:** DFG reveals independent ops → launch on different CUDA streams → concurrent SM execution. Schedule compiled statically from topological sort + earliest-start-time assignment. Zero scheduling overhead at runtime.

**Deterministic Philox RNG:** cuRAND is hardware-dependent — different sequences on H100 vs 3090. Crucible uses Philox4x32: counter-based, platform-independent, stateless. Each op derives key from `hash(master_counter, op_index, content_hash)`. Each thread: `philox(thread_idx, op_key)` — ~10 integer instructions in registers. For memory-bound kernels like dropout: runs free in otherwise-wasted ALU cycles. Same (counter, key) → same bits on any architecture.

**Kernel fusion:** adjacent ops with single producer-consumer chain fuse into one kernel keeping intermediates in registers/shared memory, eliminating HBM round trips. Decision from DFG topology at compile time.

KernelCache is part of the **Cipher** — write-once, persists across reincarnations.

---

## L3 — Memory

**Where tensors live. 2ns allocation vs 2000ns.**

PyTorch's CUDACachingAllocator: freelist search, splitting, coalescing, mutex contention. 200-2000ns/alloc. For 1000-op models: ~2000 allocs/iter × 500ns = 2ms pure overhead = 13% of a 15ms iteration.

**Crucible: static memory plan** from DFG lifetimes. Background thread computes offline:
- Birth = producer op index; Death = last consumer op index; Size = shape × dtype, aligned 512B
- Greedy interval-based offset assignment (first-fit on sorted-by-birth tensors)
- Output: `MemoryPlan { total_bytes, slots[]: (op_idx, port, offset, size) }`

One `cudaMalloc(total_bytes)` at iteration start. Every "allocation" = `base_ptr + offset`. ~2ns, no mutex, no fragmentation, no contention. Plan is read-only at runtime.

**Deterministic:** same DFG → same plan → same addresses → same kernel behavior. Eliminates PyTorch's history-dependent allocator non-determinism. **Arena allocator** for DAG metadata: bump-pointer, ~2ns/alloc, bulk reset. **Aliased tensors** (views, transposes): one offset for base storage, aliases reference with different (offset, sizes, strides). Zero-cost. **Dynamic shapes:** new plan built by background thread, swapped atomically at iteration boundary.

**Automatic activation checkpointing** from measured data:
```
For each forward activation needed in backward:
    if store_cost / recompute_cost > threshold: recompute
    else: store
```
Per-tensor, optimal, no manual `torch.utils.checkpoint()`. Threshold adapts to memory pressure.

**Per-Relay planning:** same DFG, different plans per device capacity (H100 80GB vs 3090 24GB vs MI300X 192GB). Memory heterogeneity handled by adapting the plan.

**OOM is structurally impossible.** Keeper has the plan BEFORE execution. One check: `plan.pool_bytes ≤ device_memory - reserved`. If it won't fit: adapt plan first (more checkpointing, smaller batch, offload optimizer state), then proceed. `cudaMalloc` never fails. **Predictive adaptation:** track pool_bytes growth across iterations, extrapolate, preemptively adapt before limits approach.

---

## L4 — Operations

**The atomic unit. What happens when Python says `x + y`.**

Every PyTorch op dispatches through the Dispatcher's priority-ordered function pointer table. `DispatchKey::Conductor` intercepts above backend keys.

**RECORD mode** (6 steps, ~20ns total):
1. Snapshot input TensorMeta (shapes, strides, dtype, device, data_ptr). Handle TensorList unpacking. Encode scalars as int64 (up to 5 inline).
2. Compute schema_hash (op name) and shape_hash (input sizes).
3. Execute eagerly via redispatch.
4. Snapshot output TensorMeta.
5. Append to MetaLog (SPSC buffer, 1M entries, ~144MB).
6. Record to TraceRing (64-byte cache-line-aligned entry).

**COMPILED mode** (~2ns total):
1. Advance op index
2. Check guard: `compiled_trace[idx].schema_hash == current?` — if not, DIVERGE
3. Push pre-allocated shadow handles (ConductorTensorImpl with correct metadata, pointing into memory plan)
4. Return. No execution, no allocation.

GPU executes compiled kernels asynchronously on streams, decoupled from Python.

**Graduated divergence detection:**
- schema_hash mismatch → hard diverge, immediate eager fallback
- shape_hash mismatch → hard diverge (dynamic shapes changed)
- scope_hash mismatch → soft warning (different module, same ATen op)
- callsite_hash mismatch → softest warning (refactored code, identical behavior)

Pre-emptive: prepare eager path before confirming compilation is broken.

**Matrix structure discovery per layer:**
- Full-rank → dense matmul
- Low-rank (r << d) → A(d×r)·B(r×d), 2× cheaper at r=d/4
- Near-Toeplitz → depthwise conv + correction, 10× cheaper
- Sparse (>95%) → cuSPARSE
- Block-diagonal → smaller independent matmuls

Replacements are DAG branches with quality verification.

**Communication ops** (all_reduce, all_gather, etc.) intercepted identically — same recording, timing, optimization pipeline. The recording pipeline is **event sourcing**: the Cipher persists the event log for deterministic replay and reincarnation.

No training/inference distinction at L4 — same fallback, same recording, same compiled execution.

---

## L5 — Tensors

**Metadata, shadow handles, and the latent space.**

**ConductorTensorImpl (shadow handle):** real PyTorch tensor with correct metadata (shape, strides, dtype, device) but storage points into pre-planned memory pool. Data written asynchronously by compiled kernels. Python holds the shadow, inspects metadata, passes to next op (which returns another shadow in COMPILED mode). Not a future — a full TensorImpl with `DispatchKey::Conductor`.

**Sync points** (the only moments Python blocks): `.item()`, `.cpu()`, `.numpy()`, `print()`, conditionals on values, unrecognized ops. Everything else is shadow. 1000 ops with 1 `loss.item()`: 999 shadow returns (~2μs) + 1 sync (~10μs). Python wall time: ~12μs vs ~15ms eager.

**TensorMeta:** 144 bytes/tensor — sizes[8], strides[8], data_ptr, ndim, dtype, device_type, device_idx. Lives in MetaLog parallel to TraceRing. Sparse tensor shadows (COO, CSR/CSC/BSR/BSC) extend the same pattern.

**Latent space is observable** (during recording, actual data is available):
- **Intrinsic dimensionality:** PCA on activations reveals effective rank per layer. A 4096-dim state might use only 600 dims → 3496 wasted.
- **Dead dimensions:** per-dimension variance < ε → carries zero information → maskable (20% savings if 847/4096 dead).
- **Representation collapse:** CKA ≈ 1.0 between adjacent layers → redundancy → prune or add auxiliary loss.
- **Representation steering:** direction vectors (mean truthful - mean hallucinated activations) added at inference for behavior control. One vector addition per layer, negligible cost.
- **Manifold Mixup:** interpolate hidden states between samples at intermediate layers → new training signal from latent geometry.
- **Tensor provenance:** complete causal ancestry through DFG — trace any output back to root cause.

Shadow handles are mode-agnostic: training and inference produce identical objects.

---

## L6 — Graphs

**The skeleton. Dataflow, aliases, edges, and cycles.**

**TraceGraph:** bidirectional CSR property graph from one iteration. Nodes = ops, edges = relationships:
- **DATA_FLOW:** data_ptr tracking via PtrMap (open-addressing, 8192 slots, stack-allocated). Output ptr matches input ptr → producer-consumer edge.
- **ALIAS:** same data_ptr from different ops → view/in-place → shared storage.

Each node carries: schema/shape/scope/callsite hashes, TensorMeta arrays, scalar args, grad/inference flags. Built in single pass, O(V+E) via counting sort. ~50-100μs for 1000 ops.

**IterationDetector:** K=5 schema_hash signature, two-match confirmation for iteration boundaries. Handles warmup.

**LoopNodes for cyclic computation:** wraps acyclic body with feedback edges + termination (Repeat(N) | Until(ε)):
- **Compiled recurrence:** RNN as one body × 1000 reps, no Python per timestep
- **Convergence execution:** DEQ fixed-points, diffusion denoising — stop when converged
- **Cross-iteration pipelining:** overlap N+1's forward with N's optimizer via double-buffering
- **Nested loops:** DiLoCo inner/outer as nested LoopNodes, independently compilable
- **Self-referential:** Crucible's own autotuning loop as a LoopNode

Graph's fixed execution order is a pillar of deterministic replay.

---

## L7 — The Merkle DAG

**Content-addressable, versioned computation graph.**

Central data structure. L1-L6 feed in, L8-L16 read/modify. L0 proves correctness. Simultaneously: computation specification, compilation cache key, guard system, versioning mechanism, and deployment artifact.

**RegionNodes:** compilable op sequences. **content_hash** = hash(schema_hashes, input shapes/strides/dtypes/devices, scalar values). Identical computation → identical hash, even across models. **merkle_hash** = content_hash + child hashes → O(1) equality for entire subtrees (like git commits).

**BranchNodes:** dynamic behavior. Guard = the op sequence itself. Mismatch at op N → branch arms for different paths. Both arms independently compilable. Shared suffixes share content_hashes and kernels.

BranchNodes are THE mechanism for everything that changes: architecture mutation (L10), attention replacement (L9), hyperparameter changes (L11), continuous learning (L14). Every adaptation is a branch. Every branch is versioned and rollbackable.

**KernelCache:** (content_hash, device_capability) → CompiledKernel. Lock-free reads. Persists across runs, models, organizations. The **computation genome** — every run enriches it.

**Atomic swaps:** background thread builds new DAG structures → one atomic pointer swap at iteration boundary → zero-downtime activation. Same mechanism for compilation activation, branch swaps, memory plan updates, topology changes, rollbacks. Coordinated across Canopy at same iteration boundary.

**The DAG IS the Vigil.** No torch.export(), ONNX, TorchScript. Same DAG trains and serves. Deploy = copy Cipher to Relay.

**The DAG IS the audit trail.** Root merkle_hash captures entire computation state. Divergence found in O(log N) by walking tree. Cryptographic provenance for regulatory compliance.

**Git operations on models:** diff (which regions changed), merge (non-overlapping clean, overlapping = conflict), bisect (binary search through versions for regression), cherry-pick (select specific region updates), blame (trace value through version history).

**LoopNodes in the DAG:** cycle semantics within acyclic hash framework. `merkle_hash = hash(body.content_hash ⊕ "loop" ⊕ feedback_signature ⊕ termination)`. Transforms DAG from computation snapshot to computation PROGRAM. Entire training run = one compact cyclic graph.

---

## L8 — Tokens

**Input granularity. Matching compute to information density.**

Fixed tokenization violates information theory. A blank wall gets same compute as a circuit diagram. Shannon: minimum bits = entropy. Crucible observes information density at runtime:

**Token merging (proven):** pairwise cosine similarity between adjacent representations after layer N. Similarity > threshold → merge (average). 40-50% reduction, <0.5% accuracy loss (Bolya et al. 2023). Crucible makes it **adaptive per-input per-layer** — ocean photo merges 80% at layer 2; circuit diagram merges 5%. Attention is O(n²), so 4× fewer tokens = 16× less attention. DAG BranchNode for merge/no-merge.

**Early exit per token (proven):** measure ||h_N - h_{N-1}|| per token. Below threshold → freeze, skip remaining layers. Bucket tokens into convergence groups for batch efficiency. Average tokens converge around layer 4-6: 50-60% compute savings.

**Adaptive patching (images):** quadtree decomposition by information content (gradient magnitude, frequency, entropy). Rock photo → 8-16 tokens. Blueprint → 256-512. Compiled as a LoopNode.

**Variable-length batching:** pack sequences contiguously, compile kernels for ragged shapes with known offsets. Complexity hidden below the model.

**Per-token precision:** high-information tokens in FP16, low-information in INT8/INT4. Separate kernels per precision group.

**Extensions:** video (delta frames like H.264), audio (coarse for silence, fine for transients), time series (one token for flat, many for spikes).

---

## L9 — Layers

**Attention replacement, local learning, per-layer gradient strategy.**

Crucible groups ops by scope_hash and analyzes each layer independently. Layers are NOT uniform.

**Attention head classification** (from recorded attention matrices, mutual information with input):
- **Positional (~60%):** diagonal band, content-independent → depthwise conv, O(n·k) vs O(n²), 32× cheaper for k=64, n=4096
- **Global (~15%):** attend to fixed landmarks (BOS, separators) → gather + broadcast, O(n), 4096× cheaper
- **Averaging (~10%):** high-entropy uniform attention → mean pooling, O(n)
- **Dead (~5-10%):** near-zero output/gradient → remove entirely
- **Content-routing (~10-15%):** sparse, input-dependent, genuinely need attention → keep, or replace with hash-routing / iterative message passing

**Iterative message passing** for routing heads: k-nearest-neighbor exchanges for log₂(n/k) rounds. O(n·k·log(n/k)) vs O(n²). For n=1M: 1100× cheaper. Hash-accelerated: LSH to find similar tokens per round.

Result: 144-head transformer evolves to ~85 sparse-attention + 15 conv + 20 pool + 10 removed + 14 message-passing. Total attention cost drops ~60%.

**Local losses:** insert per-layer learning signal via DAG modification — small MLP probes predicting final output. Gradient path: 1 layer deep, no vanishing. Options: predictive coding, contrastive (InfoNCE), reconstruction, Forward-Forward (Hinton 2022). Type can differ per layer.

**Per-layer gradient strategy** (from measured SNR, Jacobian rank, gradient norms):
- Last 2-4 layers: standard backprop (high SNR)
- Middle layers: K-FAC natural gradient (2-3× fewer steps, ~2× cost/step, curvature denoises)
- Early layers: synthetic gradients (near-zero real SNR, any approx equally good)
- Converged layers: freeze entirely

Strategy evolves: early training → mostly local losses/synthetic; late → mostly frozen, few layers with full backprop. **Selective backpropagation:** skip backward for layers with gradient norm < ε for N steps. 50-70% layers skippable late in training → 24-36% total time savings.

**Hessian-vector products** (Pearlmutter 1994): O(N) cost for Hv → per-parameter curvature (principled LR), top eigenvalues (saddle detection, sharpness), K-FAC factors (F ≈ A⊗G, natural gradient F⁻¹g). Periodic, not per-step.

**Adaptive bottlenecks:** effective rank 600 in 4096-dim → insert W_down(4096×600)·W_up(600×4096). ~3.4× cheaper. Dimension from measurement, adapts during training.

**NaN/Inf early kill:** lightweight `isfinite` checks at numerically sensitive points (~1μs). Catch instantly → rollback to previous iteration → skip bad batch. vs PyTorch: silent propagation, user notices 20 minutes later.

---

## L10 — Models

**Growing, pruning, evolving, composing the architecture itself.**

The DAG IS the architecture. Modifying the DAG IS architecture search. Every modification is a verified, rollbackable branch.

**Layer growing:** loss plateau + capacity analysis → insert layer at highest-gradient position → initialize (identity/distillation/random) → branch + verify → recompile.

**Layer pruning:** CKA >0.95 between adjacent layers or near-zero gradient → branch skipping redundant layer → verify → commit. Model SHRINKS as it converges.

**Width mutation:** effective rank 400 consistently → reduce hidden dim to 512 via PCA/SVD projection + adapter projections. Heterogeneous width shaped by data.

**Activation function evolution:** try alternatives per layer (SwiGLU, ReLU, GELU) → measure → per-layer optimal discovered empirically.

**Progressive growing:** start small (4 layers, d=512), grow on plateau, widen when needed, prune when converged. Size trajectory determined by data and loss, not human guess.

**Model composition:** DAG splicing. Vision encoder DAG + adapter + language model DAG = multimodal model. Sub-DAGs retain content_hashes → compiled kernels reused.

**Genetic evolution:** population of model variants → train → select → crossover (DAG splicing) → mutation (DAG branch) → repeat. KernelCache shared across population.

**Live surgery:** remove dead head while serving production → atomic swap → 6% faster, same quality, zero downtime.

---

## L11 — Training

**Hessian, meta-gradients, curriculum, and self-tuning.**

The entire training loop (forward + backward + optimizer) is in the DAG → observable, differentiable, modifiable.

**Meta-gradients:** lr → θ_{t+1} → val_loss. Compute ∂(val_loss)/∂(lr) via one additional backward pass. Same for weight_decay, β₁, β₂, ε. Hyperparameters tune themselves by gradient descent on validation loss. No grid/random/Bayesian search.

**Per-layer LR from curvature:** Hessian diagonal gives optimal lr ∝ 1/H_ii. Hybrid: Hessian for periodic calibration, Adam for step-to-step adaptation.

**K-FAC natural gradient:** F ≈ A⊗G per layer, tractable inverse. Steepest descent in distribution space. 2-3× fewer steps, ~2× cost/step. Activated where SNR is moderate.

**Curriculum learning:** per-sample loss observable → order by difficulty. Try random/hard-first/easy→hard for 100 steps each → keep best → re-evaluate. 20-40% faster convergence.

**Loss function evolution:** add/weight auxiliary losses, regularization terms. Meta-gradients on term weights. **Optimizer evolution:** Adam/AdaFactor/Lion/learned-update-rule as DAG branches → measure → keep best. Logical endpoint: optimizer IS a learned function trained by meta-gradients.

**Automatic mixed precision from measurement:** run each op in FP32 and FP16/BF16/TF32/INT8/FP8, measure per-op difference, pick cheapest precision maintaining quality. Per-model, per-op, per-training-stage. Not a static allow-list.

---

## L12 — Data

**Pipeline absorption, augmentation, and steering.**

Crucible dissolves the boundary between data loading and training.

**Backpressure:** measure GPU idle between iterations → signal DataLoader to prefetch more/less. **GPU-side augmentation:** tensor ops (crop, flip, jitter, blur) moved to GPU as compiled DAG ops. ~500μs CPU → ~5μs GPU. **Curriculum integration:** L11 measures difficulty → L12 reorders data stream.

**Manifold Mixup:** interpolate hidden states at layer K: h_mix = α·h_A + (1-α)·h_B → forward remainder → loss against interpolated label. Layer K chosen by linear probe accuracy. DAG modification at L7.

**Representation steering at inference:** add α × direction_vector to hidden state at optimal layer. No weight changes. Direction discovery automated: difference of means between desired/undesired behavior activations.

**Distribution shift monitoring:** KL divergence between current activations and training reference → trigger continuous learning (L14) or alert.

---

## L13 — Distribution

**Distributed mesh. Multiple nodes, shared state, no master.**

**Keeper mesh:** each Relay runs a Keeper, discovers peers via gossip. No master. Raft for critical state, CRDTs for eventually-consistent metrics. Any Keeper can propose changes.

**Spot-aware:** 30-second eviction → Keeper signals Canopy → mesh reshards to N-1 (redundant copies already exist) → Vigil continues from same step. New instance → Keeper discovers Canopy → loads Cipher → joins.

**Heterogeneous compute:** same Vigil, per-Relay compiled kernels (H100/3090/MI300X/A100 each optimized via L0+L2). Content-addressing handles naturally.

**LOR batch distribution:** micro-batches proportional to measured throughput. H100 gets 3× more than 3090. Both fully utilized. Gradients weighted by actual batch size.

**UCX multi-backend transport:** GPUDirect RDMA (NVIDIA), ROCm-aware RDMA (AMD), host-staged (TPU). Cross-vendor zero-CPU-staging. Not NCCL-locked.

**Adaptive topology:** continuous N×N latency/bandwidth probing → optimal algorithm per collective per message size (ring for bandwidth-bound, tree for latency-bound, recursive halving-doubling for balanced, direct for expert routing). Topology swaps atomically at iteration boundaries. Routes around degraded links.

**RAID-like redundancy (hot Cipher):** configurable overlap α (0=pure FSDP, 0.125=survive 1 failure at 12.5% overhead, 1.0=pure DDP). Redundancy updates pipelined into communication dead time. On Relay failure: ~100ms detection → surviving Relays already have shards → reshard in 2-5s → zero lost compute. Dynamic α: unhealthy Relays get higher neighbor α. Topology-aware placement across failure domains.

**DiLoCo enhancement:**
- Adaptive H from measured inter-island parameter drift
- Heterogeneous islands: different step counts, weighted by actual work
- Selective sync: skip small-delta parameters (60%+ bandwidth savings)
- Compressed pseudo-gradients: top-K + int8 quantization (50-100× reduction)
- Async outer sync: staleness-aware weighting, no barriers
- Hierarchical: NVLink every step / InfiniBand every 5 / WAN every 50, H auto-tuned per level

**5D parallelism auto-tuning:** measure actual per-dimension costs (TP all-gather, PP bubble, DP reduce-scatter, EP all-to-all, CP transfer) → simulate alternatives → try if predicted improvement exceeds threshold → commit or rollback. Configuration evolves during training.

---

## L14 — Lifecycle

**Continuous operation with persistent state.**

**No deployment.** The compiled DAG IS the runtime. Shadow handles work for training and inference. Deploy = copy Cipher to Relay. No export, no conversion, no coverage gaps.

**Continuous learning:** new data → forward (= inference response) → loss → backward → update → DAG branch verification (old weights arm A vs new weights arm B, validate, atomic swap if B ≥ A, discard if B < A). Built-in A/B testing. Instant rollback.

**Catastrophic forgetting prevention:** stable regions (unchanged content_hash, near-zero gradients) → frozen. New learning in new branches. Knowledge accumulates without interference.

**Live model surgery:** detect redundant layer → create pruned branch → verify → atomic swap while serving. Or grow capacity for new patterns. No downtime, no retraining.

**Deterministic reproducibility:** DAG fixes execution order, kernel selection, memory layout, communication topology, Philox RNG. Bit-identical runs. Enables exact reproducibility, regression testing, formal verification.

**Time-travel debugging:** DAG + periodic snapshots → replay to any step → extract any activation → trace any anomaly backward through DFG → root cause. "Why did loss spike at step 12,847?" → NaN at op 312 → gradient explosion → LR warmup ended too aggressively. Git blame for tensors.

**The Cipher (three tiers):**
- **Hot:** other Relays' RAM (from RAID redundancy). Single node failure → zero-cost recovery.
- **Warm:** local NVMe per Relay (1/N FSDP shard). Recovery from reboot: seconds.
- **Cold:** durable storage (S3/GCS). Recovery from total cluster failure: minutes.

Event-sourced: DAG chain (few KB/step) persisted every step, weight snapshots periodic. Recover to step T+500: load snapshot at T, replay 500 deterministically. Self-updating Keepers: download new binary, verify hash, swap atomically.

---

## L15 — Meridian+Augur

**Operational intelligence. Two time scales in one layer.**

Meridian = startup calibration (5-15s). Augur = continuous per-iteration monitoring.

**Meridian (startup):**
- GPU profiling: GEMM→actual TFLOPS, streaming copy→HBM/PCIe BW, NVML→power/temp/ECC/memory
- Network probing: N×N latency/bandwidth matrix, topology detection
- Per-vendor calibration microbenchmarks populate `TargetCaps` + `OpcodeLatencyTable`; Mimic's MAP-Elites search warm-starts from Cipher-persisted archives for the measured hardware
- Discrete-search joint 5D partition optimization (FORGE.md §25.6): TP×DP×PP×EP×CP factorization + schedule + bucket size + per-link weight assignment, minimizing predicted step time from `CollectiveBenchmarks` + `mimic::fast_cost` + per-link congestion telemetry. Bounded branch-and-bound over the calibrated cost surface; no external SMT solver.
- Output: complete device-specific kernel set + MeridianConfig. Re-probes on topology change.

**Augur (continuous):**
- Digital twin: DAG + Mimic kernel predictions + Meridian corrections → iteration prediction (±5-10%)
- Per-kernel bottleneck classification: COMPUTE/MEMORY_BW/COMMUNICATION/BUBBLE/IMBALANCE
- Predicted vs actual monitoring. >10% drift → diagnose → trigger Meridian recalibration
- Recommendations ranked by expected_speedup × confidence, tagged auto-hot/auto-cold/manual
- Model intelligence (periodic): Hessian spectrum (Lanczos), gradient health, effective rank (randomized SVD), CKA layer redundancy, convergence prediction, Chinchilla scaling laws

---

## L16 — Ecosystem

**Cross-run learning, computation genome, federated intelligence.**

**Computation genome:** shared KernelCache. Content-addressing means different models with same sub-computations hit the same kernels. Every training run enriches the cache. Network effects: value grows superlinearly with contributors. Docker Hub for GPU computation.

**Federated learning:** DiLoCo + differential privacy. Sites train locally, send noised pseudo-gradients (sensitivity bounded by known gradient norms). No raw data/gradients leave sites. Auditable via Merkle trail.

**Cross-Vigil transfer:** import DAG subgraphs between Vigils. Same content_hashes → zero compilation. Pre-trained weights load into imported regions. Components become reusable libraries — not just weights but self-describing computation fragments.

**Model marketplace:** content-addressed DAG fragments + KernelCache + verified quality metrics. Download, splice, verify, commit. Architectures from best-in-class components discovered ecosystem-wide.

**Hardware co-design:** aggregated KernelCache reveals real workload patterns (shape distributions, sparsity patterns, bottleneck frequencies). Feed to hardware designers → next-gen silicon optimized for actual workloads → per-vendor Mimic backend recalibrates + re-runs MAP-Elites for the new silicon → new data → co-evolution.

**What Crucible is not:** not intelligent, not AGI. A matmul is a matmul. It observes, compiles, adapts, distributes, heals, persists, evolves — mechanically, from measurements. The model determines the quality ceiling. Crucible removes infrastructure overhead so the model can reach its potential.

---

## Development Plan

**Phase 1: Foundation (DONE — 9.5K lines, 24 tests, Clang 22 + GCC 15)**

L4 Operations: TraceRing SPSC, MetaLog, recording pipeline. L6 Graphs: TraceGraph CSR. L7 Merkle DAG: RegionNode, BranchNode, content/merkle hashing. L3 Memory: MemoryPlan sweep-line, PoolAllocator. L4/L7 Compiled Tier 1: ReplayEngine, CrucibleContext, dispatch_op, divergence recovery. L2 Kernels: CKernel 146-op taxonomy. L14: Serialize/Deserialize, Cipher. L6 Graph IR: Graph.h, ExprPool, SymbolTable. L0 partial: Met(X) effect rows + cap tags (`effects/Capabilities.h` defining `effects::Alloc / IO / Block` and `Bg / Init / Test`, `effects/Computation.h`, `effects/EffectRow.h`), Reflect.h (reflect_hash, reflect_print). Vessel: PyTorch adapter.

**Phase 2a: Safety Foundation (IN FLIGHT)**

Goal: complete the L0 structural-guarantee layer — axioms, safety wrappers, session types, CSL permissions.

- **Safety wrappers** in `include/crucible/safety/` — Linear, Refined, Tagged, Secret, Permission, Session, ScopedView, Machine, Monotonic, AppendOnly, WriteOnceNonNull, FinalBy/NotInherited, ConstantTime. Zero runtime cost (`sizeof(Wrapper<T>) == sizeof(T)` under `-O3`).
- **Session-type stack** (12-layer binary + MPST at `sessions/Session*.h`): Honda 1998 binary, HYC 2008 MPST, Gay-Hole 2005 subtyping, SY19 parametric φ, GPPSY23 precise async, BSYZ22/BHYZ23 crash-stop, HYK24 association, PMY25 top-down async. ~9/12 milestones shipped (~8,400 lines + ~1,700 FOUND-C, 102 tests + 9 PSH integration tests + 10 negative-compile fixtures green); L9 CSL × session shipped as FOUND-C v1 (`sessions/PermissionedSession.h`). Remaining: L7 φ predicates (Task #346), async ⩽_a (#348), full coinductive merging (#381).
- **CSL permissions** (THREADING.md) — `Permission<Tag>`, `SharedPermission` + pool, `permission_fork` (CSL parallel rule as RAII fork-join), cache-tier cost model (L1/L2 → sequential, L3/DRAM → parallel).
- **Production refactors**: Vigil → Machine + Session, TraceRing → PermissionedSpscChannel, KernelCache → SwmrSession + ContentAddressed, Cipher tiers → Delegate + Tagged, CNTP layers → Session over Session. ~70 tracked tasks in the backlog.
- **Lean proofs** (Phase 5 of safety-integration plan): PermissionFlow, AssociationPreservation, StreamSessionLifetime, CrashFlow, SecretFlow. `lean/Crucible/` already has 36 modules / 1,312 theorems / zero sorry covering L0-L17.
- **`verify` preset (internal small SMT — deferred)** reserved for residual integer-arithmetic proof obligations only — scope matches TVM Analyzer PR #1367: bounds, divisibility, modular. No external solver dependency. Interim mode: contract enforcement only. Not a kernel-optimality engine.

**Phase 2b: Forge + Mimic Core (IN PARALLEL)**

Goal: vendor-agnostic optimizer + per-vendor backend framework per FORGE.md / MIMIC.md. No dependency on 2a; the two phases proceed in parallel.

- **IR002 scaffolding** (FORGE.md §18): `KernelGraph`, `KernelNode`, `NumericalRecipe` (interned), `TileSpec`, per-kind attrs pools, `ExecutionPlan`, PatchPoint taxonomy (8 kinds), ChainEdge semaphore pool.
- **Recipe registry** (FORGE.md §20) — `crucible/data/recipes.json` with four-tier determinism per recipe (UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT), `native_on` bitmap per chip, `tc_shape_constraint` for BITEXACT_TC recipes.
- **Forge 12-phase pipeline** (FORGE.md §5): INGEST → ANALYZE → REWRITE → FUSE → LOWER_TO_KERNELS → TILE → MEMPLAN → COMPILE → SCHEDULE → EMIT → DISTRIBUTE → VALIDATE. Hard wall-clock budgets per phase.
- **Mimic CPU reference backend first** (correctness oracle): x86_64 AVX512 / aarch64 NEON, scalar-FMA BITEXACT_STRICT always, every higher-tier recipe validated pairwise against CPU output.
- **Mimic NVIDIA backend** (M2-M9 of MIMIC.md build plan): IR003NV + SASS emitter + three-tier simulator + MAP-Elites + CUPTI calibration harness + runtime library (direct `/dev/nvidia*` ioctls, no libcuda) + collective library (CNTP, no NCCL).
- **Mimic AMD / TPU / Trainium backends** follow the same template (one self-contained subsystem per vendor).

**Phase 3: Meridian+Augur**

Goal: hardware calibration + continuous monitoring as one operational intelligence layer.

- GPU profiling + network probing at startup. Discrete-search joint 5D partition optimization (FORGE.md §25.6) picks topology from calibrated `CollectiveBenchmarks` + `mimic::fast_cost`. Per-link congestion telemetry (TX/RX bytes, drop rate, queue depth, sysctl-derived effective capacity) feeds into the cost surface so decisions adapt to heterogeneous-NIC fleets and live load.
- Digital twin: DAG + Mimic kernel predictions + calibration corrections → iteration prediction (±5-10%).
- Continuous monitoring, bottleneck diagnosis, recommendations engine. Augur drift detection triggers per-vendor Mimic recalibration when P95 residual > 10% for 100+ samples.
- Model intelligence: Hessian spectrum, gradient health, effective rank, CKA, scaling laws.

**Phase 4: Compiled Tier 2-3**

Goal: shadow-handle dispatch and pushbuffer replay — push the foreground past
recording into a model where the user-visible work per op is just metadata.

- Shadow handles: ConductorTensorImpl with metadata pointing into PoolAllocator.
- Batched kernel launch: accumulate MAP-Elites-selected kernels, one doorbell write per ExecutionPlan.
- Pushbuffer + PatchPoint + ChainEdge replay: plan composition per CRUCIBLE.md §11.9 and FORGE.md §J.6.

**Phase 5: Keeper + Canopy + Cipher**

Goal: distributed, self-healing, persistent, cross-run-shareable.

- Keeper daemon: systemd service, health monitoring, self-updating. Executes Augur's advice.
- Canopy mesh: SWIM gossip + Raft-scoped consensus, peer discovery. No master.
- Cipher: hot tier (RAID redundancy), warm tier (NVMe), cold tier (S3/GCS). Event-sourced. Three-level KernelCache: L1 IR002 snapshot federation-shareable cross-vendor; L2 IR003\* snapshot cross-chip within vendor family; L3 compiled bytes per-chip.
- TrainingCheckpoints (weights, optimizer, data cursor, seed, step_idx, fleet UUIDs at checkpoint) survive reincarnation. Hardware-specific kernels recompiled by Mimic on new hardware using the warm-started Cipher archive.

**Phase 6: L8-L12 Intelligence**

Goal: model-aware optimizations, guided by Augur, validated by cross-vendor CI.

- L8: Token merging, early exit, adaptive patching.
- L9: Attention head classification, local losses, per-layer gradient strategy.
- L10: Layer growing/pruning, width mutation, architecture evolution.
- L11: Meta-gradients, per-layer LR from curvature, optimizer evolution.
- L12: Curriculum learning, manifold mixup, pipeline absorption.
- All optimizations are DAG branches (L7). Forge Phase L + Augur measure improvement; the Keeper activates via atomic swap only if (a) the branch compiles cleanly through Forge + Mimic, (b) cross-vendor CI tolerance holds for the branch's recipe tier, and (c) Augur's predicted improvement > threshold.

---

Contracts discipline the code. Safety wrappers carry invariants in the type system. Measurement — MAP-Elites + calibrated simulators + cross-vendor CI — replaces proof as the correctness-witnessing mechanism. Meridian maps hardware. Augur monitors reality. The Keeper acts on calibrated-optimal decisions. The Vigil thinks within typed-safe infrastructure. The Cipher remembers — the compiled kernels, the MAP-Elites archives, the calibration data, the TrainingCheckpoints. When the last Relay dies, the Cipher carries the model and everything needed to re-materialize it on whatever silicon comes next.

---

## The Layers (17 layers, L0–L16)

```
L16  Ecosystem        computation genome, federated learning, hardware co-design
L15  Meridian+Augur   calibration, digital twin, monitoring, recommendations
─────────────────────────────────────────────────────────────────────────────
L14  Lifecycle        Cipher persistence, reincarnation, deterministic replay
L13  Distribution     Canopy, Relays, no master, RAID, DiLoCo, 5D parallelism
L12  Data             pipeline absorption, curriculum, latent augmentation
L11  Training         meta-gradients, Hessian, K-FAC, curriculum, optimizer evolution
L10  Models           growing, pruning, width mutation, composition, live surgery
L9   Layers           attention replacement, local losses, per-layer gradient strategy
L8   Tokens           merging, early exit, adaptive patching, per-token precision
─────────────────────────────────────────────────────────────────────────────
L7   Merkle DAG       specification, branches, guards, LoopNodes, atomic swaps
L6   Graphs           CSR property graph, DFG/alias edges, deterministic order
L5   Tensors          shadow handles, TensorMeta, latent space observation, provenance
L4   Operations       Vessel dispatch interception, recording, event sourcing, divergence
L3   Memory           tested allocators (jemalloc CPU, CUDA pool), static plans, OOM structurally impossible
L2   Kernels          Forge+Mimic MAP-Elites search, calibrated simulators, cross-vendor CI, KernelCache, Philox
L1   Hardware         Relays, hardware profiling, multi-vendor, health → Keeper
─────────────────────────────────────────────────────────────────────────────
L0   Safety           8 axioms + contracts + CSL permissions + session types + safety/*.h wrappers
```

Safety disciplines the code. Meridian maps. Augur sees. Vessel intercepts. Keeper serves. Vigil thinks. Cipher remembers. Canopy protects. Relay executes.



# Crucible Code Guide

*The canonical reference for writing Crucible code. Every rule has a cost-of-violation, a compiler-enforcement mechanism, and a discipline fallback. Nothing is style; everything is measured.*

Design intent: **the lowest foreground recording and shadow-dispatch latency the hardware allows, zero UB, bit-identical across hardware under BITEXACT recipes**. We do not promise specific nanosecond numbers — those depend on workload, system load, cache state, NUMA topology, and contention; they are reported by the bench suite, not guaranteed by the docs. Every rule below serves those structural intents.

---

## I. Toolchain

| Preset    | Compiler              | Role                                          |
|-----------|-----------------------|-----------------------------------------------|
| `default` | GCC 16.0.1 (rawhide)  | Primary dev. Debug. Contracts + reflection.   |
| `release` | GCC 16.0.1            | Production. `-O3 -march=native -flto=auto -DNDEBUG` |
| `bench`   | GCC 16.0.1            | Release + `CRUCIBLE_BENCH=ON`                 |
| `tsan`    | GCC 16.0.1            | ThreadSanitizer (mutually exclusive with ASan)|
| `verify`  | GCC 16.0.1            | + internal small-SMT verification suite (deferred — interim: contracts-only, no external solver) |

**GCC 16 is the only supported compiler.** Crucible's safety axioms structurally depend on features that exist only there:

- **Contracts (P2900R14)** — the InitSafe / NullSafe / TypeSafe / MemSafe enforcement mechanism. GCC 16 exclusive.
- **Erroneous behavior for uninit reads (P2795R5)** — InitSafe ceiling. GCC 16 exclusive.
- **Reflection (P2996R13)** — auto-generated hashing and serialization. GCC 16 exclusive.
- **Expansion statements `template for` (P1306R5)** — reflection iteration. GCC 16 exclusive.
- **`constexpr` exceptions (P3068R5)** — compile-time IR verifier. GCC 16 exclusive.
- **Partial program correctness (P1494R5)** — contract violation without UB. GCC 16 exclusive.

GCC 15 cannot compile the codebase. Clang 22 cannot compile the codebase. No fallback exists, and none is pursued — the axioms are load-bearing and non-negotiable.

```bash
cmake --preset default && cmake --build --preset default && ctest --preset default
# For release perf:
cmake --preset release && cmake --build --preset release
# For race detection:
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

When GCC 17 ships, re-evaluate. When Clang eventually ships contracts + reflection + P2795R5, it joins as a parity compiler. Until then: GCC 16 only.

---

## II. The Eight Safety Axioms

Every edit checks all eight. No exceptions. No "fix it later." The axiom violated most recently is the one most likely to be violated next.

### 1. InitSafe — `read(v) ⇒ initialized(v)`

**Cost of violation:** undefined behavior, heisenbug, info leak.

**Compiler enforcement:** P2795R5 erroneous behavior + `-ftrivial-auto-var-init=zero` + `-Werror=uninitialized` + `-Wanalyzer-use-of-uninitialized-value`.

**Discipline:**
- Every struct field has NSDMI (non-static data-member initializer): `T field = sentinel_value;`.
- Padding is explicit: `uint8_t pad[N]{};` never bare arrays.
- Stack aggregates: `RegionNode r{};` always, never `RegionNode r;`.
- C arrays of strong IDs: default ctor of the strong ID initializes (e.g. `MetaIndex::none()` → `UINT32_MAX`).
- `memset` only as fast-path zeroing AFTER NSDMI already documents zero semantics.

```cpp
// ✓ CORRECT
struct TensorSlot {
  uint64_t    offset_bytes = 0;
  SlotId      slot_id;                          // default ctor = UINT32_MAX
  ScalarType  dtype        = ScalarType::Undefined;
  uint8_t     pad[3]{};                         // zero-init padding
};

// ✗ WRONG — reading `offset_bytes` before assignment reads zero by accident,
//           not by guarantee. Replace with NSDMI.
struct TensorSlot {
  uint64_t    offset_bytes;
  SlotId      slot_id;
  ScalarType  dtype;
  uint8_t     pad[3];
};
```

### 2. TypeSafe — `(⊢ e : T) ⇒ eval(e) : T`

**Cost of violation:** silent parameter swap, implicit conversion bug, type confusion.

**Compiler enforcement:** `-Werror=conversion -Werror=sign-conversion -Werror=arith-conversion -Werror=enum-conversion -Werror=old-style-cast -fno-rtti`.

**Discipline:**
- Every semantic value is a strong type. No raw `uint32_t` for anything with meaning.
- IDs: `OpIndex`, `SlotId`, `NodeId`, `SymbolId`, `MetaIndex`, `KernelId` (all `CRUCIBLE_STRONG_ID(Name)` → `explicit(uint32_t)`, `.raw()`, `.none()`, `<=>`, no arithmetic).
- Hashes: `SchemaHash`, `ShapeHash`, `ContentHash` (all `CRUCIBLE_STRONG_HASH(Name)`).
- Enums: `enum class` with explicit underlying type. Convert via `std::to_underlying()` only.
- Bit reinterpretation: `std::bit_cast<T>()` only. `reinterpret_cast` is BANNED.
- Arithmetic: `std::add_sat` / `std::sub_sat` / `std::mul_sat` for all size/offset math.
- Casts: C-style cast is a compile error. `const_cast` is BANNED.

```cpp
// ✓ CORRECT — silent parameter swap impossible
void connect(OpIndex src, OpIndex dst, SlotId slot);

// ✗ WRONG — caller can swap src/dst/slot silently
void connect(uint32_t src, uint32_t dst, uint32_t slot);
```

### 3. NullSafe — `deref(v) ⇒ v ≠ null`

**Cost of violation:** crash (best case) or silent wrong answer.

**Compiler enforcement:** `-Werror=null-dereference -Werror=nonnull -Werror=nonnull-compare -Wanalyzer-null-dereference -Wanalyzer-possible-null-dereference`, contracts `pre(p != nullptr)` on all pointer params.

**Discipline:**
- `[[nodiscard]]` on every query returning bool or pointer.
- `(ptr, count)` pairs → `std::span` accessor. `span(nullptr, 0)` is a valid empty span.
- `alloc_array<T>(0)` returns `nullptr` AND sets count to 0 — both must agree.
- Iterate via span, never raw `(ptr, count)` loop.
- OOM → `std::abort()`. Crucible never runs on systems where OOM is recoverable.

```cpp
// ✓ CORRECT
[[nodiscard]] std::span<const TensorMeta> input_span() const {
    return {input_metas, num_inputs};
}

// Boundary function: contract-enforce non-null
void process(const TraceEntry* entry)
    pre (entry != nullptr)
{
    // body can assume entry is non-null
}
```

### 4. MemSafe — `free(v) ⇒ ¬live(v)`

**Cost of violation:** use-after-free, double-free, memory corruption, RCE.

**Compiler enforcement:** `-fsanitize=address` in debug, `-Werror=use-after-free=3 -Werror=free-nonheap-object -Werror=dangling-pointer=2 -Werror=mismatched-new-delete -Wanalyzer-use-after-free -Wanalyzer-double-free`. `-fno-exceptions` eliminates the whole class of "destructor during stack unwind" bugs.

**Discipline:**
- All graph/DAG memory lives in an Arena (bump pointer, ~2 ns alloc, no fragmentation, no UAF). Arena bulk-frees at epoch boundary.
- No `new` / `delete`. No `malloc` / `free` on hot path.
- No `std::shared_ptr`. No `std::unique_ptr` on hot path (only for top-level ring/MetaLog buffers).
- `= delete("reason")` on copy and move of non-value types. WITH a reason string.
- `static_assert(sizeof(T) == N)` on layout-critical structs.
- Arena type punning uses `std::start_lifetime_as<T>()` (C++23) — NOT `reinterpret_cast`.

```cpp
// ✓ CORRECT
class Arena {
    Arena(const Arena&) = delete("interior pointers would dangle");
    Arena(Arena&&)      = delete("interior pointers would dangle");

    template<typename T>
    T* alloc_obj() {
        void* raw = bump(sizeof(T), alignof(T));
        return std::start_lifetime_as<T>(raw);  // C++23, not reinterpret_cast
    }
};
```

### 5. BorrowSafe — no aliased mutation

**Cost of violation:** data race, torn read, spooky action at a distance.

**Compiler enforcement:** limited in C++ (no borrow checker). Discipline-enforced with review + `-fsanitize=thread` in CI.

**Discipline:**
- SPSC = one writer, one reader, per ring buffer. Never multi-producer without explicit atomic sync.
- Document ownership in a comment at the struct level: "owned by fg thread", "owned by bg thread", "SPSC via acquire/release".
- No shared mutable state except through atomics.
- Arena gives out raw pointers — once given, the arena's bump cursor is opaque to the holder; no one else can reference that region.

### 6. ThreadSafe — acquire/release only

**Cost of violation:** reordered writes, lost wakeups, ARM-specific heisenbugs that don't repro on x86.

**Compiler enforcement:** `-fsanitize=thread` catches races. Discipline for ordering.

**Discipline:**
- Foreground owns TraceRing head + MetaLog head.
- Background owns TraceRing tail + MetaLog tail.
- Cross-thread signals: atomic acquire/release ONLY. Never `memory_order_relaxed`.
- `compare_exchange_strong` with `acq_rel` on RMW.
- Spin on atomic load with `CRUCIBLE_SPIN_PAUSE` (→ `_mm_pause` on x86, `yield` on ARM).
- BANNED on hot path: `sleep_for`, `yield`, `futex`, `eventfd`, `condition_variable`, `atomic::wait/notify`, any timeout.

```cpp
// ✓ CORRECT
producer.store(new_head, std::memory_order_release);

while (consumer.load(std::memory_order_acquire) != expected) {
    CRUCIBLE_SPIN_PAUSE;  // 10-40 ns via MESI cache-line invalidation
}

head.compare_exchange_strong(exp, des, std::memory_order_acq_rel);
```

Relaxed = ARM reordering = race. On x86 it's the same MOV as acquire/release — the cost of always using acquire/release is zero on our target platforms, and the safety is real.

### 7. LeakSafe — bounded resource lifetime

**Cost of violation:** DoS over time, process growth, gradual degradation.

**Compiler enforcement:** `-fsanitize=leak` (integrated into ASan on Linux), `-Wanalyzer-malloc-leak -Wanalyzer-fd-leak`.

**Discipline:**
- Arena bulk-frees graph memory (no per-object free).
- `std::unique_ptr` for long-lived owned buffers (TraceRing, MetaLog).
- `bg_` thread member declared LAST in containing struct — destroyed first, joins before other members die.
- No Rc cycles (we don't use Rc anyway).
- Cipher tiers have explicit eviction: hot (LRU), warm (age), cold (S3 lifecycle policy).

### 8. DetSafe — `same(inputs) ⇒ same(outputs)`

**Cost of violation:** replay breaks, bit-exactness CI reddens, cross-vendor equivalence lost.

**Compiler enforcement:** no `-ffast-math` family. `-ffp-contract=on` (safe: FMA within a statement only). Pinned FTZ via recipe.

**Discipline:**
- DAG fixes execution order (topological sort with hash-based tiebreak).
- Memory plan fixes addresses (pool_base + offset, content-addressed).
- Philox4x32 RNG: counter-based, platform-independent. Zero RNG state anywhere.
- KernelCache keyed on `(content_hash, device_capability)`.
- Reduction topology: pinned binary tree sorted by UUID for BITEXACT recipes.
- No hash-table iteration order dependencies. Sort keys before iterating.
- No pointer-based ordering. Events have `(cycle, kind, sequence_number)`.

---

## III. C++26 Language Features — Opt Matrix

### Opt IN

| Feature | Paper | Usage |
|---|---|---|
| Contracts (`pre`/`post`/`contract_assert`) | P2900R14 | Every boundary function. Hot-path TUs use `contract_evaluation_semantic=ignore` |
| Erroneous behavior for uninit reads | P2795R5 | Foundation of InitSafe axiom |
| Partial program correctness | P1494R5 | Contract violation = `std::terminate`, not UB |
| Trivial infinite loops not UB | P2809R3 | Closes LLVM `while(1){}` → unreachable optimization |
| Remove UB from lexing | P2621R2 (DR) | Lexer no longer has UB corners; applied retroactively by GCC |
| Preprocessing never undefined | P2843R3 | Preprocessor UB corners removed |
| On the ignorability of standard attributes | P2552R3 (DR) | Clarified attribute ignorability — predictable behavior |
| Disallow returning ref to temporary | P2748R5 | Compile error for dangling ref |
| Deleting ptr-to-incomplete ill-formed | P3144R2 | Compile error for silent UB |
| Reflection | P2996R13 | `reflect_hash<T>`, auto-serializers (`-freflection`) |
| Annotations for reflection | P3394R4 | Tag fields for custom codegen |
| Splicing a base class subobject | P3293R3 | Reflect across hierarchy |
| Function parameter reflection | P3096R12 | Auto-generate dispatch from schema |
| `define_static_{string,object,array}` | P3491R3 | Compile-time constexpr static arrays (CKernelId tables) |
| Error handling in reflection | P3560R2 | Structured compile-time errors |
| Expansion statements (`template for`) | P1306R5 | Iterate reflected members without macros |
| `constexpr` exceptions | P3068R5 | IR verifier throws at compile time, zero runtime cost |
| `constexpr` structured bindings | P2686R4 | constexpr ergonomics |
| `constexpr` placement new | P2747R2 | Constexpr arena patterns |
| `constexpr` cast from void* | P2738R1 | Constexpr helpers |
| Pack indexing `Ts...[N]` | P2662R3 | Variadic template access |
| Structured bindings introduce pack | P1061R10 | Clean `auto [first, ...rest] = tup;` |
| Attributes for structured bindings | P0609R3 | `[[maybe_unused]]` per binding |
| Placeholder variable `_` | P2169R4 | Explicit discard |
| User-generated static_assert messages | P2741R3 | Better compile errors |
| `= delete("reason")` | P2573R2 | Every banned copy/move |
| Structured binding decl as condition | P0963R3 | `if (auto [iter, ok] = map.insert(x))` — skip trailing `;ok` |
| Variadic friends | P2893R3 | Tagged-newtype friend families across template packs |

### Opt IN — from C++23

| Feature | Paper | Usage |
|---|---|---|
| Explicit lifetime management | P2590R2 | `std::start_lifetime_as<T>` fixes arena type-punning UB |
| Deducing `this` | P0847R7 | CRTP without CRTP boilerplate |
| Portable assumptions (`[[assume]]`) | P1774R8 | Compile-time invariants to optimizer |
| `static operator()` | P1169R4 | Stateless callable with no `this` |
| Relaxed `constexpr` restrictions | P2448R2 | More compile-time opportunities |
| `if consteval` | P1938R3 | Switch behavior when compile-time vs runtime |
| `consteval` propagates up | P2564R3 | Cleaner consteval chains |
| `#embed` | P1967R14 | Embed binary blobs at compile time |

### Opt OUT

| Feature | Ban via | Reason |
|---|---|---|
| Exceptions | `-fno-exceptions` | Setup cost even unthrown; unwind tables in icache; use `std::expected` or `abort()` |
| RTTI | `-fno-rtti` | Vtable bloat; use `kind` enum + `static_cast` |
| Coroutines on hot path | discipline | Heap allocation, unpredictable latency |
| `volatile` for concurrency | P1152R4 deprecated | `volatile` does not order; use `std::atomic` |
| `[=]` capturing `this` | P0806R2 deprecated; `-Werror=deprecated-this-capture` | Lifetime footgun |
| `memory_order::consume` | P3475R2 deprecated; `-Werror=deprecated-declarations` | Compilers promote to acquire anyway |
| VLAs (`int arr[n]`) | `-Werror=vla` | Stack UB |
| C-style casts | `-Werror=old-style-cast` | Silent UB conversions |
| `reinterpret_cast` | code review grep-ban | Use `std::bit_cast<T>` |
| `const_cast` | `-Werror=cast-qual` | Casting away const is almost always wrong |
| Static downcast (`static_cast<Derived*>`) | N/A — no inheritance in data types | We have no `virtual` |
| Implicit narrowing | `-Werror=conversion -Werror=sign-conversion` | Silent truncation |
| Float `==` | `-Werror=float-equal` | Use `std::abs(a-b) < eps` or exact bit compare |
| Uniform init with `initializer_list` surprises | discipline | `std::vector<int>{10,20}` is 2 elements, not 30; we ban vector anyway |
| Most vexing parse (`Widget w();`) | discipline | Always `Widget w{};` |
| Trigraphs, `register`, `auto_ptr` | already removed from standard | — |

---

## IV. Library Types — Opt Matrix

### Opt IN

| Type | Purpose |
|---|---|
| `std::inplace_vector` (C++26) | Bounded fixed-capacity container, no heap |
| `std::function_ref` (C++26) | Non-owning callable for Mimic callbacks |
| `std::mdspan` (C++23) | Multi-dim span for tensor metadata views |
| `std::expected<T,E>` (C++23) | Error paths without exceptions |
| `std::flat_map` / `std::flat_set` (C++23) | Sorted vector container, cache-friendly |
| `std::move_only_function` (C++23) | Move-only callable |
| `std::start_lifetime_as` (C++23) | Arena type punning correctness |
| `std::bit_cast` (C++20) | The ONLY type-pun primitive allowed |
| `std::add_sat` / `mul_sat` / `sub_sat` (C++26) | Saturation arithmetic at size-math sites |
| `std::breakpoint()` (C++26) | Hardware breakpoint for debug asserts. **libstdc++ 16.0.1 status:** `<debugging>` header declares but does not yet ship symbol definitions — use the `crucible::detail::breakpoint*` shim in Platform.h until libstdc++ ships |
| `std::unreachable()` (C++23) | After exhaustive switch to eliminate default branch |
| `std::countr_zero` / `popcount` (C++20) | Bit manipulation primitives |
| `std::span` (C++20) | Pointer+count replacement |
| `std::jthread` (C++20) | Auto-joining thread, no destructor-terminate |
| `<simd>` (C++26 P1928R15) | Default for new SIMD on x86; library does per-ISA dispatch internally. **CRITICAL spelling note:** libstdc++ 16 ships under `namespace std::simd` (NOT `std::datapar` from ISO; no bridging aliases). **Vec is `std::simd::vec<T,N>`** (alias for `basic_vec<T,Abi>`); ISO `basic_simd`/`simd` names DO NOT exist. **Mask is `std::simd::mask<T,N>`** (alias for `basic_mask<Bytes,Abi>` — first param is byte size, NOT element type); ISO `basic_simd_mask`/`simd_mask` DO NOT exist. **Vec subscript is value-returning const** — use the generator constructor `V([](auto lane){ return ...; })` for per-lane construction, NOT `result[i] = x`. **Compare returns mask, NOT bool:** `v == w` yields `mask_type`; `if (v == w)` fails. Wrap with `all_of(v == w)`, `none_of(v != w)`, etc. Mask also has NO `operator bool` — `if (mask)` fails; use `if (any_of(mask))`. **`select` is NOT re-exported into plain `std::`** (only `min`, `max`, `minmax`, `clamp` are). **x86-only gate:** `bits/version.h` requires `__SSE2__` — on AArch64/Graviton, `<simd>` is empty. Use `<experimental/simd>` (`std::experimental::parallelism_v2`) for ARM + math; see separate row. **FTM:** `__cpp_lib_simd` is NEVER defined; internal gate is `__glibcxx_simd 202506L` requiring `__SSE2__` + structured bindings ≥202411 + expansion statements ≥202411. We don't gate per the no-feature-guards rule. **Shipped:** all loads/stores (6 overloads each: range / iter+n / iter+sentinel × masked/unmasked), all reductions (`reduce(v[, mask][, op[, identity]])`, `reduce_min/max(v[, mask])` — noexcept; `reduce(v, op)`/`reduce_{min,max}_index` NOT noexcept), all mask reductions (`all_of`, `any_of`, `none_of`, `reduce_count`, `reduce_min_index`, `reduce_max_index`), all element-wise algorithms (`min`, `max`, `minmax`, `clamp`, `select`), `chunk`, `cat` (signature under open LWG review), `permute`, public struct templates `rebind<T,V>` / `resize<N,V>` / `alignment<V,T>` plus `_t`/`_v` aliases, `zero_element`, `uninit_element`, all flag constants (`flag_default`, `flag_aligned`, `flag_overaligned<N>`, `flag_convert`). **First-class intrinsic interop:** `basic_vec` has ctors AND `operator _NativeVecType()` conversions for raw `[[gnu::vector_size]]` builtins AND x86 `__m128`/`__m256`/`__m512` — drop in/out of `<immintrin.h>` at zero cost without `bit_cast`. **Missing entirely:** all math (`sin`/`cos`/`sqrt`/`fma`/`abs`(fp)/...), all bit-manipulation (`popcount`/`rotl`/`byteswap`/`bit_ceil`/...), all complex math (`real`/`conj`/`polar`/...), all Bessel/special functions, public `iota` (libstdc++ has `__iota` as private); `crucible::simd::iota_v<V>()` provides this. **DetSafe rule:** integer reductions only — FP reductions are forbidden because std::simd's chunked-fold reorders operations and IEEE rounding diverges across AVX-512 / AVX2 / NEON. The `crucible::simd::DetSafeSimd<V>` concept enforces this at facade boundaries |
| `<experimental/simd>` (parallelism v2, TS) | Fallback for (a) ARM/Power/SVE targets where `<simd>` is gated out by `__SSE2__` and (b) math functions (`sin`/`cos`/`sqrt`/`exp`/`log` on vec) that `<simd>` does not provide. Namespace `std::experimental::parallelism_v2` (inline under `std::experimental`). FTM `__cpp_lib_experimental_parallel_simd = 201803`. Not deprecated — still maintained, has dedicated `simd_neon.h`, `simd_sve.h`, `simd_ppc.h`, `simd_math.h` (1501 lines). Use only where `<simd>` is unavailable; DetSafe rule still applies (no FP reductions) |
| `std::atomic<T>::fetch_max` / `fetch_min` (C++26 P0493R5) | Monotonic update without CAS retry loop; replaces the `Monotonic<T>::bump` CAS pattern. **libstdc++ 16.0.1 status:** shipped (`__cpp_lib_atomic_min_max = 202403L`) — for `atomic<integral>`, `atomic<T*>`, `atomic<floating>` (including `_Float16/32/64/128`, `__bf16`), AND all three `atomic_ref` specializations. Uses `__atomic_fetch_min/max` builtins when available; else CAS-loop fallback. Free-fn `atomic_fetch_{min,max}[_explicit]` exists for `atomic<T>*` (NOT for `atomic_ref`, per spec) |
| `std::atomic_ref<T>` (C++20 P0019R8, C++26 bump) | Atomic operations on externally-owned storage. **libstdc++ 16.0.1 status:** shipped. FTM `__cpp_lib_atomic_ref = 201806L` in C++20 mode, bumps to `202603L` in C++26 mode. Full API: `load`/`store`/`exchange`/`compare_exchange_*`/`fetch_*`/`wait`/`notify_one`/`notify_all`. `atomic_ref<const T>` exposes read-only subset. Padding-bits handling (P3475R2) is behaviorally present via `__compare_exchange<_AtomicRef=true>` CAS-retry using `__builtin_clear_padding`, though `__cpp_lib_atomic_ref_padding_bits` FTM is NOT advertised |
| `std::atomic_ref<T>::address()` (C++26 P2929R1) | Bench/diagnostic — verify the atomic points where the planner said it does. **libstdc++ 16.0.1 status:** shipped in C++26 mode. Rides on `__cpp_lib_atomic_ref >= 202603L` bump; no dedicated FTM. `constexpr noexcept`; returns `const void*`/`void*` (volatile-qualified as applicable) |
| `std::atomic<T>::wait` / `notify_one` / `notify_all` (C++20 P1135R6) | Efficient wait-for-change on atomic. **libstdc++ 16.0.1 status:** shipped. FTM `__cpp_lib_atomic_wait = 201907L`. On Linux uses raw `futex` syscall (4-byte aligned `__platform_wait_t`); on FreeBSD 64-bit uses its native 64-bit futex; elsewhere falls back to mutex+condvar proxy wait (so latency is higher on non-Linux). `atomic_flag` + free-fn `atomic_wait[_explicit]`/`atomic_notify_*` also shipped. **Hot-path rule still stands:** this is futex-backed, latency is 1-5 µs — keep spinning with `_mm_pause` for intra-core waits ≤40 ns |
| `std::atomic<shared_ptr<T>>` / `std::atomic<weak_ptr<T>>` (C++20 P0718R2) | Atomic reference-counted pointer — one-stop publication when a DAG branch replaces a shared object. **libstdc++ 16.0.1 status:** shipped. FTM `__cpp_lib_atomic_shared_ptr = 201711L`. `is_always_lock_free = false` — uses internal `_Sp_atomic` with packed refcount+pointer. Full API incl. `wait`/`notify_*`. Not appropriate for hot-path atomic — the refcount CAS serializes readers. Use for Keeper/Cipher warm-tier shared state updates |
| `std::atomic_signed_lock_free` / `std::atomic_unsigned_lock_free` (C++20 P1135R6) | Type aliases to the widest integer atomic that is always-lock-free on the target. **libstdc++ 16.0.1 status:** shipped. FTM `__cpp_lib_atomic_lock_free_type_aliases = 201907L`. Use for counters where portability of the lock-free guarantee matters more than exact width |
| `<debugging>` — `breakpoint_if_debugging`, `is_debugger_present` (C++26) | Pause when debugger attached, continue otherwise; tighten `CRUCIBLE_INVARIANT`. **libstdc++ 16.0.1 status:** header declares but symbols absent from libstdc++.so — use `crucible::detail::*` shim in Platform.h |
| `std::latch` / `std::barrier` / `std::counting_semaphore` (C++20) | Pool throttling, one-shot init, fan-in waits — replace bespoke atomic+spin where ≥100 ns latency is acceptable |
| `std::source_location` (C++20) | Replace `__FILE__`/`__LINE__` in trace/assert/contract-violation paths |
| `std::is_sufficiently_aligned`, `std::aligned_accessor` (C++26) | Typed alternatives to `__builtin_assume_aligned`. **libstdc++ 16.0.1 status:** shipped (`__cpp_lib_is_sufficiently_aligned`/`aligned_accessor = 202411`) |
| `std::philox_engine` (C++26) | Standard counter-based RNG; cross-reference Crucible's `Philox.h` for bit-equivalence. **libstdc++ 16.0.1 status:** shipped (`__cpp_lib_philox_engine = 202406`) |
| `std::is_layout_compatible_with`, `std::is_pointer_interconvertible_with_class` (C++20) | Semantic companion to `static_assert(sizeof(T) == N)` for layout-strict structs |

#### Blocked on libstdc++ 16.0.1 (revisit when shipped)

These C++26 library features are spec'd and the project will adopt them, but libstdc++ 16.0.1 rawhide does not ship the implementation yet. Do not write code that depends on them today.

| Type | Intended use | Blocking FTM |
|---|---|---|
| `std::is_within_lifetime` (P2641R4, C++26) | Debug-time UAF detection in `Linear<T>` / `ScopedView<>` (consteval-only — needs compiler lifetime tracking, not shimmable) | `__cpp_lib_is_within_lifetime` undefined |
| `std::atomic<T>::wait_for` / `wait_until` (P2643R2, C++26) | Timed atomic wait — bounded fallback for cross-thread wait on a counter with timeout. The backing machinery (`__atomic_wait_address_until[_v]`, `__atomic_wait_address_for[_v]`) already exists in `bits/atomic_timed_wait.h` (used by `<semaphore>::try_acquire_for/until`) but public `atomic::wait_for`/`wait_until` member functions are NOT wired | `__cpp_lib_atomic_timed_wait` undefined |
| `__cpp_lib_atomic_ref_padding_bits` FTM (P3475R2, C++26) | Feature-detection for padding-bit-aware `compare_exchange` on `atomic_ref`. **Behavior already present** — `__compare_exchange<_AtomicRef=true>` in `bits/atomic_base.h` does up-to-3 CAS-retry with `__builtin_clear_padding` — but the FTM is undefined, so feature detection via the paper-prescribed macro fails. Crucible doesn't depend on this FTM, just flagged for awareness | `__cpp_lib_atomic_ref_padding_bits` undefined |

### Opt OUT

| Type | Why |
|---|---|
| `std::function` | Type-erased, heap-allocated, indirect call; use `function_ref` or templated `auto&&` |
| `std::any` | Heap + type erasure |
| `std::regex` | 10-100× slower than alternatives, throws, huge code size |
| `std::async` | Launches threads, heap, ambiguous semantics |
| `std::promise`/`std::future` | Heap + mutex; use atomic flags + SPSC signal |
| `std::shared_mutex` | Often slower than plain mutex; we use neither on hot path |
| `std::thread` raw | Use `std::jthread` (C++20) |
| `std::endl` | Flushes; use `'\n'` |
| `std::vector<bool>` | Proxy iterators, not really a container; use `std::bitset<N>` |
| `std::vector::reserve` (anywhere) | **Banned** — signals wrong container choice. `vector::push_back` past capacity is **O(n)** (copies every existing element to the new buffer); reserve only delays the first O(n) spike, doesn't eliminate it. The "amortized O(1)" story hides three real costs: (1) **tail-latency**: each individual growth event is O(n), fatal for p99 budgets; (2) **silent perf cliff**: move-during-growth requires `noexcept` move ctors or falls back to **copy** without warning; (3) **heap churn**: every growth allocates new + frees old, fragments the allocator. Replacements: known max → `std::inplace_vector<T, N>` (compile-time bound, zero heap, true O(1) push_back, contract-checked overflow); known exact size at construction → `vector<T> v(N)` and fill by index (one allocation, no growth); truly unbounded → plain `vector<T>` and accept amortization (rare in practice; usually means you should have used arena-backed storage). Hot path: arena, never vector at all (per §X.10) |
| `std::vector::push_back` on growth-uncertain hot paths | Same root cause as the reserve ban — growth event is O(n) with hidden allocator interaction. Use `std::inplace_vector<T, N>` for type-encoded bounds or arena allocation for unbounded cold growth |
| `std::cout` / `std::cerr` on hot path | Synced with stdio; use `fprintf(stderr, ...)` for debug |
| `std::printf` / `std::format` on hot path | Formatting cost; reserve for debug paths |
| `std::rand()` / `std::srand()` | Global state, poor quality; Philox only |
| `std::this_thread::sleep_for` | Already banned per ThreadSafe |
| `std::string` in hot-path structs | 32 B SSO, breaks memcpy; `const char*` + explicit lifetime |
| `std::unordered_map` | Chained buckets, pointer chasing; Swiss table open-addressing |
| `std::map` | Red-black tree, pointer chasing; `std::flat_map` |
| `std::shared_ptr` | Atomic refcount, heap; arena raw pointers |
| `std::ranges` pipelines | Compile-time bloat, debug-build performance cliff |
| `std::optional` on hot path | +1 B tag + branch; `nullptr` or `kind` enum sentinel |
| `std::variant` on hot path | Visitor dispatch; `kind` enum + `static_cast` |
| Bitfields | Often slower than manual shift+mask; manual bits |
| `std::codecvt` | Deprecated, broken API |
| `std::rcu` / `<hazard_pointer>` (C++26) | We publish via `AtomicSnapshot<T>` + `atomic_ref` — finer control, DetSafe-documented; stdlib variants add overhead we don't need. **libstdc++ 16.0.1 status:** not shipped (`__cpp_lib_rcu` / `__cpp_lib_hazard_pointer` undefined) — banning the policy now ensures no one reaches for them later when they do land |
| `std::simd` FP reductions (`reduce_*` on float/double) | ISA-dependent rounding; breaks DetSafe bit-equality across platforms. Integer reductions are safe |
| `std::linalg` (C++26) | HS9 bans vendor BLAS; `<linalg>` dispatches through one anyway. **libstdc++ 16.0.1 status:** not shipped (`__cpp_lib_linalg` undefined) — policy ban applies once it lands |
| `std::copyable_function` (C++26) | Heap allocation risk on capture-heavy lambdas; prefer `function_ref` (borrow) + explicit owned-pointer when ownership is needed |

---

## V. Compiler Flags

### Common (every build)

```
-std=c++26                           strict C++26, no GNU dialect drift
-fcontracts                          P2900 contracts
-freflection                         P2996 reflection
-fno-exceptions                      eliminate unwind tables
-fno-rtti                            eliminate vtables of typeinfo
-fno-strict-overflow                 don't optimize assuming signed overflow impossible
-fno-delete-null-pointer-checks      don't optimize away null checks
-fno-math-errno                      math functions don't set errno (faster, vec-friendly)
-ffp-contract=on                     FMA within a statement (safe for BITEXACT)
-ftrivial-auto-var-init=zero         P2795R5 — zero-init stack, kills InitSafe class
-fstack-protector-strong             stack canaries
-fstack-clash-protection             stack clash mitigation (~0.1% cost)
-fharden-control-flow-redundancy     GCC CFG hardening
-fcf-protection=full                 Intel CET / ARM BTI+PAC
-fno-omit-frame-pointer              readable traces (<1% cost)
-fno-plt                             direct calls
-fno-semantic-interposition          allow cross-TU inlining
-fvisibility=hidden                  hidden symbols = more inlining
-fvisibility-inlines-hidden          same for inline
-ffunction-sections                  one function per section (for --gc-sections)
-fdata-sections                      one var per section
-fno-common                          no tentative definitions
-fstrict-flex-arrays=3               strict flex array rules
-fsized-deallocation                 sized `delete` overloads
-D_FORTIFY_SOURCE=3                  glibc bounds checks
-D_GLIBCXX_ASSERTIONS                libstdc++ cheap asserts (release-safe)
```

### Debug preset

```
Common flags +
-Og -g                                debuggable but fortify/analyzer friendly
-fcontract-evaluation-semantic=enforce   check + terminate
-fsanitize=address                    ASan + LeakSan
-fsanitize=undefined,bounds-strict    UBSan + strict bounds
-fsanitize=shift-exponent,pointer-overflow  extra UBSan
-fno-sanitize-recover=all             abort on first violation
-D_GLIBCXX_DEBUG                      heavy container checks
```

### Release preset

```
Common flags +
-O3
-march=native -mtune=native
-DNDEBUG
-g                                    keep frame info for profiling
-flto=auto                            whole-program LTO, ~10-20% typical win
-fcontract-evaluation-semantic=observe  log but continue (or ignore on hot TUs)
-ftree-vectorize                      on by default at -O3
-fvect-cost-model=unlimited           aggressive auto-vec
-mprefer-vector-width=512             AVX-512 where HW supports
-fipa-pta                             interprocedural pointer analysis
-fgraphite-identity                   polyhedral loop framework
-floop-nest-optimize                  Graphite loop optimizer
-fno-trapping-math                    assume FP doesn't trap (vec-friendly)
+ PGO artifacts from bench workload
```

### Verify preset

Release flags + `-fcontract-evaluation-semantic=enforce` + `-fanalyzer`. The internal small-SMT verification tier is reserved for residual integer-arithmetic obligations (deferred — interim: contracts-only). No external solver dependency: Crucible ships no Z3, no CVC, no proprietary SMT engine, period.

### NEVER (kills determinism or wastes perf)

```
-ffast-math                       breaks IEEE 754 — kills BITEXACT
-funsafe-math-optimizations       same
-fassociative-math                reorders FP
-fno-signed-zeros                 breaks IEEE
-ffinite-math-only                assumes no NaN/Inf
-ffp-contract=fast                cross-statement FMA (bits can differ)
-fno-strict-aliasing              disables TBAA, big perf loss
-fshort-enums                     non-portable ABI
-fpermissive                      accepts non-conforming code
-ftrapv                           traps on signed overflow — expensive
-fwrapv                           ~1% global perf cost; use `std::*_sat` at sites
-funroll-all-loops                bloats icache; per-loop pragma instead
```

---

## VI. Warnings Promoted to Errors

All UB-adjacent and lifetime-adjacent warnings are hard errors.

```
-Werror=return-type
-Werror=uninitialized
-Werror=maybe-uninitialized
-Werror=null-dereference
-Werror=nonnull
-Werror=nonnull-compare
-Werror=shift-count-overflow
-Werror=shift-count-negative
-Werror=shift-negative-value
-Werror=stringop-overflow
-Werror=stringop-truncation
-Werror=array-bounds
-Werror=restrict
-Werror=dangling-pointer=2
-Werror=use-after-free=3
-Werror=free-nonheap-object
-Werror=alloc-size-larger-than
-Werror=mismatched-new-delete
-Werror=mismatched-dealloc
-Werror=implicit-fallthrough
-Werror=format-security
-Werror=format-nonliteral
-Werror=aggressive-loop-optimizations
-Werror=aliasing
-Werror=cast-qual
-Werror=conversion
-Werror=sign-conversion
-Werror=arith-conversion
-Werror=enum-conversion
-Werror=old-style-cast
-Werror=float-equal
-Werror=vla
-Werror=type-limits
-Werror=switch
-Werror=switch-default
-Werror=pessimizing-move
-Werror=redundant-move
-Werror=self-move
-Werror=deprecated-copy
-Werror=deprecated-copy-dtor
-Werror=return-local-addr
```

Non-error warnings (informational, not yet hard):
- `-Wpadded` (off — we accept padding where it occurs)
- `-Wsuggest-*` (off — suggestions, not errors)

---

## VII. Attributes and Macros

`Platform.h` provides these. Use them deliberately.

```cpp
// ── Inlining control ──────────────────────────────
#define CRUCIBLE_INLINE       [[gnu::always_inline]] inline
#define CRUCIBLE_HOT          [[gnu::hot, gnu::always_inline]] inline
#define CRUCIBLE_COLD         [[gnu::cold, gnu::noinline]]
#define CRUCIBLE_FLATTEN      [[gnu::flatten]]      // inline all calls inside
#define CRUCIBLE_NOINLINE     [[gnu::noinline]]

// ── Purity (optimizer can CSE / move) ─────────────
#define CRUCIBLE_PURE         [[gnu::pure, nodiscard]]    // depends on args + memory
#define CRUCIBLE_CONST        [[gnu::const, nodiscard]]   // depends on args only

// ── Pointer contracts ─────────────────────────────
#define CRUCIBLE_NONNULL              [[gnu::nonnull]]
#define CRUCIBLE_RETURNS_NONNULL      [[gnu::returns_nonnull]]
#define CRUCIBLE_MALLOC               [[gnu::malloc]]      // returned ptr doesn't alias
#define CRUCIBLE_ALLOC_SIZE(n)        [[gnu::alloc_size(n)]]
#define CRUCIBLE_ASSUME_ALIGNED(n)    [[gnu::assume_aligned(n)]]

// ── Tail call (state machines) ────────────────────
#define CRUCIBLE_MUSTTAIL     [[gnu::musttail]]

// ── Spin pause (hot wait) ─────────────────────────
// x86: _mm_pause() — 10-40ns via MESI invalidation
// ARM: yield instruction
CRUCIBLE_SPIN_PAUSE
```

Use in function bodies:

```cpp
// Runtime alignment hint → optimizer assumes alignment from here on
p = static_cast<decltype(p)>(__builtin_assume_aligned(p, 64));

// Compile-time invariant
[[assume(n > 0 && n % 8 == 0)]];

// Likely/unlikely
if (cache_hit) [[likely]] { ... } else [[unlikely]] { ... }

// After exhaustive switch
switch (kind) {
    case Kind::A: ... return x;
    case Kind::B: ... return y;
    case Kind::C: ... return z;
}
std::unreachable();  // removes default branch, optimizer assumes switch is exhaustive
```

Use `__restrict__` on non-aliasing pointer params in inner loops — unlocks auto-vectorization:

```cpp
void scan(const T* __restrict__ in, U* __restrict__ out, size_t n);
```

---

## VIII. Performance Discipline

### Data layout

```
L1d:  48 KB   (~750 × 64B lines)    ~4-5 cycles
L2:   2 MB    (~30K lines)           ~12-20 cycles
L3:   ~30 MB (shared)                ~35-50 cycles
DRAM:                                 ~200-300 cycles
```

Rules:

1. **Hot working set fits in L1.** Budget 48 KB per core. Measure with `perf stat -e L1-dcache-loads,L1-dcache-load-misses`.
2. **SoA over AoS.** Iteration reads one field across N rows → one cache line per field vs N lines per row.
3. **`alignas(64)`** every atomic that crosses threads. False sharing = 40× slowdown.
4. **Pack cold fields into separate structs** or explicit trailing padding.
5. **Struct member ordering**: largest to smallest to minimize implicit padding, OR explicit `uint8_t pad[N]{}` for deterministic layout.
6. **No pointer chasing on hot path.** Contiguous arrays + indices, not linked lists.
7. **`static_assert(sizeof(T) == N)`** on layout-critical structs.

Example — hot ring entry is exactly one cache line:

```cpp
struct alignas(64) TraceRingEntry {
    OpIndex       op_idx;        //  4 B
    SchemaHash    schema_hash;   //  8 B
    ShapeHash     shape_hash;    //  8 B
    MetaIndex     meta_head;     //  4 B
    uint8_t       op_flags;      //  1 B
    uint8_t       pad[39]{};     // 39 B → total 64 B
};
static_assert(sizeof(TraceRingEntry) == 64);
```

### Prefetching

Manual prefetch ahead of loop iterations when stream access pattern is clear:

```cpp
for (size_t i = 0; i < n; ++i) {
    if (i + 16 < n) [[likely]]
        __builtin_prefetch(&data[i + 16], 0, 0);  // read, no temporal locality
    process(data[i]);
}
```

Rule: prefetch 8-16 iterations ahead; `locality = 0` for streaming reads (don't pollute L1 with data you won't revisit).

### Branches

1. `[[likely]]` / `[[unlikely]]` on predictable branches.
2. `__builtin_expect_with_probability(x, v, p)` for explicit probabilities.
3. **Predication**: replace `if (x < 0) x = 0` with `x = std::max(x, 0)` — compiler emits `cmov`.
4. **Switch on small dense enum** — compiler generates jump table; one indirect branch.
5. `std::unreachable()` after exhaustive switch — eliminates default entirely.
6. **Branchless bit tricks** where appropriate: `x & -cond` for conditional zeroing.

### Vectorization

Three tiers:

1. **Auto-vectorization** — clean loops + `__restrict__` + `[[assume]]`. Audit with `-fopt-info-vec -fopt-info-vec-missed`.
2. **`std::simd` (C++26 `<simd>`)** — namespace `std::simd::*`, vec spelling `std::simd::vec<T,N>`. See §IV opt-in for the full surface (`unchecked_load`/`partial_load`, `reduce` with masked overload, `select`, `min/max/clamp`, `chunk`/`cat`). Default for new portable SIMD; the `crucible::simd::*` facade adds only what std lacks (`iota_v`, `prefix_mask`, `DetSafeSimd` concept, microarch detection).
3. **Intrinsics** (`<immintrin.h>`) — only when `std::simd` cannot express the operation (`vpshufb`, `vpternlog`, `vpcompressd`, `vpgatherdd`, `vpopcntq`) or when math/bit-manip on vec is needed (those C++26 sections aren't shipped in libstdc++ 16). The canonical example is `SwissTable.h`'s `vpcmpeqb + vpmovmskb` probe. Compile-time `#ifdef __AVX2__ / __SSE2__` selection — single ISA per build, matches the deployment microarch chosen at `-march=` time.

Crucible's vectorizable hot paths: Philox RNG, hash mixing, TensorMeta extraction, reduction kernels.

### Link-Time Optimization (LTO)

`-flto=auto` — parallel LTO: whole-program inlining, dead code elimination, cross-TU constant propagation. Enabled unconditionally in release.

### OS / kernel tuning

For production Keeper deployments:

1. **Huge pages**: `madvise(arena_base, size, MADV_HUGEPAGE)` — cut TLB misses.
2. **`mlock()`** hot regions — no page faults on critical path.
3. **CPU affinity**: `pthread_setaffinity_np` — pin fg thread to one core, bg to another.
4. **Kernel CPU isolation**: boot with `isolcpus=4,5,6,7 nohz_full=4,5,6,7 rcu_nocbs=4,5,6,7`.
5. **NUMA-local allocation**: `numa_alloc_onnode()` for arena matching thread's NUMA node.
6. **Avoid SCHED_FIFO unless necessary** — real-time priority can lock the machine.

### Measurement discipline

`cmake --preset bench && ctest --preset stress`

1. Report **p50, p99, p99.9, max** — mean is banned. Tail dominates at distributed scale; a 7 ns mean with 500 ns p99.9 destroys throughput when any worker's worst iteration blocks the rest.
2. Run 10+ iterations. Worst observed is the bound, not the median.
3. `taskset -c <N>` to pin to an isolated core; `std::chrono::steady_clock` or `__rdtsc()` for timing (never `system_clock` — wall-clock jumps break experiments).
4. Variance > 5% = throttling = invalid. Check `dmesg | grep thermal`, re-run on quiet machine.
5. Units: ns per op, μs for setup.

```
✗ "try_push: ~8 ns average over 1M calls"
✓ "try_push: p50=7, p99=10, p99.9=18, max=142 ns (10M calls, max from cold-start cache miss)"
```

### Profiling toolkit — concrete commands

**Aggregate counters** (first look at any hot path):

```bash
perf stat -e cycles,instructions,cache-misses,cache-references, \
             L1-dcache-loads,L1-dcache-load-misses, \
             branch-misses,dTLB-load-misses \
    -- taskset -c 4 ./build/bench/bench_dispatch
```

Healthy fg-dispatch numbers on Tiger Lake / Zen 4 / Sapphire Rapids:
- IPC: 2.5–4.0 (instructions per cycle)
- L1d miss rate: <1%
- Branch miss rate: <0.5%

**Flame graph** (per-function cycle attribution):

```bash
perf record -F 997 -g --call-graph=lbr -- ./build/bench/bench_X
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

LBR gives cycle-accurate call stacks with minimal overhead. `-F 997` = ~1 kHz sampling, prime to avoid aliasing with timer tick.

**False-sharing hunt** (cache-line contention):

```bash
perf c2c record -- ./build/bench/bench_X
perf c2c report --stdio
```

Shows per-cache-line HITMs between cores. >1000 HITMs/s on a single line = false sharing. Fix: `alignas(64)` separation.

**Auto-vectorization diagnostics:**

```bash
g++ -O3 -march=native -fopt-info-vec=vec.log \
                      -fopt-info-vec-missed=missed.log \
                      -fopt-info-loop=loop.log ...
```

`missed.log` tells you WHY the vectorizer gave up. Common fixes: `__restrict__`, `[[assume(n%8==0)]]` on trip count, align, remove branch from inner body.

**Inlining diagnostics:**

```bash
g++ -O3 -fopt-info-inline-missed=inline.log ...
```

Catches accidental non-inlining of `CRUCIBLE_INLINE` / `CRUCIBLE_HOT` functions — typically triggered by type-erasure or virtual calls you didn't realize were there.

**Disassembly of a specific function** (audit what the optimizer produced):

```bash
objdump -d --disassembler-options=intel build/bench/bench_X | less
# or
g++ -S -O3 -masm=intel src.cpp -o -
```

Read the hot path's assembly. It's short. If you see a branch you didn't expect, a `lock` prefix you didn't want, or a spill to stack — fix at source.

**Intel PT full trace** (when counters + sampling aren't precise enough):

```bash
perf record -e intel_pt//u -- ./bench_X
perf script -F insn,ip,pid | awk '/hot_function/'
```

Full instruction trace at per-cycle resolution. ~10× overhead but gives exact branch-by-branch history. Reach for when diagnosing rare heisenbugs.

### Operation shape per hot path

Every hot operation has a structural cost shape — what it must do per call. Crucible does not promise specific nanosecond numbers (those vary by workload, system load, cache state, NUMA topology, and contention); the bench suite reports current measurements on the dev hardware.

| Operation | Per-call shape | Notes |
|---|---|---|
| Shadow handle dispatch | metadata write, no function call | `[[gnu::flatten]]` |
| TraceRing push | one acquire/release pair on isolated cache lines | SPSC + `_mm_pause` + `alignas(64)` head/tail |
| Arena bump allocation | bump + mask, no branch, no lock | one cache line touch |
| MetaLog append | one acquire/release pair on isolated cache lines | SPSC, write-combined |
| Cross-core signal wait | bounded by MESI cache-line transfer cost | floor is the interconnect; cross-socket worse than intra-socket |
| Swiss-table lookup (hit) | one open-addressed probe with SIMD compare | Open addressing + SIMD probe |
| Contract check at boundary | one branch under `semantic=observe`; nothing under `ignore` | hot TUs use `ignore` |
| ExecutionPlan submit (warm) | cache lookup + doorbell write | Cache hit + doorbell |
| ExecutionPlan submit (cold, ≤5 patches) | plan lookup + patch writes + SFENCE + doorbell | one plan creation per fresh shape |
| Syscall | kernel-mediated transition | Banned on hot path |
| `malloc` | allocator round-trip | Banned on hot path |

A regression in measured latency on the bench suite is investigated like any other regression — root-cause first, then fix. There is no fixed "budget number" promised in this guide; the bench-suite outputs are the source of truth for the current state of the world on each hardware target.

### Hot functions fit in one cache line

Target: every hot-path function compiles to ≤64 bytes of machine code (one I-cache line fetch). Audit via `objdump -d`. Commit a snapshot of hot-function disassembly under `build/asm-snapshot/` and review its diff on every PR like source code.

If a hot function exceeds 64 bytes of machine code, either (a) the design is wrong (split into hot + cold), (b) a dependency is bloated (eliminate), or (c) the budget was optimistic (document the new budget and notify).

### Cold-path outlining is mandatory

Every `[[unlikely]]` branch body that is more than one return statement or one early-abort is **outlined** to a `[[gnu::cold, gnu::noinline]]` helper function. Rationale: `[[unlikely]]` only steers the branch predictor; it does not prevent the cold body's code from occupying an I-cache line adjacent to the hot path.

Rule: if a `[[unlikely]]` block has more than 8 lines of non-trivial work, outline it. The cold helper sits in `.text.unlikely` (a separate section) and never touches the L1 I-cache during normal execution.

### PGO + AutoFDO for release

Release builds are profile-guided; non-PGO release is a dev build, not production. Typical gain on branchy code: **5-15%**.

```bash
# 1. Instrumented build
cmake --preset release -DCRUCIBLE_PGO=generate && cmake --build --preset release

# 2. Run the bench suite under production workload
./build/bench/bench_dispatch && ./build/bench/bench_trace_ring

# 3. Rebuild with profile
cmake --preset release -DCRUCIBLE_PGO=use && cmake --build --preset release
```

Alternative (continuous profiling from production runs): **AutoFDO** via `-fauto-profile=<profile.afdo>` fed from `perf record`. Same wins, no instrumented build.

### Per-deployment microarchitecture targeting

- **Local development**: `-march=native -mtune=native`. Exact features of the build machine.
- **Distribution / production**: the specific minimum microarch of the deployment fleet, declared in the build manifest. No project-wide default — the choice is deployment-local and documented alongside the fleet's hardware spec.
- **Single-target binaries only.** Crucible does not use `[[gnu::target_clones]]` or any other multi-target / function-multiversioning mechanism. Each binary is compiled for one ISA tier; fleets that span multiple microarchs ship multiple binaries (one per tier), not a single fat binary with runtime dispatch. This keeps icache pressure predictable, eliminates the indirect-jump-on-first-call cost, and means every hot function compiles to a single straight-line code path the optimizer fully sees through.

No single `-march=` default ships with Crucible. The build owner picks it for their fleet.

---

## IX. Concurrency Patterns

### The only two threads

```
Foreground (hot):  records ops at ~5 ns each via TraceRing
Background (warm): drains ring, builds TraceGraph, memory plan, compiles
```

No third thread except OS / OS-adjacent (systemd, signal handlers). Multiple background workers allowed inside Mimic for parallel kernel compilation.

### SPSC ring pattern

```cpp
template<typename T, size_t N>  // N power of 2
class SpscRing {
    alignas(64) std::atomic<uint64_t> head_{0};  // fg writes
    alignas(64) std::atomic<uint64_t> tail_{0};  // bg writes
    std::array<T, N> buffer_;                    // N = capacity
    static constexpr uint64_t MASK = N - 1;

public:
    // Producer side (fg thread)
    CRUCIBLE_HOT bool try_push(const T& item) {
        uint64_t h = head_.load(std::memory_order_relaxed);  // own variable
        uint64_t t = tail_.load(std::memory_order_acquire);   // cross-thread
        if (h - t >= N) [[unlikely]] return false;
        buffer_[h & MASK] = item;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Consumer side (bg thread)
    bool try_pop(T& item) {
        uint64_t t = tail_.load(std::memory_order_relaxed);  // own variable
        uint64_t h = head_.load(std::memory_order_acquire);   // cross-thread
        if (h == t) return false;
        item = buffer_[t & MASK];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
};
```

Note: `relaxed` is OK for a thread reading its OWN atomic. Only cross-thread reads need `acquire`.

### Cross-thread shared state

| Object | Producer | Consumer | Sync |
|---|---|---|---|
| TraceRing entries | fg | bg | SPSC acquire/release |
| MetaLog entries | fg | bg | SPSC acquire/release |
| KernelCache slots | bg | fg | CAS on atomic slot, fg reads acquire |
| RegionNode::compiled | bg | fg | atomic pointer swap (release/acquire) |
| Cipher warm writes | bg | (read by any peer) | Raft-committed |

### Async event waiting — the latency hierarchy

**The floor is ~10-40 ns on intra-socket, set by MESI.** Nothing is faster. Understanding why tells you when to spin and when not to.

#### Mechanism

```
Producer core                           Consumer core
─────────────                           ─────────────
  store(1, release)                       while (load(acquire) == 0)
                                              pause()
  │                                         │
  ▼                                         ▼
  Store buffer → L1 (Modified)            L1 has line in Shared state
  │                                       load returns 0 from L1 (~1 ns)
  ▼
  RFO invalidation via
  ring/mesh interconnect ─────────────→  Line transitions Shared → Invalid
  │                                       │
  │                                       Next load misses L1
  │                                       │
  ▼                                       ▼
  Line state = Modified                   Query L3 / peer L1-L2
                                          ◄───── Line forwarded (Exclusive → Shared)
                                          │
                                          Line now in Shared state, value = 1
                                          Load returns 1

  Total wall-clock: ~10-40 ns intra-socket (L3 ring latency)
                    ~30-100 ns cross-socket (UPI/QPI hop)
```

No kernel. No syscall. No context switch. Just transistors talking to transistors through the cache-coherence fabric.

#### Latency hierarchy

| Technique | Latency | Power | Use case |
|---|---|---|---|
| `load(acquire)` + `_mm_pause` | **10-40 ns** intra-socket, 30-100 ns cross-socket | High (core busy) | **Hot-path signal from imminent event — our default** |
| Same + exponential backoff | 10 ns – 1 μs | Moderate | Unknown-delay signal; rare in Crucible |
| `UMWAIT` (WAITPKG, C0.1/C0.2) | ~100-500 ns + wait time | Low | Power-aware; expected wait 1-100 μs. Not applicable on our hot path |
| `std::atomic::wait/notify` | 1-5 μs (maps to futex on Linux) | Low | BANNED on hot path |
| `futex(FUTEX_WAIT)` | 1-5 μs | Low | BANNED on hot path |
| `pthread_cond_wait` | 3-10 μs | Low | BANNED on hot path |
| `poll` / `epoll_wait` | 5-20 μs | Low | BANNED on hot path |

**Rule:** if the expected wait is under ~1 μs, spinning wins outright. If it's over ~100 μs, UMWAIT wins on power but we don't have waits that long on a well-designed hot path. In between (1-100 μs), the architecture is wrong — pipeline the work, don't wait.

#### Why `_mm_pause()` matters

PAUSE adds no latency. It hints SMT spinning (sibling HT gets more issue bandwidth), reduces memory-order-violation pipeline flush on loop exit, lowers power, and on Skylake+ is ~140 cycles (~40 ns at 3.5 GHz) acting as natural backoff. Pre-Skylake ~10 cycles — Skylake stretched it for fairer SMT. ARM: `yield` (SEV/WFE is for sleep-wait, too slow).

#### Store-side discipline

Fast spin-wait requires equally fast signal delivery.

- x86 (TSO): aligned `store(release)` is one `MOV` — no fence. `compare_exchange` is `LOCK CMPXCHG` — 15-25 ns (drains store buffer + full MESI round-trip).
- ARM (weakly-ordered): `store(release)` emits `STLR`, 1-2 cycles.
- `atomic_thread_fence(release)` is free on x86, emits `DMB ISH` on ARM.
- Never put atomic ops in tight producer loops — batch.

#### Producer-side false sharing — the 40× trap

If the producer's head_ counter and the consumer's tail_ counter share a cache line, every store on one side invalidates the other's read. Ping-pong. 40× latency penalty:

```cpp
// ✗ WRONG — both atomics on one line, cross-thread ping-pong
struct Ring {
    std::atomic<uint64_t> head_;     // 8 B
    std::atomic<uint64_t> tail_;     // 8 B, SAME CACHE LINE
    // ...
};

// ✓ CORRECT — each atomic isolated on its own cache line
struct Ring {
    alignas(64) std::atomic<uint64_t> head_;  // fg writes, bg reads
    alignas(64) std::atomic<uint64_t> tail_;  // bg writes, fg reads
    // ... buffer after
};
```

The `alignas(64)` is not decoration. It turns a 40× slowdown into optimal throughput. Same discipline applies to EVERY cross-thread atomic.

#### `std::atomic_ref` for element-wise atomic access

For atomic CAS on an element of a plain array (e.g. SwissCtrl bytes, KernelCache slots) without paying `std::atomic<T>` per-element overhead:

```cpp
uint64_t slots[N];                             // plain array, one cache line per slot

// Atomic CAS on one slot without making every slot atomic
std::atomic_ref<uint64_t> slot_ref{slots[idx]};
slot_ref.compare_exchange_strong(expected, desired, std::memory_order_acq_rel);
```

Requires alignment: `alignof(T) >= std::atomic_ref<T>::required_alignment` — usually `alignof(T)`, but AVOID spanning cache lines for 16-byte atomics.

#### Compiler barriers — ordering without runtime cost

Sometimes you need ordering without any hardware operation:

```cpp
asm volatile("" ::: "memory");                 // compiler barrier: don't reorder across this
std::atomic_signal_fence(std::memory_order_seq_cst);  // same, portable
```

Use when:
- Preventing the optimizer from hoisting/sinking ops around a measurement point (benchmarks).
- Ensuring a non-atomic write is committed before signaling (together with an atomic release on a separate variable).

Zero machine cost. Just blocks the optimizer.

#### The bottom line

30 ns is the physical floor for cross-core wait; the SPSC ring hits it. "Lower" claims are single-threaded, same-thread store-buffer read-after-write (~1 ns, not cross-thread), or cooked warm-L1 benchmarks. >50 ns per signal on hot path = something wrong: false sharing, a LOCK-prefixed fence, NUMA-remote memory, or a disguised kernel call. Diagnose with `perf stat` on cache events + `perf c2c`.

### Permission discipline — CSL-typed concurrency

The "two threads" rule above is the *floor* of concurrency in Crucible. It's also the easy case: one fg producer, one bg consumer, hand-coded SPSC ring. Beyond that — kernel compile pools, sharded dispatch, multi-reader snapshots, BG pipelines — the discipline must scale, and "scale" means the type system has to do the bookkeeping the human stops doing.

Crucible encodes **Concurrent Separation Logic** (O'Hearn 2007) as a family of zero-cost C++ types. The discipline is mechanical: tokens prove ownership at the type level; the compiler enforces who can call what. The runtime cost is exactly the underlying primitive's cost (SpscRing acquire/release, AtomicSnapshot seqlock, etc.) — no extra mutex, no extra CAS, because the type system already proved the access pattern is sound.

#### The CSL → C++ mapping

| CSL concept | C++ encoding | File | When to use |
|---|---|---|---|
| Separating conjunction `*` | `permission_split<L,R>(Permission<In>&&)` | `safety/Permission.h` | Splitting a region into disjoint subregions |
| Frame rule | Linearity (move-only `Permission<Tag>`) | `safety/Permission.h` | Every exclusive ownership claim |
| Parallel composition rule | `permission_fork<Children...>(parent, callables...)` | `safety/PermissionFork.h` | Spawning N threads with disjoint sub-permissions |
| Fractional permissions `e ↦_p v` | `SharedPermission<Tag>` + `SharedPermissionPool` (atomic refcount) | `safety/Permission.h` | Multi-reader / single-writer with mode upgrade |
| Lifetime-bound borrow | `ReadView<Tag>` (CRUCIBLE_LIFETIMEBOUND) | `safety/Permission.h` | Scoped read borrow (function-call lifetime) |
| Resource invariants (Brookes) | DEFERRED — needs `LockedResource<T, Inv>` | (future) | Mutex-protected shared state |
| Logical atomicity (TaDA) | Implicit — every consume-and-return cycle | (free) | All Permission-typed operations |

Backlog: SEPLOG-A1..E3 (#300–#319). Phase A primitives → Phase B worked examples → Phase C scheduler → Phase D Crucible integration → Phase E validation.

#### "Just right amount of concurrency" — the cache-tier rule

Parallelism only wins when memory bandwidth is the bottleneck. When the working set is L1/L2-resident, adding cores adds nothing but cache-line ping-pong, instruction-cache cold misses, and TLB shootdowns — strictly worse than a single core that already has the data hot. When the working set lives in L3 or DRAM, memory latency is the bottleneck and adding cores adds independent cache hierarchies (each thread's L1/L2 preloads its share) plus parallel memory-controller channels.

The decision rule (encoded in `concurrent/CostModel.h`, SEPLOG-C2):

| Working set | Decision | Reason |
|---|---|---|
| `< L1d_per_core` (~32 KB) | **SEQUENTIAL** | Already hot in one core's L1; threading thrashes |
| `< L2_per_core` (~1 MB) | **SEQUENTIAL** | L2 is private; thread #2 cold-misses everything |
| `< L3_per_socket` (~32 MB) | **PARALLEL ≤ 4** | Within socket, cores share L3 bandwidth |
| `≥ L3` (DRAM-bound) | **PARALLEL = min(cores, ws / L2_per_core)** with NUMA-local affinity | Memory-channel-bound; scale until channels saturate |
| Compute-bound override | Raise floor regardless of footprint | If per-item compute > 100 ns, parallelism wins independent of memory |

The promise: **never regresses**. If the cost model says sequential, parallel must not be measurably faster (within 5%, validated by SEPLOG-E1 bench harness). If it says parallel, the speedup must justify the sync cost.

`AdaptiveScheduler` (SEPLOG-C3) reads `Topology` (SEPLOG-C1, sysfs probe of cache sizes, core counts, NUMA distances), evaluates the rule against a `WorkingSet` declared by the task, and dispatches via `NumaThreadPool` (SEPLOG-C4, per-core jthreads + ChaseLevDeque + `sched_setaffinity`). Sequential decisions invoke the body inline; parallel decisions perform `permission_fork` with NUMA-local placement.

#### Decision matrix — which Permission primitive

```
Need exclusive single-thread ownership?
    → Permission<Tag>                       (linear, move-only, sizeof = 1)

Need shared-read scoped to a function call (lifetime fits inside the caller's stack)?
    → ReadView<Tag>                         (lifetime-bound, copyable, sizeof = sizeof(void*))

Need shared-read across threads (lifetime escapes)?
    → SharedPermission<Tag> via SharedPermissionPool::lend()
                                            (RAII guard, atomic refcount, mode upgrade via try_upgrade)

Need structured fork-join (split parent → N children → rejoin)?
    → permission_fork<Children...>(parent, callables...)
                                            (CSL parallel rule, jthread-based, RAII join)

Need a queue with mode-typed access (read mode vs write mode)?
    → PermissionedRwQueue<T, N, Tag>        (synthesis primitive, SEPLOG-B4)

Need a sharded dispatch grid (M producers × N consumers)?
    → PermissionedShardedGrid<T, M, N, Cap, Tag>  (SEPLOG-B3)
```

#### Anti-patterns (review-rejected)

- **Storing `Permission<Tag>` in a long-lived struct field** that is shared between threads. Defeats linearity — the struct may be aliased and the type system can't see it. Permissions belong in handles (Pinned), thread-local stacks, or function parameters.
- **Passing `SharedPermission` by value across functions** without lifetime context. Lifetime gets confusing fast. Prefer `ReadView<Tag>` for scoped borrows; use `SharedPermissionGuard` (RAII) when crossing thread boundaries.
- **Manually spawning `std::jthread` with a Permission inside** instead of using `permission_fork`. Bypasses the CSL parallel-rule encoding and skips static verification of `splits_into_pack`. Use `permission_fork` and let the type system check.
- **Parallelizing a workload smaller than L2** without explicit override. `CostModel` will refuse; bypassing it almost always regresses (icache cold, MESI ping-pong, TLB shootdowns).
- **`new`-allocating a Permission or ReadView**. Both have deleted `operator new` precisely because heap-allocating them defeats the lifetime contract. Stack only.

#### Composition rules

- `Permission<Tag>` IS already linear → wrapping in `Linear<Permission>` is redundant (and will fail the `is_writeonce_v` check pattern).
- Handles holding a `Permission` should be `Pinned` — the handle's existence is the proof of permission; moving the handle would break the proof.
- Use `[[no_unique_address]] Permission<Tag>` on handle members — collapses to 0 bytes via EBO.
- `SharedPermissionGuard` is move-only RAII → do NOT also wrap in `Linear<>`.
- `SharedPermissionPool` is `Pinned` — the atomic refcount IS the channel identity.
- `PermissionedSpscChannel` / `PermissionedSnapshot` / `PermissionedShardedGrid` / `PermissionedRwQueue` are all `Pinned` for the same reason — the underlying primitive's atomics ARE the channel.

---

## X. Footgun Catalog

Organized by category. Each has a remediation.

### Lifetime / ownership

| Footgun | Remediation |
|---|---|
| `return std::move(local)` — defeats NRVO | `-Werror=pessimizing-move -Werror=redundant-move` |
| `std::move` from `const` — silent copy | `-Werror=pessimizing-move` |
| Use-after-move | `-fanalyzer`; moved-from only destroyed or reassigned |
| Reference member in struct | Pointer instead; reference breaks ctor/copy |
| Dangling `string_view`/`span` return | `-Werror=dangling-reference -Werror=return-local-addr` |
| Implicit capture-by-reference in escape lambda | Capture by value or explicit pointer |
| Iterator invalidation during mutation | Never mutate while iterating; collect + apply |
| Pointer into arena after arena reset | Generation counter on arena in debug; ASan catches |
| Self-copy not guarded | Non-trivial copies = `delete`; trivial types fine |

### Object model

| Footgun | Remediation |
|---|---|
| Slicing (`Base b = derived`) | No copyable polymorphic types (we have no `virtual`) |
| Implicit shallow copy of pointer members | `-Werror=deprecated-copy -Werror=deprecated-copy-dtor`; explicit `= default` or `= delete("reason")` |
| Most vexing parse | Uniform init: `Widget w{};` always |
| ADL surprises | Qualify `std::` calls; prefer hidden friends for customization |
| Hidden virtual override typo | `-Werror=suggest-override`; `override` keyword mandatory |

### Numeric

| Footgun | Remediation |
|---|---|
| `uint16_t + uint16_t → int` promotion | `-Werror=conversion -Werror=sign-conversion -Werror=arith-conversion` |
| `for (size_t i = n-1; i >= 0; --i)` infinite loop | `-Werror=type-limits`; idiom `while (i-- > 0)` |
| Left shift of negative signed | Unsigned bitwise or `std::rotl`; UBSan |
| Shift ≥ bitwidth | UBSan `shift-exponent` |
| Float `==` | `-Werror=float-equal`; `std::abs(a-b) < eps` or bit compare |
| `NaN` compare silently false | Explicit `std::isnan()` check |
| `size_t` vs `ptrdiff_t` mix | `-Werror=sign-conversion` |
| Signed overflow at non-sat site | `-fno-strict-overflow` + `-fsanitize=signed-integer-overflow` in CI |

### Concurrency

| Footgun | Remediation |
|---|---|
| Data race on non-atomic | `-fsanitize=thread` in CI |
| `std::thread` destructor without join | `std::jthread` (auto-joins) |
| `std::atomic<T>` for non-lock-free T | `static_assert(std::atomic<T>::is_always_lock_free)` |
| Torn read on oversize atomic | same assert |
| Relaxed atomics | acquire/release discipline + code review |
| `std::shared_mutex` | Banned; use SPSC or lock-free CAS |
| `volatile` for atomicity | `std::atomic` only |

### Template / metaprogramming

| Footgun | Remediation |
|---|---|
| O(N²) variadic recursion | Fold expressions + pack indexing (C++26) |
| Deep SFINAE | Concepts (C++20) |
| Forwarding-ref ambiguity | Constrain with concepts |
| `decltype(x)` vs `decltype((x))` | Prefer `std::remove_reference_t<decltype(x)>` |

### Library

| Footgun | Remediation |
|---|---|
| `std::function` heap + indirect | `std::function_ref` or templated `auto&&` |
| `std::regex` slow / throws | Handrolled parser or skip |
| `std::async` thread launch | Explicit `std::jthread` with captured work |
| `std::endl` flush | `'\n'` |
| `std::vector<bool>` proxy | `std::bitset<N>` or custom |
| `std::rand` global | Philox |

### Preprocessor / build

| Footgun | Remediation |
|---|---|
| Macro name collision | `CRUCIBLE_` prefix; `#undef` if absolutely necessary |
| ODR violation | Every header self-contained; `inline` variables for constants |
| Static init order fiasco | No dynamic-init globals; `constexpr` / `constinit` only |
| Singleton static-local guard atomic | Explicit init from `main` / `Keeper::init` |
| Header include order sensitivity | Every header compiles standalone; IWYU discipline |

---

## XI. Canonical Patterns

### Strong ID

```cpp
#define CRUCIBLE_STRONG_ID(Name) \
    struct Name { \
        uint32_t value_ = UINT32_MAX; \
        constexpr Name() = default; \
        explicit constexpr Name(uint32_t v) noexcept : value_{v} {} \
        constexpr uint32_t raw() const noexcept { return value_; } \
        constexpr bool is_none() const noexcept { return value_ == UINT32_MAX; } \
        static constexpr Name none() noexcept { return Name{UINT32_MAX}; } \
        auto operator<=>(const Name&) const = default; \
    }
```

### NSDMI struct with strong IDs

```cpp
struct alignas(32) TraceEntry {
    OpIndex     op_idx;                           // default = none()
    SchemaHash  schema_hash;                      // default = 0
    ShapeHash   shape_hash;                       // default = 0
    MetaIndex   meta_head;                        // default = none()
    ScopeHash   scope_hash;                       // default = 0
    uint8_t     op_flags  = 0;
    uint8_t     pad[3]{};                         // explicit zero-init
};
static_assert(sizeof(TraceEntry) == 32);
```

### Arena allocation

```cpp
template<typename T>
[[nodiscard]] T* Arena::alloc_obj() {
    static_assert(std::is_trivially_destructible_v<T>,
                  "Arena does not call destructors");
    void* raw = bump(sizeof(T), alignof(T));
    if (!raw) [[unlikely]] std::abort();
    auto* obj = std::start_lifetime_as<T>(raw);
    ::new (obj) T{};                              // default-init (NSDMI fires)
    return obj;
}
```

### Contract on boundary, bare on hot path

```cpp
// Boundary: contract-checked
std::expected<PlanId, Error> submit_plan(PlanId id, std::span<const PatchValue> patches)
    pre  (!id.is_none())
    pre  (std::all_of(patches.begin(), patches.end(), valid_patch))
    post (r) (r.has_value() || !r->is_none())
{
    return runtime::submit_plan_inner(id, patches);
}

// Hot path: no contracts (compiled with contract-semantic=ignore)
CRUCIBLE_HOT void submit_plan_inner(...) {
    // Guaranteed by caller; zero runtime check.
}
```

### SPSC ring hot path

```cpp
CRUCIBLE_HOT bool try_push(const TraceEntry& entry) noexcept {
    uint64_t h = head_.load(std::memory_order_relaxed);  // own variable
    uint64_t t = tail_.load(std::memory_order_acquire);   // cross-thread
    if ((h - t) >= CAPACITY) [[unlikely]] return false;
    buffer_[h & MASK] = entry;
    head_.store(h + 1, std::memory_order_release);
    return true;
}
```

### Reflection-generated hash (GCC 16 only)

```cpp
#if CRUCIBLE_HAS_REFLECTION
template <typename T>
[[nodiscard]] consteval uint64_t reflect_hash(const T& obj) {
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a seed
    template for (constexpr auto field : std::meta::nonstatic_data_members_of(^T)) {
        h = detail::fmix64(h ^ hash_field(obj.[:field:]));
    }
    return h;
}
#endif
```

### Hot loop with all the hints

```cpp
CRUCIBLE_HOT void scan(
    const TraceEntry* __restrict__ in,
    uint64_t*         __restrict__ out,
    size_t n
) noexcept {
    in  = static_cast<const TraceEntry*>(__builtin_assume_aligned(in, 64));
    out = static_cast<uint64_t*>       (__builtin_assume_aligned(out, 64));
    [[assume(n > 0 && n % 8 == 0)]];

    for (size_t i = 0; i < n; ++i) {
        out[i] = in[i].schema_hash.raw();
    }
}
// GCC 16 emits clean AVX-512 strided load + store, no bounds churn.
```

---

## XII. Error Handling and Debug Assertions

No exceptions (compiled out via `-fno-exceptions`). Three tiers of error response:

| Class | Mechanism | Runtime cost | Example |
|---|---|---|---|
| **Impossible** (contract violation) | `pre` / `post` / `contract_assert` | 0 ns on hot path (semantic=ignore), check in debug | Null pointer, OOB index, invariant violation |
| **Expected-but-rare** | `std::expected<T, E>` return | ~1 ns (branch on `.has_value()`) | Parse error, shape out of bucket, peer timeout |
| **Catastrophic** | `crucible_abort(msg)` | — | OOM, hardware fault, corrupt state, FLR failure |

### Contract semantics per TU

Hot-path TUs compile contracts with `ignore` semantic. Boundary TUs use `enforce`. Switch per file:

```cpp
// At top of a hot-path .cpp:
#pragma GCC contract_evaluation_semantic ignore

// Boundary .cpp:
#pragma GCC contract_evaluation_semantic enforce
```

CI builds everything with `enforce`. Release builds default `observe` (log + continue) except hot TUs explicitly marked `ignore`.

### The canonical `std::expected` flow

```cpp
enum class CompileError : uint8_t {
    SchemaHashMismatch,
    ShapeOutOfBucket,
    RecipeNotInFleet,
    BackendCompileFailed,
    BudgetExceeded,
};

[[nodiscard]] std::expected<CompiledKernel, CompileError>
compile_kernel(effects::Bg bg, Arena& arena,
               const KernelNode& k, const TargetCaps& caps)
    pre (k.recipe != nullptr)
    pre (k.tile != nullptr)
{
    if (!fleet_supports(k.recipe, caps)) [[unlikely]]
        return std::unexpected(CompileError::RecipeNotInFleet);
    // ...
    return CompiledKernel{ ... };
}

// Caller:
auto r = compile_kernel(bg, arena, node, caps);
if (!r) [[unlikely]] {
    log_compile_failure(r.error(), node);
    return fall_back_to_reference_eager(node);
}
const auto& ck = *r;  // happy path
```

`std::expected` is a union + discriminator tag (≤ 24 B for most errors). Can't be silently ignored (`[[nodiscard]]`). No heap, no exception tables.

### Assertion macro triad

```cpp
// ── CRUCIBLE_ASSERT ────────────────────────────────────────────
// Always-on boundary precondition. Cheap check, runs in release.
// Maps to contracts; respects contract-evaluation-semantic.
#define CRUCIBLE_ASSERT(cond) contract_assert(cond)

// ── CRUCIBLE_DEBUG_ASSERT ──────────────────────────────────────
// Hot-path invariant. Check in debug, compiled out in release.
#ifdef NDEBUG
  #define CRUCIBLE_DEBUG_ASSERT(cond) ((void)0)
#else
  #define CRUCIBLE_DEBUG_ASSERT(cond) contract_assert(cond)
#endif

// ── CRUCIBLE_INVARIANT ─────────────────────────────────────────
// Fact the optimizer can exploit. `[[assume]]` in release (free).
// Debug branch uses crucible::detail::breakpoint_if_debugging — a
// libstdc++-16-shim for std::breakpoint_if_debugging (<debugging>
// header declares the symbol but libstdc++ 16.0.1 ships no
// definition; see Platform.h).
#ifdef NDEBUG
  #define CRUCIBLE_INVARIANT(cond) [[assume(cond)]]
#else
  #define CRUCIBLE_INVARIANT(cond) do {                                \
      if (!(cond)) [[unlikely]] {                                       \
          if (!::crucible::detail::is_debugger_present()) {             \
              fprintf(stderr, "invariant failed: %s (%s:%d)\n",         \
                      #cond, __FILE__, __LINE__);                       \
          }                                                             \
          ::crucible::detail::breakpoint_if_debugging();                \
          std::abort();                                                 \
      }                                                                 \
  } while (0)
#endif
```

**When to use which:**
- `CRUCIBLE_ASSERT` — public API entry. Contracts handle it.
- `CRUCIBLE_DEBUG_ASSERT` — SPSC ring bounds, arena bump sanity, RNG counter — hot path, can't afford a branch.
- `CRUCIBLE_INVARIANT` — loop trip counts, alignment, range bounds. The optimizer uses it.

### Abort path

```cpp
[[noreturn]] CRUCIBLE_COLD
void crucible_abort(const char* msg) noexcept {
    fprintf(stderr, "crucible: fatal: %s\n", msg);
    cipher::emergency_flush();       // try to salvage hot-tier state
    std::abort();                    // SIGABRT → core dump if enabled
}
```

Every abort path is `[[gnu::cold]]` + `noinline` so it lives in a cold section and doesn't pollute hot icache.

### Logging and tracing on hot path — banned

No `fprintf` / `std::cout` / `std::printf` / `std::format` on hot path — format parsing ≥100 ns, output syscalls flush buffers, noise pollutes measurement. Hot path uses atomic counters and structured events pushed to an SPSC ring for bg drain; human-readable output only on bg thread post-capture.

Development-mode trace gated on `NDEBUG` — release binary contains zero trace calls:

```cpp
#ifndef NDEBUG
  #define CRUCIBLE_TRACE(fmt, ...) \
      fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
  #define CRUCIBLE_TRACE(fmt, ...) ((void)0)
#endif
```

---

## XIII. Testing Discipline

Tests prove axioms hold. A failing test is an axiom violation, not a style issue.

### The load-bearing suite

These tests ARE the design guarantee. If they red, the guarantee is broken — stop and investigate.

| Test | Axiom(s) | Cadence |
|---|---|---|
| `bit_exact_replay_invariant` | DetSafe | Every PR |
| `cross_vendor_step_invariant` | DetSafe | Release gate (multi-backend) |
| `fleet_reshard_replay` | DetSafe + BorrowSafe | Release gate |
| `bit_exact_recovery_invariant` | DetSafe + MemSafe | Release gate |
| `checkpoint_format_stability` | DetSafe + LeakSafe | Every PR |
| `tsan_spsc_ring_*` | ThreadSafe + BorrowSafe | Every PR (tsan preset) |
| `asan_arena_lifetime_*` | MemSafe + LeakSafe | Every PR (default preset) |
| `ubsan_numeric_*` | TypeSafe + InitSafe | Every PR (default preset) |
| `stress_repeat_50x` | ThreadSafe (race detector) | Nightly |

If `bit_exact_replay_invariant` reddens — STOP. Hidden state was introduced. Never merge.

### Per-axiom test coverage

Each new struct / function adds at least one test that exercises its axiom claims:

```cpp
// Axiom InitSafe: default-constructed state is fully specified
TEST(Axiom_InitSafe, TensorSlot_DefaultIsWellDefined) {
    TensorSlot s{};
    ASSERT_EQ(s.offset_bytes, 0u);
    ASSERT_TRUE(s.slot_id.is_none());
    ASSERT_EQ(s.dtype, ScalarType::Undefined);

    // Padding is zero (NSDMI + P2795R5 guarantee)
    const auto* raw = reinterpret_cast<const std::byte*>(&s);
    for (size_t i = offsetof(TensorSlot, pad); i < sizeof(TensorSlot); ++i)
        ASSERT_EQ(std::to_integer<uint8_t>(raw[i]), 0u);
}

// Axiom TypeSafe: strong IDs reject silent swap
TEST(Axiom_TypeSafe, OpIndexSlotIdNotInterchangeable) {
    static_assert(!std::is_convertible_v<OpIndex, SlotId>);
    static_assert(!std::is_convertible_v<SlotId, OpIndex>);
}
```

### Unit test discipline

1. **One test, one claim.** Prefix with `Axiom_<Name>_<Case>` or `Behavior_<Feature>_<Case>`.
2. **Deterministic.** Fixed seed `42`. No `std::rand`, no `system_clock`, no reading `/proc` or network. Reproducibility is load-bearing.
3. **Isolated.** Each test builds its own arena / rings / ops. No shared state between tests.
4. **Fast.** < 100 ms per test (except stress/replay). ~100 tests × 100 ms = 10 s CI — keeps iteration tight.
5. **No retries.** A flaking test = race condition or bug. Fix it. Never `--repeat until-pass`.
6. **Every contract has a test that violates it.** Use `EXPECT_DEATH(fn(bad_args), ".*contract.*")` to prove contracts fire.

### Sanitizer preset matrix

```bash
# ASan + UBSan + analyzer: MemSafe, NullSafe, TypeSafe, InitSafe
cmake --preset default && cmake --build --preset default && ctest --preset default

# TSan: ThreadSafe, BorrowSafe
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan

# Stress (repeat-until-fail ×50): ThreadSafe race detection
ctest --preset stress

# Release: perf-bound tests, catches release-only codegen bugs
cmake --preset release && cmake --build --preset release && ctest --preset release

# Verify: contract enforcement + internal small-SMT (deferred)
cmake --preset verify && cmake --build --preset verify && ctest --preset verify
```

Pre-merge gate: `default` + `tsan` + `release`. Release gate: all four + `verify`.

### Property-based + fuzz

For structural invariants (Merkle hash stability, arena aliasing, IR normal form), property tests with Philox-seeded inputs:

```cpp
TEST(Property_ContentHash_StableUnderEdgePermutation) {
    for (uint64_t seed : {42ULL, 0xBADC0FFEE0DDF00DULL, 0xDEADBEEFCAFEBABEULL}) {
        Philox rng{seed};
        auto g  = generate_random_graph(rng, /*nodes=*/100);
        auto gp = permute_unordered_edges(g, rng);
        ASSERT_EQ(g.root_hash(), gp.root_hash())
            << "hash depends on edge order — DetSafe violation";
    }
}
```

Fuzzing via AFL++ / libFuzzer targets untrusted-input paths (Cipher deserialize, Merkle DAG load, recipe registry parse). Hot paths don't need fuzz — their inputs are already well-formed by construction.

### Benchmarks are not tests

`bench/` and `test/` are separate trees. Bench asserts measure **numerical results** on the dev hardware and gate against the previously-recorded baseline; binary-size and other structural-cap asserts (e.g. ≤ 500 KB binary) live in test. A test that accidentally measures latency is bad design — split it. Benchmarks can be noisy; tests cannot.

### Coverage targets

- Every public `.h` has a test.
- Every `pre` / `post` has a test that exercises both success AND violation.
- Every enum variant has a test (round-trip if serialized).
- Every `std::expected` error has a test that triggers it.
- Every `[[assume]]` has a test that proves the condition holds at every reachable call site.

No "% line coverage" number — coverage **of the 8 axioms** is the metric.

---

## XIV. Platform Assumptions

Hardcoded values. Changing any requires an audit sweep of the affected macros and `alignas`.

| Assumption | Value | Why |
|---|---|---|
| Cache line size | **64 bytes** | x86-64, most ARM. `alignas(64)` pervasive |
| Small page size | **4 KB** | x86-64 default. ARM may differ — use `sysconf(_SC_PAGESIZE)` at runtime for variable code |
| Huge page size | **2 MB** | x86-64 default huge page |
| Memory model | **TSO (x86) / weakly-ordered (ARM)** | Acquire/release correct on both |
| Endianness | **Little-endian** | x86 + ARM in practice; hashing assumes this |
| Word size | **64-bit** | No 32-bit support |
| Min x86 baseline | **AVX2 + FMA + BMI2** | Haswell-and-later |
| Optional x86 uplift | AVX-512, AMX | Opt-in per build via `-march=`; never assumed at the source level |
| Min ARM baseline | **ARMv8.2-A + NEON** | Graviton 2+, Apple M1+ |
| Float representation | **IEEE 754** | `-fno-fast-math` enforces |
| Stack size | **8 MB** (Linux default) | Large arrays → arena, not stack |
| Canonical VA bits | **48** (x86-64 without LA57) | Pointer-tagging schemes assume this |
| Thread API | **pthreads** (via `std::jthread`) | Linux only |
| Syscall ABI | **Linux x86_64 / aarch64** | No Windows, no macOS in production |

### Platform checks at build time

```cpp
// In Platform.h or a dedicated Assumptions.h
static_assert(sizeof(void*) == 8, "64-bit required");
static_assert(std::endian::native == std::endian::little, "little-endian required");

#if defined(__aarch64__) && defined(__APPLE__)
  #error "Apple Silicon has 128-byte cache lines — alignas(64) is insufficient. Audit before enabling."
#endif

#if !defined(__x86_64__) && !defined(__aarch64__)
  #error "Crucible targets x86-64 and aarch64 only."
#endif
```

### When the assumptions change

- **New architecture** (RISC-V, Power): audit every `alignas(64)`, every `_mm_pause`, every endian-sensitive `bit_cast`.
- **Apple Silicon target**: cache line is 128 B. All `alignas(64)` becomes `alignas(CRUCIBLE_CACHE_LINE)` where the macro resolves per platform.
- **ARM 16 KB pages** (Apple, some Android): audit huge-page / `mmap` / `MADV_HUGEPAGE` code.
- **32-bit or 128-bit target**: not supported. Reject.

### Determinism across platforms

Under `BITEXACT_STRICT`, the same IR + same seed produces byte-identical output on any supported platform. This works because:

- IEEE 754 + `RN` rounding + `-fno-fast-math` = deterministic FP
- Philox4x32 is platform-independent (counter-based, bit-exact spec)
- Memory plan offsets are content-addressed (no alloc-order dependency)
- Canonical reduction topology (UUID-sorted binary tree)
- `std::bit_cast` for serialization — no endian-dependent `reinterpret_cast`

CI verifies with `cross_vendor_step_invariant`. A new platform must pass this test before shipping.

---

## XV. Headers, Includes, and Modules

### Header discipline

1. **`#pragma once`** on every header. No include guards.
2. **Self-contained.** Every header compiles standalone. Add required includes directly; never rely on transitive pull-in.
3. **IWYU** (include-what-you-use). If a `.cpp` uses `std::span`, include `<span>` — not via some project header that happens to pull it.
4. **No circular includes.** Refactor: one side gets a forward declaration, full include in the `.cpp` only.
5. **Forward declare in headers whenever possible.** Full definitions only when needed (inline methods, templates, `sizeof`).
6. **No precompiled headers.** PCH hides dependency bugs and complicates CMake. If build is slow, audit headers.

### Include order convention

```cpp
// In a .cpp file:
#include <crucible/ThisUnit.h>        // 1. own header first (catches missing includes)

#include <crucible/OtherUnits.h>      // 2. project, alphabetical

#include <third_party/lib.h>          // 3. third-party (rare in Crucible)

#include <cstdint>                    // 4. C stdlib
#include <atomic>                     // 5. C++ stdlib
```

### Hot path is header-only

- **Hot path** (`TraceRing`, `Arena`, `Graph`, `Expr`, `MerkleDag`, `CKernel`, `PoolAllocator`): header-only. Enables cross-TU inlining even without LTO.
- **Cold path** (Cipher cold tier, serialization, CLI, tools): `.h` + `.cpp` split. Reduces rebuild cost when implementation changes.
- **Templates**: always header-only (mandatory).
- **`inline` functions**: header-only with `inline` keyword (ODR).

### Namespace discipline

- All code in `namespace crucible` or nested (`crucible::mimic::nv`, `crucible::forge::phase_d`).
- **No** `using namespace std;` at file or namespace scope, ever. Inside function body OK when the benefit is clear.
- Anonymous namespaces for TU-local helpers. Not the `static` keyword (deprecated for this purpose).
- Don't use `using X::foo;` to leak implementation details from a nested namespace to a parent.

### C++20 Modules — deferred

GCC 16 has ABI-stable modules but we defer: CMake/ninja integration still maturing, debug info less mature than headers, and `-flto=auto` on header-only hot paths already delivers cross-TU inlining. Revisit when clean-rebuild exceeds ~30 s or modules become part of the public API. Until then: `#include`-based, header-only hot, split cold.

### One-definition rule (ODR) discipline

- `inline` on every function in a header (including single-line getters).
- `inline` on every `constexpr` variable in a header.
- Template specializations: inline + in a header.
- Anonymous namespace for TU-local: never in a header.

---

## XVI. Safety Wrappers

Library types in `include/crucible/safety/` that mechanize the axioms from §II at compile time. Every wrapper is a phantom-type newtype with **zero runtime cost** — `sizeof(Wrapper<T>) == sizeof(T)`, same machine code as the bare primitive under `-O3`. Generated assembly is indistinguishable from the unwrapped equivalent.

### Header catalog

| Header | Axioms it enforces | Role |
|---|---|---|
| `Linear.h` | MemSafe, LeakSafe, BorrowSafe | Move-only `Linear<T>`. `.consume() &&` takes ownership; `.peek() const&` borrows. Construction is `[[nodiscard]]`. |
| `Refined.h` | InitSafe, NullSafe, TypeSafe | `Refined<Pred, T>` — predicate checked by contract at construction; function bodies treat the invariant as `[[assume]]` downstream. |
| `Secret.h` | DetSafe + information-flow discipline | Classified-by-default `Secret<T>`. Escapes only via `declassify<Policy>()` with a grep-able `secret_policy::*` tag. |
| `Tagged.h` | TypeSafe | Phantom tags for provenance (`source::FromUser`, `source::FromDb`, `source::Internal`) and trust (`trust::Verified`, `trust::Unverified`). Mismatch at call sites = compile error. |
| `Session.h` | BorrowSafe | Type-state protocol channels. Each `.send()` / `.recv()` returns a new type carrying the remaining protocol. Wrong order or missing step = compile error. State lives in the type; zero runtime cost. |
| `Checked.h` | TypeSafe, DetSafe | `checked_add` / `wrapping_add` / `trapping_add` over `__builtin_*_overflow`. `std::add_sat` / `std::mul_sat` / `std::sub_sat` pass-through for saturation. |
| `Mutation.h` | MemSafe, DetSafe | `AppendOnly<T>` — no erase/resize. `Monotonic<T, Cmp>` — advance-only with contract guard on the step. |
| `ConstantTime.h` | DetSafe (side-channel resistance) | `ct::select`, `ct::eq`, branch-free primitives for crypto paths and Cipher key handling. |
| `Permission.h` | BorrowSafe, ThreadSafe, MemSafe | `Permission<Tag>` — phantom-typed move-only token (sizeof = 1, EBO-collapsible) encoding CSL frame rule. `SharedPermission<Tag>` + `SharedPermissionPool` for fractional read sharing (atomic refcount + mode upgrade). `ReadView<Tag>` for lifetime-bound borrows. Factories: `permission_root_mint` / `permission_split` / `permission_combine` / `permission_split_n`. |
| `PermissionFork.h` | ThreadSafe, BorrowSafe | `permission_fork<Children...>(parent, callables...)` — encodes CSL parallel composition rule as RAII fork-join over `std::jthread`. Constraint: `splits_into_pack_v<Parent, Children...>`. Returns parent permission after all children join. |

Each header is ≤150 lines except `Permission.h` (≤500 — substantial doc + the fractional-permission machinery), header-only, depending only on `<type_traits>`, `<atomic>`, and `Platform.h`.

### Usage rules

1. **Public API params wrap raw primitives.** `fn(Refined<positive, int> n)` — never `fn(int n)`. Bodies then trust the invariant without re-validating.
2. **Every resource type wraps in `Linear<T>`** — file handles, mmap regions, TraceRing, channel endpoints, arena-owned objects with drop semantics.
3. **Every load-bearing predicate gets a named alias** — `PositiveInt`, `NonNullTraceEntry`, `ValidSlotId`, `NonEmptySpan<T>`. Not anonymous refinements at call sites.
4. **Every classified value wraps in `Secret<T>`** — Philox keys, Cipher encryption keys, private weights, credentials. Declassification requires a `secret_policy::*` tag.
5. **Every trust-boundary crossing uses `Tagged<T, source::*>`** — deserialized input, network payload, FFI return. Sanitized-only APIs demand `source::Internal`.
6. **Every fixed-order protocol uses `Session<...>`** — handshakes, init sequences, channel lifecycles, plan-chain acquisition.
7. **Every append-only or monotonic structure wraps** in `AppendOnly<>` / `Monotonic<T, Cmp>` — event logs, generation counters, version numbers, Cipher warm writes.
8. **Every crypto path uses `ct::*` primitives** for comparisons and selections. Non-CT code in a `with Crypto` context is a review reject.
9. **Every concurrent producer/consumer endpoint wraps in `Permission<Tag>`** — handles holding a Permission are `Pinned`; cross-thread handoff goes through `permission_fork` (structured concurrency), `SharedPermissionPool::lend()` (refcounted shared read), or move-into-`std::jthread`-lambda (single owner). Never a raw `std::thread` without a Permission token; never two threads simultaneously calling the same `try_push` on a shared queue without a Permission split. The cache-tier rule (§IX) decides whether to actually parallelize — Permissions just prove the access pattern is sound.

### Compiler enforcement

- `-Werror=conversion` + the wrapper types together prevent accidental unwrapping across boundaries.
- `-Werror=use-after-move` + `Linear<>`'s deleted copy constructor catches double-consume at compile time. Same mechanism catches `Permission<Tag>` double-use after split/fork.
- `[[nodiscard]]` on every wrapper type's constructor forces the caller to capture the return value.
- Contracts on `Refined<>` and `Monotonic<>` constructors fire at construction sites under `semantic=enforce` (debug, CI, boundary TUs) and under `semantic=ignore` on hot-path TUs they compile to `[[assume]]` hints, optimizing downstream code as if the invariant always holds.
- Deleted copy + defaulted move on `Linear<>` / `Secret<>` / `Session<>` / `Permission<Tag>` means the compiler rejects accidental duplication.
- `permission_split`, `permission_combine`, `permission_split_n`, `permission_fork` all `static_assert` on `splits_into_v` / `splits_into_pack_v` — splitting into undeclared subregions is a compile error, naming the missing trait specialization in the diagnostic.
- Contract violations abort via `std::terminate` (P1494R5), never invoke undefined behavior.

### Review enforcement

Rules for code review and grep-guards:

- `declassify<` without a `secret_policy::*` policy tag → reject.
- `const_cast` → reject (banned per §III).
- `reinterpret_cast` → reject; use `std::bit_cast`.
- `std::chrono::system_clock` → reject (use `steady_clock` or `rdtsc`).
- A new public API taking raw `int`, `size_t`, `void*`, or `T*` without a wrapper → questioned on review; almost always rewritten.
- A new resource-carrying type without `Linear<>` → questioned; must have justification.
- Any `[[unlikely]]` body of more than 8 non-trivial lines without being outlined into a `CRUCIBLE_COLD` helper → reject.
- A new concurrent producer/consumer pair without a `Permission<Tag>` discipline → questioned; bare `std::thread` + raw atomic SPSC is an old-style pattern; new code uses `Permission<Tag>` for the static safety + `permission_fork` for handoff.
- A `permission_root_mint<X>()` call site outside `main()` / a Vessel/Keeper init function → reject; root-mint is once-per-program-per-tag and review-discoverable via `grep permission_root_mint<` exactly because of this rule.
- A new `splits_into<...>` or `splits_into_pack<...>` specialization in a header far from its tag tree's declaration → questioned; the manifest belongs in the same TU as the tags so reviewers see the whole region tree at one glance.
- A `Permission<Tag>` stored in a struct field of a type that is itself shared between threads (i.e., not Pinned + not handle-pattern) → reject; defeats linearity.
- Bypassing `AdaptiveScheduler` to spawn N raw threads when working set is L2-resident → questioned; cache-tier rule (§IX) says sequential wins. Override requires bench evidence and a justification comment.

### Canonical wrapper-nesting order (FOUND-I03)

Composition is **wrapper-nesting**, not mega-product-lattice. Each `Graded<Modality, Lattice, T>` instantiation is one algebraic slice; multi-axis composition stacks them, outer-to-inner. The order is canonical, not aesthetic — wrapper-nesting is order-sensitive (`Stale<Tagged<T>>` ≢ `Tagged<Stale<T>>`) and the canonical order is what `row_hash` (`safety/diag/RowHashFold.h`, FOUND-I02) folds along when computing federation cache keys. Out-of-order stacks compile fine but produce DIFFERENT row hashes; review questions deviations unless the author documents a deliberate-different-cache-slot intent.

```
HotPath ⊃ DetSafe ⊃ NumericalTier ⊃ Vendor ⊃ ResidencyHeat ⊃
  CipherTier ⊃ AllocClass ⊃ Wait ⊃ MemOrder ⊃ Progress ⊃
  Stale ⊃ Tagged ⊃ Refined ⊃ Secret ⊃ Linear ⊃ Computation
```

Outer wrappers carry "higher-level" properties (where in the system this runs, what tier it serves); inner wrappers are "closer to the value" (provenance tags, refinement predicates, classification, ownership). Reading example bottom-up: the value `T` is wrapped in `Computation<Row, T>` to declare its OS-effect row, then in `Linear<>` to declare exclusive ownership, then in `Secret<>` to mark as classified, and so on outward. Reading top-down: `HotPath<Hot, ...>` says "this lives on the hot path", and the rest of the stack refines what kind of hot-path value.

Worked example — a tensor that comes back from a Bg-context kernel, BITEXACT, NV vendor, hot-path:

```cpp
HotPath<HotPathTier::Hot,
    DetSafe<DetSafeTier::Pure,
        NumericalTier<NumericalTier::BITEXACT,
            Vendor<VendorBackend::NV,
                Computation<Row<Effect::Bg>, ResultTensor>>>>>
```

Each layer EBO-collapses if its grade is a type-level singleton (regime-1 or regime-2 per `algebra/GradedTrait.h`). The 5-deep nest is `sizeof(ResultTensor) + at most a few bytes for non-singleton grades + alignment` — usually exactly `sizeof(T)`.

**F\*-style named aliases** (FOUND-G79/80, `effects/FxAliases.h`) provide canonical compositions for the common cases — `Pure<int>` is "Progress<Terminating, DetSafe<Pure, Computation<Row<>, int>>>", `Tot<E_os, T>` is "Progress<Terminating, DetSafe<Pure, Computation<E_os, T>>>", etc. Use the aliases at production call sites; the substrate's per-wrapper composition is the authoritative algebraic story but verbose for everyday code.

**Order-discipline summary:**

1. Wrapper authors construct stacks in canonical order. Deviations question on review unless commented with a deliberate cache-slot-separation rationale.
2. `row_hash_contribution<W<Inner>>` specializations follow: `combine_ids(<W's tag bits>, row_hash_contribution_v<Inner>)`. The Boost-style combiner is order-sensitive — once a wrapper W ships its specialization, `W<X>` and `X` fold to different hashes, and stacks `W1<W2<T>>` vs `W2<W1<T>>` produce different hashes (different cache slots, different semantics).
3. `Computation<R, T>` is the innermost member of every effect stack — it is the carrier; everything else is metadata about the carrier. The `row_hash_contribution<Computation<R, T>>` specialization (FOUND-I02-AUDIT) folds the row R "outer" and the payload T's contribution "inner".
4. **Append-only Universe extension** (FOUND-I04 backlog): adding a new effect atom (e.g., `Effect::Refute`) is permitted only at the next free position; existing atom positions never change. This bounds cache invalidation to entries that actually mention the new atom — `Row<Effect::Bg>` keeps the same hash forever because `Effect::Bg`'s underlying value never changes.

**Currently shipped row_hash specializations (FOUND-I02 + FOUND-I02-AUDIT + GAPS-028/029):**

- `row_hash_contribution<effects::Row<Es...>>` — sort-fold over Effect underlying values, cardinality-seeded.
- `row_hash_contribution<effects::Computation<R, T>>` — combine_ids(R-hash, T-hash); payload-blind for bare T, row-discriminating, nested-non-collapsing.
- `row_hash_contribution<HotPath<Tier, T>>`
- `row_hash_contribution<DetSafe<Tier, T>>`
- `row_hash_contribution<NumericalTier<Tier, T>>`
- `row_hash_contribution<Vendor<Backend, T>>`
- `row_hash_contribution<ResidencyHeat<Tier, T>>`
- `row_hash_contribution<CipherTier<Tier, T>>`
- `row_hash_contribution<AllocClass<Tag, T>>`
- `row_hash_contribution<Wait<Strategy, T>>`
- `row_hash_contribution<MemOrder<Tag, T>>`
- `row_hash_contribution<Progress<Class, T>>`
- `row_hash_contribution<Stale<T>>`
- `row_hash_contribution<Tagged<T, Source>>`
- `row_hash_contribution<Refined<Pred, T>>`
- `row_hash_contribution<Secret<T>>`
- `row_hash_contribution<Linear<T>>`

All 16 entries in the canonical wrapper-nesting order ship `row_hash_contribution` specializations as of 2026-05-05 (`cc11141`). Nested compositions hash differently based on wrapper order — `Stale<Tagged<T>>` and `Tagged<Stale<T>>` produce distinct federation-cache-slot keys. The discipline is regression-tested in `test/test_migration_verification.cpp` nesting-order cells per GAPS-029.

`safety/DimensionTraits.h` also pins the wrapper × lattice × modality × tier quadruple for every shipped Graded-backed safety wrapper via `wrapper_dimension<W>`, `wrapper_tier_v<W>`, and `verify_quadruple<W>()` (GAPS-091). `TimeOrdered<T, N, Tag>` is deliberately Tier-L (`Representation`) over `HappensBeforeLattice<N, Tag>`; `EpochVersioned<T>` is deliberately Tier-V (`Version`) over the epoch/generation product lattice.

### GCC 16 contracts — implementation gotchas

Real-world issues encountered implementing the wrappers.  Document them here so the next person doesn't rediscover them.

**Contract clause order on member functions.**  On a constructor or member function, `pre` / `post` must appear *after* `noexcept` and before the member initializer list (or body).  Newlines between clauses don't cost compilation, but `pre` separated from the colon by other tokens will not parse.

```cpp
// ✓ CORRECT
constexpr R(int x) noexcept pre(x > 0) : v{x} {}

// ✗ WRONG — pre before noexcept
constexpr R(int x) pre(x > 0) noexcept : v{x} {}

// ✗ WRONG — pre separated from colon by a different clause order
constexpr R(int x)
    pre(x > 0)         // parser error: expected ‘;’
    noexcept
    : v{x} {}
```

**`-fcontracts` and `-freflection` require `-std=c++26`.**  CMake's compiler-probe step runs before the project's `CMAKE_CXX_STANDARD` takes effect, so putting these flags in `CMAKE_CXX_FLAGS` via the preset breaks configuration.  Instead, set them at target level via `target_compile_options(crucible INTERFACE -freflection -fcontracts)` after `project()` has declared the standard.

**`handle_contract_violation` must be defined by the program.**  GCC 16 / libstdc++ 16 does not ship a default handler.  Every program that enables contracts must provide one; otherwise the link fails with `undefined reference to handle_contract_violation(std::contracts::contract_violation const&)`.  The project default belongs in a shared `CrucibleContractHandler.cpp` wired to `crucible_abort()`.

```cpp
#include <contracts>
void handle_contract_violation(const std::contracts::contract_violation& v) {
    fprintf(stderr, "contract: %s\n", v.comment());
    std::abort();
}
```

**Postconditions with value parameters.**  P2900R14 forbids using a by-value parameter in a postcondition unless it is `const`.  `fn(int n) post(r: r == n*2)` fails with "value parameter used in postcondition must be const"; write `fn(int const n) post(r: r == n*2)` instead.  Fix is trivial once you know; confusing if you don't.

### GCC 16 reflection — implementation gotchas

**Reflection APIs return `std::vector<std::meta::info>`.**  Since `std::vector` uses `operator new`, you cannot assign the result directly to a non-`static` `constexpr` local and then iterate it with `template for` — the `operator new` allocation is not constant in the expansion-statement's required-constant-expression context.

```cpp
// ✗ WRONG — non-static constexpr local crossing consteval→runtime
constexpr auto members = std::meta::nonstatic_data_members_of(^^T, ctx);
template for (constexpr auto m : members) { ... }  // ERROR

// ✓ CORRECT — static gives the local a constant address
static constexpr auto members = std::define_static_array(
    std::meta::nonstatic_data_members_of(^^T, ctx));
template for (constexpr auto m : members) { ... }
```

`std::define_static_array` (P3491R3) materializes the vector into a static constexpr array, bypassing the allocator boundary.

**`access_context::current()` is mandatory.**  Reflection introspection primitives take an `access_context` as their second argument (P3293R3 plumbing).  `std::meta::nonstatic_data_members_of(^^T)` compiles, but returns fewer members than expected; always pass `std::meta::access_context::current()`.

**Unstructured bindings in `template for`.**  An expansion statement iterating a pack where each element needs destructuring must use a `constexpr auto` binding, not `auto&`.  `&` reference bindings create a non-constant expression.

### GCC 16 toolchain — build gotchas

**Clangd parses with stale config after a toolchain swap.**  After dropping Clang from the presets, IDE diagnostics may still show `Unknown argument: '-freflection'` and `'__config_site' file not found` errors.  Delete `build/compile_commands.json` and reconfigure: `rm -rf build && cmake --preset default` so clangd re-reads the new compile flags.  The actual build is unaffected.

**Runtime libstdc++ resolution.**  Binaries compiled with the local GCC 16 tree link against that tree's `libstdc++.so.6` which is newer than the system's.  Without an rpath, they fail at runtime.  The `default` preset's `CMAKE_EXE_LINKER_FLAGS` / `SHARED_LINKER_FLAGS` / `MODULE_LINKER_FLAGS` all carry `-Wl,-rpath,$(gcc16 prefix)/usr/lib64`; verify with `readelf -d build/test/test_X | grep RUNPATH`.

### What the wrappers do not cover

- **Flow-sensitive refinement propagation.** GCC does not prove a `Refined<>` invariant holds across multiple function boundaries. The wrapper checks at construction; the body inside the function trusts the invariant. Chaining requires either re-wrapping at each boundary or an SMT-discharged proof via the `verify` preset (§I).
- **Alias analysis.** `Linear<>` catches double-consume on a single value but not two pointers to the same underlying object created through unsafe channels. Review + `-fsanitize=address` + the axioms from §II are the line of defense.
- **Compile-time information flow.** Branching on a `Secret<>` value is not rejected at compile time. `ct::*` primitives and the constant-time discipline (§III opt-out of `memory_order::consume`, crypto paths using `ct::select`) are opt-in.

For those properties, the `verify` preset reserves space for an internal small-SMT solver (deferred — interim: contract enforcement only); see §I. No external solver dependency.

---

## XVII. Identifier and Readability Discipline

Code tells a story. Reading a function should read like prose — a noun subject, a verb action, an adjective condition. Every identifier reads as a sentence fragment that narrates what the thing IS or DOES. The primary reader of this codebase is an agentic LLM — so names carry semantic weight equal to types. Under `-fno-rtti` + `-fno-exceptions` an identifier's spelling is often the only remaining signal an automated tool has about semantics; ambiguous names degrade grep, code review, and future refactoring equally.

### Names by part-of-speech

Pick the word class intentionally:

- **Nouns** for data, state, fields, values: `arena`, `pool`, `recipe`, `resolvedBackend`, `pendingRegion`, `completedLength`. Plural for collections: `slots`, `edges`, `operands`, `pendingObligations`.
- **Verbs** for actions — functions, methods, effectful steps: `buildTrace`, `interpretRegion`, `classifyKernel`, `publishKernel`, `emitDiagnostic`. Lead with a verb. Factory helpers: `makeRegion`, `makeLoop`, not `regionFromSpec`.
- **Adjectives / past participles** for transformed values: `alignedSize`, `sortedArgs`, `pinnedSlots`, `deadNodes`, `canonicalHash`, `validatedEntry`. Describes what happened.
- **Question verbs** (`is`, `has`, `should`, `must`, `can`, `will`, `was`, `needs`) for booleans and predicates — see telling-word rule below.

### Telling-word rule for predicates and booleans

Every `bool`, every `[[nodiscard]] bool` query, every `*_flag` name, every `is_*`/`has_*` axiom discipline MUST start with or contain a question verb so a reader can mentally complete the sentence:

- `is_compiled`, `is_recording`, `is_flushing`, `is_valid`, `is_initialized`, `is_mutable` — "Is this <X>?"
- `has_scalar_args`, `has_pending_region`, `has_reflected_hash`, `has_tensors` — "Does this have <X>?"
- `should_retain_mode_on_divergence`, `should_suppress_recording` — "Should we <X>?"
- `must_terminate`, `must_be_contiguous`, `must_be_pinned` — "Must this <X>?"
- `can_fuse`, `can_coerce`, `can_elide_guard` — "Can we <X>?"
- `will_publish`, `will_block_on_wait` — "Will this <X>?"
- `was_consumed`, `was_declassified`, `was_validated` — "Was this <X>?"
- `needs_rehash`, `needs_reclassify`, `needs_flush` — "Does this need <X>?"

**Forbidden predicate shapes** (reject on review):

- `ok`, `valid` (standalone, without `is_`), `good`, `done`, `flag`, `check`, `b`, `p`, `pred`, `set` — these do not form a sentence; the reader cannot tell what is being asked. Rename: `ok → was_dispatched_without_divergence`; `valid → is_well_formed`; `done → has_reached_terminal_state`; `flag → a named is_*`.
- Negation via prefix `not_`: prefer the positively-named inverse (`is_consumed` over `is_not_consumed`; write `!is_consumed` at the call site). Double negatives (`not_unused`) are banned.

Test names (fuzzer property checks, gtest-style names) obey the same rule: `prop_hash_determinism` (property under test), `invariant_bit_exact_replay` (invariant being checked), `prop_kernel_cache_roundtrip` (behavior exercised). Never `test1`, `test_bug`, `my_test`.

### Hard rules

- **Banned**: any non-ASCII character in a C++ identifier (variable, parameter, function, template parameter, member, namespace). No Γ, no α, no ω, no μ, no ≤. Doc-comments MAY cite papers or specs with Unicode (`fmix64` from xxHash, the `Θ(log n)` complexity of Chase-Lev). Code MAY NOT.
- **Banned**: single-character identifiers (`g`, `t`, `e`, `s`, `x`, `a`, `b`, `n`, `r`) in any scope except numeric `for` loop induction over a range.
- **Banned**: two-character identifiers (`ty`, `ex`, `fn`, `st`, `pt`, `tc`, `ok`, `nf`). Abbreviations are not names.
- **Discouraged**: identifiers ≤ 3 characters. Prefer `scope` over `sc`, `param` over `p`, `binder` over `b`, `result` over `r`, `grade` over `g`, `index` over `i`.

### Allowed exceptions (canonical technical terminology)

Five categories of short names are exempt because they ARE the standard vocabulary in their domain — renaming them would make the code less recognizable, not more:

- **Crucible ontology primitives**: `Vigil`, `Keeper`, `Relay`, `Cipher`, `Canopy`, `Meridian`, `Augur`, `Vessel` are full words and fine at any length. Their short aliases in hot paths are not — use the full name.
- **Hot-path idiomatic short names** canonical in Crucible: `op` (Op), `args` (const Expr* const*), `nargs` (uint8_t), `ndim` (uint8_t), `dtype` (ScalarType), `arena` (`effects::Alloc` cap-tag — the canonical short name for the alloc-capability parameter on every Arena/ExprPool/MerkleDag/Graph allocator function), `ctx` (CrucibleContext), `bg` (`effects::Bg` context — local-variable name for the background-thread context that aggregates Alloc + IO + Block caps), `fg` (foreground sentinel; no `effects::*` analogue because hot-path code holds no capability), `ms` (MetaIndex strong ID), `ring` (TraceRing&). Established in TraceRing.h / ExprPool.h / MerkleDag.h; rename would be churn.
- **Binary-operation sides** (the FX/parser convention, preserved): `lhs` / `rhs` inside `add(lhs, rhs)`, `mul(lhs, rhs)`, `compare(lhs, rhs)`. Fine in accessors (`binop_lhs()`, `binop_rhs()`) because they project fields whose semantics are exactly "left side" / "right side".
- **Loop induction variables** over a compile-time small range: `i`, `j`, `d` (dimension), `k` inside `for (uint8_t d = 0; d < ndim; ++d)`. `d` for dimension is idiomatic because `ndim` is the canonical spelling of the upper bound.
- **Template type parameters** in generic code: `T`, `U`, `V`, `T1`, `T2` are canonical STL-style naming. A template parameter named `Predicate` is fine; one named `Fn` or `F` depends on role — a type-erased callable is `Callable` or `Predicate`, not `F`. Single-letter OK only for type-level `T`-style.

### Required patterns

- Every helper function name states what it does (`build_trace`, not `trace`; `classify_kernel`, not `classify`; `compute_storage_nbytes`, not `nbytes`). Exception: the hot-path idiomatic short names above.
- Pattern variables / structured bindings name their role: `auto [birth_op, death_op] = slot_lifetime(s)` — not `auto [b, d]`. Match arms / switch on `kind`: the bound variable names its semantic role (`matched_op` / `diverged_op`, not `mi` / `di`).
- Every intermediate `let`-binding (const local) is named after its role: `const uint32_t aligned_size = (n + ALIGNMENT - 1) & mask;` — not `const uint32_t n2`, not `tmp`. Crucible's `auto te = ops[i];` is fine because `te` in that file is consistently "TraceEntry reference" — established canonical.
- Fold / loop accumulators name the element type in plural or the semantic role: `uint64_t content_hash`, `uint32_t total_inputs`, `size_t num_edges` — not `acc`, `r`, `sum`.
- Boolean helpers follow the telling-word rule: `is_linear_grade`, not `linear_check`; `has_refinement_clause`, not `refinement?` (we're C++, no trailing `?`); `should_reject`, not `reject`.

### Lemma / theorem / invariant names

Lean code in `lean/Crucible/` and C++ test names obey the same rule: compose a question: `isConsumed_impliesGradeZero`, `bitExact_ofReplay`, `wellFormed_ofChecked`. Never `lemma1`, `wf_thm`, `tc_test`. Test-suite names that describe the invariant should lead with the invariant name, not the test index.

### Apply unconditionally to new code. Apply opportunistically to existing code when refactoring — don't open rename-only PRs.

If an edit touches a function body, its local names become your problem; rename while you are there. Do not expand scope to other functions just to rename them. The delta between "good naming" and "perfect naming" is never worth a dedicated PR.

### Review checklist entry

Before committing, grep the diff for: single-letter identifiers outside loop ranges, two-letter abbreviations, `tmp`/`res`/`ret`/`buf`/`ptr` (unqualified), `ok`/`valid`/`done`/`flag`, `!not_*` double-negations, non-ASCII in identifiers. Every match is either a canonical exception (loop `d`, hot-path `op`/`args`/`nargs`) or a rename target.

---

## XVIII. Hard Stops (Review Checklist)

Every PR passes all hard stops or is rejected.

**HS1.** Fix root cause, never bump a timeout or workaround. Timeouts mask races.

**HS2.** Clean on `default`, `tsan`, and `release` presets before merge. All three are GCC 16 — no other compiler is supported (§I).

**HS3.** No unwanted files — no .md summaries, no helper/utils. New files only when structurally necessary.

**HS4.** No destructive git — no `checkout`/`reset`/`clean`/`stash` without explicit permission. No `--no-verify`. Atomic commits.

**HS5.** Measure, don't guess. Run 10+ iterations, worst observed is the bound. Variance > 5% = throttling = results invalid.

**HS6.** No `sed`/`awk`. Edit tool only. Every change visible in diff.

**HS7.** All 8 axioms checked on every struct, every function, every edit. Not just the axiom being worked on.

**HS8.** Zero `-Werror=` violations. Warnings-as-errors are the minimum bar.

**HS9.** No vendor libraries. No cuBLAS, no cuDNN, no NCCL, no libcuda. Kernel driver ioctls only (see MIMIC.md §36).

**HS10.** No allocation on hot path. Arena or preallocation. `new`/`delete` in hot code = rejected.

**HS11.** Determinism preserved. Same inputs → same outputs, bit-identical under BITEXACT recipe. CI test `bit_exact_replay_invariant` must pass.

**HS12.** Permission discipline on every new concurrent endpoint. New producer-consumer pairs use `Permission<Tag>` (or `SharedPermission` + Pool for SWMR). New thread-spawn sites either use `permission_fork` (structured) or document why a raw `std::jthread` move is appropriate. Bypassing the discipline is questioned on review.

**HS13.** No regression at the chosen parallelism factor. If `AdaptiveScheduler` or any new threading code chooses parallel(N), the SEPLOG-E1 bench harness must show ≤5% regression vs sequential at that workload's footprint tier. Cache-resident workloads stay sequential by default; DRAM-bound workloads parallelize.

**HS14.** Every new mint factory ships with at least 2 negative-compile fixtures. Per the Universal Mint Pattern (§XXI) discipline, a `mint_X(ctx, args...)` factory's `requires` clause is the single load-bearing soundness gate — and a soundness gate without a witness that it FIRES is just a comment. Each fixture lives in `test/effects_neg/` (or `test/safety_neg/`) and demonstrates a distinct mismatch class (unfit ctx residency, non-bridgeable direction, malformed parameter, etc.). The Tier 1 audit rounds set the bar at 29 fixtures across 7 headers; Tier 2 keystones must match the discipline.

---

## XIX. When to Update This Guide

Update rules here when:

1. A new UB class is discovered in production or CI → add to footgun catalog.
2. A new GCC/libstdc++ feature lands that measurably helps an axiom → add to opt-in.
3. A measurement invalidates a "perf wisdom" here → replace with the measured version.
4. A rule is consistently violated without consequence → investigate whether the rule is still necessary.
5. A new Permission tag tree, splits_into specialization, or PermissionedFoo primitive lands → add a row to §IX or §XVI cataloging it.
6. The cache-tier rule (§IX) is invalidated by a new microarchitecture → re-measure with SEPLOG-E1 bench harness, update the table.

Every change to this guide is a semi-major commit with rationale. This guide is the contract between engineer and codebase.

---

## XX. The Cost Hierarchy

When in doubt, the cost of failure ordering is:

```
Correctness  >  Determinism  >  Security  >  Latency  >  Throughput  >  Code size
```

Never trade correctness for latency. Never trade determinism for throughput. Security is non-negotiable (replay determinism IS a security property). Latency matters because of the per-op recording cost on the hot path — but only after the first three.

### Concurrency cost ordering

A separate ordering applies inside the concurrency layer (§IX). When deciding "should this be parallel?":

```
Sequential-correctness  >  No-regression-vs-sequential  >  Type-system safety (Permission)  >
    Cache-tier appropriateness (CostModel)  >  NUMA locality  >  Maximum throughput
```

A parallel design that regresses small workloads is worse than no parallelism at all. A parallel design that races (no Permission discipline, raw threads, shared mutables) is worse than a sequential design that's slow. The cost-model heuristic is in service of the no-regression rule, not the maximum-throughput rule. **"Just right" beats "as much as possible"** — measured every time.

---

## XXI. The Universal Mint Pattern

**Every cross-tier composition factory in Crucible follows ONE shape:**

```cpp
template <ParametricArgs..., eff::IsExecCtx Ctx, RuntimeArgs...>
    requires CtxFitsX<X<...>, Ctx>
[[nodiscard]] constexpr auto mint_X(Ctx const&, RuntimeArgs...) noexcept -> X<...>;
```

The `mint_*` prefix is load-bearing. It marks every site where the type system verifies a CROSS-TIER FIT and synthesizes a fresh authoritative instance of `X`. After mint, the value is trusted; subsequent operations run at full speed with no further check.

### Two flavors of mint

The convention has TWO modes, distinguished by whether the mint threads ctx-driven policy:

- **Token mint** — synthesizes a fresh authoritative token whose authority derives from a parent token (or root authority). NO Ctx parameter; no `CtxFitsX` gate. Examples: `mint_permission_root<Tag>()`, `mint_permission_split<L,R>(parent)`, `mint_cap<E>(source)`, `mint_session_handle<Proto>(res)`.
  - Note that `mint_permission_split` and `mint_permission_combine` consume a parent token and produce fresh children/parent — the children/parent are authoritative tokens that didn't exist before the call. Mint applies even though the operation is shape-preserving decomposition/composition.

- **Ctx-bound mint** — threads ctx-driven policy through the constructed type. Ctx is the FIRST parameter; the requires-clause is a single `CtxFitsX<X, Ctx>` concept. Examples: `mint_from_ctx<E>(ctx)`, `mint_session<Proto>(ctx, res)`, `mint_permissioned_session<Proto>(ctx, res, perms...)`, `mint_substrate_session<...>(ctx, handle)`, `mint_endpoint<...>(ctx, handle)`.

### The canonical mints (status legend: ✅ shipped • 🚧 planned • 🔮 future tier)

| Status | Layer | Mint | Concept gate | Returns |
|---|---|---|---|---|
| ✅ | Permission token | `mint_permission_root<Tag>()` | (none — root authority) | `Permission<Tag>` |
| ✅ | Permission token | `mint_permission_split<L, R>(parent)` | `splits_into<P, L, R>` | `pair<Permission<L>, Permission<R>>` |
| ✅ | Permission token | `mint_permission_combine<P>(l, r)` | `splits_into<P, L, R>` | `Permission<P>` |
| ✅ | Permission token | `mint_permission_split_n<...>(parent)` | `splits_into_pack<...>` | `tuple<Permission<...>...>` |
| ✅ | Permission token | `mint_permission_combine_n<P>(...)` | `splits_into_pack<...>` | `Permission<P>` |
| ✅ | Permission token | `mint_permission_share<Tag>(p, pool)` | (none — fractional from pool) | `SharedPermission<Tag>` |
| ✅ | Permission token | `mint_permission_fork<Children...>(parent, callables...)` | `splits_into_pack<...>` | `Permission<parent>` (after join) |
| ✅ | Capability token | `mint_cap<E>(source)` | `CanMintCap<E, S>` | `Capability<E, S>` |
| ✅ | Ctx-bound | `mint_from_ctx<E>(ctx)` | `CtxCanMint<Ctx, E>` | `Capability<E, ctx_cap_t<Ctx>>` |
| ✅ | Session token | `mint_session_handle<Proto>(res)` | `is_well_formed_v<Proto> ∧ SessionResource<Res>` | `SessionHandle<Proto, Res>` |
| ✅ | Session token | `mint_channel<Proto>(rA, rB)` | duality + well-formedness | `pair<SessionHandle<Proto,A>, SessionHandle<dual<Proto>,B>>` |
| ✅ | Session token | `mint_permissioned_session<Proto>(res, perms...)` | `is_well_formed_v<Proto>` | `PermissionedSessionHandle<Proto, PS, Res>` |
| ✅ | Session token | `mint_producer_session<Channel>(handle)` | substrate-shape | `PSH<Loop<Send<T,Continue>>, ...>` |
| ✅ | Session token | `mint_consumer_session<Channel>(handle)` | substrate-shape | `PSH<Loop<Recv<T,Continue>>, ...>` |
| ✅ | Session token | `mint_chaselev_owner<Deque>(deque, owner_perm)` | substrate-shape | `Deque::OwnerHandle` |
| ✅ | Session token | `mint_chaselev_thief<Deque>(deque[, proof])` | substrate-shape / fractional proof | `optional<Deque::ThiefHandle>` |
| ✅ | Session token | `mint_owner_session<Deque>(owner)` | substrate-shape | `PSH<Loop<Select<Send<T,Continue>,Recv<T,Continue>>>, ...>` |
| ✅ | Session token | `mint_thief_session<Deque>(thief)` | substrate-shape | `PSH<Loop<Recv<Borrowed<T,ThiefTag>,Continue>>, ...>` |
| ✅ | Ctx-bound | `mint_session<Proto>(ctx, res)` | `CtxFitsPermissionedProtocol<Proto, Ctx, EmptyPermSet>` | `PermissionedSessionHandle<Proto, EmptyPermSet, Res>` |
| ✅ | Ctx-bound | `mint_permissioned_session<Proto>(ctx, res, perms...)` | `CtxFitsPermissionedProtocol<Proto, Ctx, PermSet<Perms...>>` | `PermissionedSessionHandle<Proto, PermSet<Perms...>, Res>` |
| ✅ | Ctx-bound | `mint_substrate_session<Substr, Dir>(ctx, handle)` | `IsBridgeableDirection<Substr, Dir> ∧ SubstrateFitsCtxResidency<Substr, Ctx>` | `PSH<default_proto_for_t<...>, ...>` |
| ✅ | Ctx-bound (Tier 2) | `mint_endpoint<Substr, Dir>(ctx, handle)` | `IsBridgeableDirection<Substr, Dir> ∧ SubstrateFitsCtxResidency<Substr, Ctx>` | `Endpoint<Substr, Dir, Ctx>` |
| ✅ | Bridge wrap | `mint_recording_session(handle, log, self, peer)` | `IsSessionHandle<H>` | `RecordingSessionHandle<Proto, R, L>` |
| ✅ | Bridge wrap | `mint_crash_watched_session<PeerTag>(handle, flag)` | parameter-type gate (SessionHandle specialisation); PeerTag non-deducible | `CrashWatchedHandle<Proto, R, PeerTag, LoopCtx>` |
| ✅ | Tier 3 | `mint_stage<auto FnPtr>(ctx, in, out)` | shape/ctx gate plus `EffectRowMismatch` assertions equivalent to `CtxFitsStage<FnPtr, Ctx>` (≡ `PipelineStage<FnPtr> ∧ IsExecCtx<Ctx> ∧ Subrow<payload_row_t<input_value_type>, Ctx::row_type> ∧ Subrow<payload_row_t<output_value_type>, Ctx::row_type>`) | `Stage<FnPtr, Ctx>` |
| ✅ | Tier 3 | `mint_pipeline(ctx, stages...)` | chain/ctx gate plus `EffectRowMismatch` assertion equivalent to `CtxFitsPipeline<Ctx, Stages...>` (≡ `IsExecCtx<Ctx> ∧ pipeline_chain<Stages...> ∧ Subrow<pipeline_row_union_t<Stages...>, Ctx::row_type>`; chain folds `stages_chain<S_i, S_{i+1}>` over adjacent pairs) | `Pipeline<Stages...>` |
| ✅ | Tier 2→3 bridge | `mint_stage_from_endpoints<auto FnPtr>(ctx, in_ep, out_ep)` | `CtxFitsStageFromEndpoints<FnPtr, Ctx, ConsumerEp, ProducerEp>` (≡ `PipelineStage<FnPtr> ∧ IsExecCtx<Ctx> ∧ IsConsumerEndpoint<ConsumerEp> ∧ IsProducerEndpoint<ProducerEp> ∧ StageHandlesMatchEndpoints<FnPtr, ConsumerEp, ProducerEp>`); consumes endpoints via `into_handle()`, threads through `mint_stage` | `Stage<FnPtr, Ctx>` |
| 🔮 | Tier 4 | `mint_vigil<L, D, C>(ctx, parts...)` | per-component fit | `Vigil<L, D, C>` |
| 🔮 | Tier 5 | `mint_keeper<Vigils...>(ctx, vigils, topo)` | per-Vigil fit | `Keeper<Vigils..., ...>` |
| 🔮 | Tier 6 | `mint_canopy<Keepers...>(ctx, keepers, mesh)` | per-Keeper fit | `Canopy<Keepers..., ...>` |

### Why the pattern is load-bearing

1. **Single grep target.** `grep "mint_"` finds every authorization point in the codebase. Every cross-tier composition is discoverable in O(1) review time.
2. **Construction-time validation.** The `requires` clause is the type-level proof that runs ONCE at the boundary. Subsequent ops on the returned X are concept-free — full speed, no per-call check.
3. **Uniform shape across tiers.** A production engineer who learns one mint learns them all. A maintainer who adds Tier 7 follows the template. New contributors recognize the pattern instantly.
4. **No naming drift.** `make_*` / `establish_*` / `create_*` / `build_*` are NOT minters and MUST NOT be used for cross-tier composition. They allocate or initialize bare structures; they don't carry the type-level fit-check semantics.

### Discipline rules

- **Every cross-tier composition factory MUST be named `mint_<noun>`.** No exceptions.
- **For ctx-bound mints, the first parameter MUST be `Ctx const&`.** Token mints (which derive authority from a parent token rather than from a ctx) take the parent/source as the first parameter.
- **The `requires` clause MUST be a single concept** (`CtxFitsX<X, Ctx>` for ctx-bound mints, or the equivalent token-validity concept for token mints). Multi-clause requires-lists belong INSIDE the concept definition, not at the call site.
- **Every mint MUST be `[[nodiscard]] constexpr noexcept`** unless the factory genuinely allocates (in which case it is `[[nodiscard]] noexcept` only — `constexpr` would lie about the runtime cost).
- **Returned types are CONCRETE, not type-erased.** `mint_endpoint<...>(ctx, h)` returns `Endpoint<Substr, Dir, Ctx>`, not `auto`-erased-into-virtual. Concept-overloaded specialization downstream depends on the concrete type.
- **Diagnostics route through `safety::diag::Category`.** A mint that fails its `requires` clause emits a category-tagged diagnostic so user-facing errors stay readable.
- **Internal helpers do NOT use the `mint_` prefix.** The convention marks USER-FACING authorization points; internal detail-namespace helpers carry the trailing-underscore convention (e.g., `permission_fork_spawn_`, `permission_fork_rebuild_`) so `grep "mint_"` returns only the public surface.
- **Session ctx-bound mints use the permissioned family.** `mint_session<Proto>(ctx, res)` is the empty-`PermSet` shim; `mint_permissioned_session<Proto>(ctx, res, perms...)` is the non-empty `PermSet` form. Both route through the same ctx row gate and local permission-flow closure gate.
- **Every new mint factory MUST ship at least 2 negative-compile fixtures** demonstrating the `requires` clause fires on each kind of mismatch. See HS14.

### Anti-pattern: the runtime registry

Do NOT replace mint with a runtime registry / factory function table / virtual dispatch. The mint pattern is **compile-time-resolved** — every call site has the full type information visible to the optimizer. A runtime registry would defeat EBO collapse, branch prediction, inlining, and the whole zero-runtime-cost claim of the substrate.

---

ZERO COPY. ZERO ALLOC ON HOT PATH. EVERY INSTRUCTION JUSTIFIED.
L1d = 48 KB. L2 = 2 MB. Squat upfront, point into it, write into it.

IF VARIANCE > 5% IN BENCHES — IGNORE RESULTS, THE LAPTOP IS THROTTLING.

IF A RULE HERE CONFLICTS WITH REALITY — MEASURE, FIX REALITY, OR FIX THE RULE.
NEVER WRITE CODE THAT CONTRADICTS BOTH.

PERMISSIONS PROVE WHAT'S SAFE. THE COST MODEL DECIDES WHAT'S PROFITABLE.
NEITHER ALONE IS ENOUGH. BOTH TOGETHER IS THE WHOLE GAME.
