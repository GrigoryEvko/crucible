# The Crucible Runtime
*Adaptive infrastructure for HPC and ML programs.*

Three layers: **Hardware** — compute nodes, heterogeneous and replaceable. **The Model** — weights and computation graphs. **Crucible** — the runtime that abstracts hardware, persists state across node failures, and migrates to new devices.

**Scope.** Crucible is for general scientific and commercial high-performance computing (HPC) programs, and also for machine learning (ML) programs. The PyTorch Vessel is the first front-end adapter, and at this time it is the only one. More front-end adapters will follow. Most layer sections of this file describe the ML path, because that path came first.

Python describes. Crucible executes. The 492,000 lines of framework overhead between them become unnecessary. There is no training or inference — there is only a model in Crucible.

## Ontology

| Name | Role | Description |
|------|------|-------------|
| **Safety** | Foundation | The structural guarantee layer: 8 axioms (Init/Type/Null/Mem/Borrow/Thread/Leak/DetSafe), contracts (P2900), reflection (P2996), the wrappers of `include/fixy/` and `include/foundation/permissions/` (Linear, Refined, Tagged, Secret, Permission, SessionHandle, ScopedView, Machine, Monotonic, AppendOnly, WriteOnceNonNull, and the `fixy::ct` constant-time primitives), session-type stack, CSL permissions. Every higher layer inherits correctness from these primitives. |
| **Relay** | Body | Compute node inhabited by a Crucible daemon. Mortal. Replaceable. |
| **Keeper** | Spirit | Per-Relay daemon — self-healing, self-updating, autonomous. `crucible-keeper.service` starts at boot, discovers peers, joins mesh. Executes the runtime observer's advice. |
| **Vigil** | Intellect | The model: DAG, weights, learned knowledge. Named for the Prothean AI. Never sleeps. |
| **Cipher** | Soul | Persistent state — DAG chain, weight snapshots, KernelCache (three-level: L1 IR002 vendor-neutral / L2 IR003\* per-vendor-family / L3 compiled bytes per-chip), RNG state, MAP-Elites archives, calibration data, recipe registry snapshots. Event-sourced. Survives death, reincarnates on new hardware. |
| **Canopy** | Collective | Mesh of Keepers — distributed awareness, gossip, consensus, self-healing. No master node. |
| **Vessel** | Interface | PyTorch — the 2,000+ ATen operators Crucible intercepts via the Dispatcher. |
| **Meridian** | Map | Startup calibration. Measured hardware truth. Discrete-search joint 5D partition optimization (topology, parallelism, communication, placement) over calibrated `CollectiveBenchmarks` + `TopologyMatrix`, driven by `mimic::fast_cost`. Re-solves on topology change or under runtime-detected congestion drift. No external SMT dependency — Crucible ships no Z3, no CVC, no proprietary solver; the partition optimizer is a bounded-depth branch-and-bound over the cost-model surface. |
| **Observe** | Senses | Runtime observation surface: metrics, health observations, synthetic probes, histograms, passive BPF sample streams. It records facts; it does not enforce policy. |
| **Warden** | Enforcer | Bounds-keeping runtime surface: deadline watchdog, hardening policy, hot-region registry, quarantine, socket-lifecycle enforcement hooks, CPU/NUMA selection. It acts on facts owned by Observe, Topology, CNT-P, Cog, and Perf. |
| **Crucible** | Whole | The organism. Everything together. |

---

## L0 — Safety Foundation

**The structural guarantee layer. Every higher layer inherits correctness from these primitives.**

Crucible has no proof-assistant source of truth. Correctness is won by three complementary disciplines: **contracts-enforced invariants** (P2900R14), **linear / refined / session-typed wrappers** over every resource, and **measurement** (Mimic MAP-Elites + calibrated simulators + cross-vendor CI — §L2, §L15). The first two disciplines operate at this time. The measurement discipline is planned, because no compute backend exists. No SMT-proven optimal kernels; no proved-allocators-theorem. What we have instead:

**Eight safety axioms.** InitSafe, TypeSafe, NullSafe, MemSafe, BorrowSafe, ThreadSafe, LeakSafe, DetSafe. Every struct, every function, every edit audits all eight. Contracts (`CRUCIBLE_PRE`/`CRUCIBLE_POST`/`contract_assert` in the function body, §XII), erroneous behavior for uninit reads (P2795R5), reflection-driven hashing (P2996), strong IDs, `std::bit_cast`, saturation arithmetic. Detail catalog in §II of the Code Guide below.

**Safety wrappers.** The value-level wrappers are in `include/fixy/`, in namespace `fixy`. One algebraic substrate unifies them: `Graded<Modality, Lattice, T>` in `include/foundation/algebra/Graded.h`, in namespace `foundation::algebra`. The design is in `misc/25_04_2026.md` §2. Every Graded-backed wrapper exposes a uniform diagnostic surface (`graded_type`, `lattice_type`, `value_type`, `modality`, `value_type_name()`, `lattice_name()`), and most wrappers get it from `fixy::graded_facade` in `include/fixy/GradedFacade.h`. The `GradedWrapper` concept in `include/foundation/algebra/GradedTrait.h` enforces the contract structurally. Adversarial cheat-detection harness at `test/fixy/test_cheat_probe.cpp` (56 cheats, 2 of them admitted and documented).

**Wrapper → substrate map** (each wrapper names its substrate in its `graded_type` member):

| Wrapper | Substrate | Storage regime |
|---|---|---|
| `Linear<T>` (`fixy/Qtt.h`) | `Graded<Absolute, QttSemiring::At<QttGrade::One>, T>` | grade in the type (zero-cost EBO) |
| `Refined<Pred, T>` (`fixy/Refined.h`) | `Graded<Absolute, BoolLattice<predicate_t<Pred>>, T>` | grade in the type |
| `SealedRefined<Pred, T>` | same as Refined; minus `into()` (forces re-construct on mutate) | grade in the type |
| `Tagged<T, Tag>` (`fixy/Tagged.h`) | `Graded<RelativeMonad, TrustLattice<Tag>, T>` | grade in the type |
| `Secret<T>` (`fixy/Secret.h`) | `Graded<Comonad, ConfLattice::At<Conf::Secret>, T>` | grade in the type |
| `Monotonic<T, Cmp>` (`fixy/Mutation.h`) | `Graded<Absolute, MonotoneLattice<T, Cmp>, T>` | grade is the value |
| `AppendOnly<T, Storage>` (`fixy/Mutation.h`) | `Graded<Absolute, SeqPrefixLattice<T>, Storage<T>>` | grade derived from the container |
| `Stale<T>` (`fixy/Stale.h`) | `Graded<Absolute, StalenessSemiring, T>` | grade stored per instance |
| `SharedPermission<Tag, Brand>` (`foundation/permissions/Permission.h`) | `Graded<Absolute, DualLattice<FractionalLattice>, Tag>` (façade — atomic state in `SharedPermissionPool`) | none: the token is empty, and the pool holds the share count |

The doc-block of `include/foundation/algebra/Graded.h` gives four storage regimes:
- The grade lives in the type and collapses under `[[no_unique_address]]`
- The grade is the value (`L::element_type` is `T`)
- The grade is derived on read (`L::grade_of`)
- The grade is stored per instance.

The first three cost `sizeof(T)`. `test/fixy/test_wrapper_verification.cpp` checks the layout of each wrapper against its regime.

**Structural wrappers — deliberately not graded.** These wrappers follow non-graded disciplines (RAII, typestate, structural constraint) that do not fit the `Graded<M, L, T>` shape:
- `Permission<Tag, Brand>` + `mint_permission_fork` (`foundation/permissions/Permission.h`, `foundation/permissions/PermissionFork.h`) — CSL frame-rule linear tokens (misc/THREADING.md).
- `SessionHandle<Proto, Resource, LoopCtx, Policy, PS>` (`fixy/session/Handle.h`) — type-state binary and MPST session types in `include/fixy/session/`, namespace `fixy::session` (Honda 1998 / HYC 2008 / Gay-Hole 2005 / BSYZ22 crash-stop). The last parameter threads a CSL `PermSet<Tags...>` through the protocol position. `Send<Transferable<T, X>, K>` consumes `Permission<X>` from the set, and `Recv<Transferable<T, X>, K>` produces it. A `Continue` must see the permission set of its loop entry. A handle at `End` closes only when the set holds no open loan. `mint_permissioned_session` mints a handle with a non-empty set.
- `mint_substrate_session<Substrate, Direction>(ctx, handle)` and `mint_endpoint<Substrate, Direction>(ctx, handle)` (`fixy/concurrent/SubstrateSessionBridge.h`, `fixy/concurrent/Endpoint.h`) — the typed-session door over a handle of `PermissionedSpscChannel` or `PermissionedMpscChannel`. The session has the empty permission set.
- `PermissionedMetaLog<UserTag, Brand>` + `metalog_session::{mint_metalog_producer_session,mint_metalog_consumer_session}` (`include/crucible/PermissionedMetaLog.h`, `include/crucible/MetaLogSession.h`) — role-typed foreground append / background drain façade over the production `MetaLog` TensorMeta side-channel. Each session owns its channel handle and has the empty permission set.
- `ScopedView<Carrier, Tag, Brand>` (`fixy/ScopedView.h`) — lifetime-bounded borrow for non-consuming inspection.
- `Machine<State, Edges>` (`fixy/Machine.h`) — type-indexed state machines. An illegal transition is a compile error.
- `OwnedRegion<T, Tag, Brand>` (`fixy/OwnedRegion.h`) — a pointer and a count over one buffer, with the `Permission` that proves exclusive ownership of those bytes.
- `Pinned<T>` (`foundation/Pinned.h`) — address-stability marker (CRTP base).
- `fixy/Checked.h` — `checked_*`, `wrapping_*`, `trapping_*` and `saturating_*` overflow primitives.
- `fixy/ConstantTime.h` — `fixy::ct::select`, `eq` and `less`, branch-free primitives for crypto paths and Cipher key handling.
- `foundation/Simd.h` — SIMD primitives (§VIII).

Plus `WriteOnce<T>` / `WriteOnceNonNull<Ptr>` / `BoundedMonotonic<T, Max, Cmp>` / `OrderedAppendOnly<T, KeyFn, Cmp, Storage>` / `AtomicMonotonic<T, Cmp>` in `fixy/Mutation.h`. No lattice grades them. Each one declares an identity that `foundation/diag/RowHash.h` folds, and the `mint_*` function beside each class is its one door.

**Verification harness.** `test/fixy/test_wrapper_verification.cpp` is a single TU that asserts the cross-wrapper properties of the fixy wrappers: the diagnostic surface, sizeof and triviality per storage regime, cross-composition, and distinct nesting orders. `test/fixy/test_cheat_probe.cpp` runs 56 adversarial cheats against the concept. The build succeeds only when the concept rejects each cheat, or when the file documents the cheat as admitted.

**Soundness via measurement, not proof.** In the design, numerical correctness lives in the cross-vendor CI matrix (MIMIC.md §41): every IR002 kernel × recipe × backend runs on real silicon, outputs are compared pairwise against a CPU scalar-FMA oracle, tolerance enforced per the recipe's declared `ReductionDeterminism` tier (UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT). A backend that violates tolerance will fail the build. The `BITEXACT_TC` tier has a hardware limit, and §L2 states it: FP16 and BF16 tensor-core fragments are not bit-identical across vendors. No compute backend exists at this time, so the matrix does not run.

**No external SMT dependency.** Crucible ships no Z3, no CVC5, no third-party SMT solver. At this time, the `verify` CMake preset builds Release code with every contract family armed. An internal small-SMT solver for that preset is deferred. It will discharge residual integer / Presburger obligations only — bounds, divisibility, modular arithmetic, the same narrow scope TVM Analyzer (PR #1367) uses. Its planned budget is 5 ms per query, not on the hot path. Out of scope (and never planned): kernel-optimality proofs, floating-point reasoning, cost-model decidability. Those are measurement problems for the planned cross-vendor CI harness (MIMIC.md §41), not theorem proving.

**Capability tags.** `include/foundation/effects/Effect.h`, in namespace `foundation::effects`, defines three groups. The first is the `Effect` enum of six atoms (Alloc, IO, Block, Bg, Init, Test). The second is the value-level tags `cap::Alloc`, `cap::IO` and `cap::Block`, which the namespace re-exports as `Alloc`, `IO` and `Block`. The third is the context classes `Bg`, `Init` and `Test`. A context holds its value atoms as empty `[[no_unique_address]]` fields (`bg.alloc`, `bg.io`, `bg.block`), so the tags cost nothing at run time.

Only `mint_context<Ctx>(key)` builds a context, and only the owner of a key can build the key. `utils/scripts/ctx-init-door-allowlist.txt` and `utils/scripts/ctx-bg-door-allowlist.txt` list the entry points that use the two production doors. The tags and the contexts are NOT F\*X proof obligations. They are C++-level capabilities enforced at compile time. Production call sites spell them `::foundation::effects::*`.

**Met(X) effect rows.** `include/foundation/effects/Row.h` gives `Row<Es...>`, the `Subrow<R1, R2>` concept, and `row_union_t / row_difference_t / row_intersection_t`. `include/foundation/effects/Computation.h` gives the `Computation<Row, T>` carrier with `mint_computation / mint_computation_in_ctx / extract / weaken / map / then` per Tang-Lindley POPL 2026 / `misc/25_04_2026.md` §3.2. `include/foundation/effects/Ctx.h` gives `ExecCtx<Cap, Row>` and the gates `CtxAdmits` and `CtxOwnsCapability`. The production hot paths take a value-level tag parameter (for example `::foundation::effects::Alloc a`), and they do not return a `Computation<Row<...>, T>`. Tests: `test/foundation/test_effects_core.cpp`, `test/foundation/test_capability.cpp`, `test/foundation/test_computation.cpp`, `test/foundation/test_ctx.cpp`, and the `neg_computation_*` fixtures in `test/foundation/neg/`.

---

## L1 — Hardware

**Compute hardware. Heterogeneous, replaceable.**

This section is the design. No code reads NVML, sends health data to a Keeper or runs Mimic at this time. The Keeper is planned (Phase 5), and the Mimic backends are planned (MIMIC.md).

GPUs are ecosystems: tensor cores (1000 TFLOPS FP16 on H100), scalar ALUs (60 TFLOPS), four-level memory hierarchy (registers → shared memory → L2 → HBM), power envelopes. Gap between theoretical peak and achieved: 40-70%.

**Multi-vendor:** NVIDIA (sm_86/89/90/100), AMD (gfx1100/942), Intel XMX, Apple AMX, Google TPU MXU. Same computation described once in the Merkle DAG; different Mimic-compiled native-ISA kernels per (content_hash, device_capability). See MIMIC.md for per-vendor backends.

**Power management:** NVML exposes clocks, power, temperature, ECC errors. Memory-bound phases don't need full core clock — drop 30% for zero perf loss, significant savings.

**Health monitoring → Keeper:** ECC error trends, thermal throttling, clock degradation feed into the Keeper. A failing GPU gets load-reduced, data pre-replicated to healthy Relays (L13 Distribution, RAID). State is already replicated before failure completes. New hardware → fresh Keeper discovers mesh and Cipher → Mimic re-runs MAP-Elites (warm-started from the nearest-family archive in Cipher) for the new device → reshards for new topology → resumes exactly.

---

## L2 — Kernels

**Calibrated-optimal computation. Measured, not proved.**

Current frameworks: static lookup (op + dtype → library kernel). Same kernel for 64×64 and 8192×8192, A100 and 3090, contiguous and transposed. No adaptation.

**Forge + Mimic replace the vendor stack (planned).** In the design, `Forge` (vendor-agnostic optimizer, FORGE.md) lowers the IR001 tensor DAG to the IR002 portable kernel DAG with a pinned `NumericalRecipe`. The same IR002 kernel then gives bit-exact results, or results inside a bound in units in the last place (ULP), on every supported chip. The numerics paragraph of this section states the limit for tensor-core fragments. `Mimic` (per-vendor backend, MIMIC.md) will emit native ISA from IR002, with one backend for each target: Hopper/Blackwell SASS, CDNA3+/RDNA3+ AMDGPU, a TPU executable, NEFF for Trainium, and a CPU reference oracle. No Forge phase and no compute backend exist at this time. `include/crucible/forge/` holds the IR001 operation kinds and the network recipe table. `include/crucible/mimic/` holds `Fence.h`, `Semaphore.h` and `CogMimic.h`. `include/crucible/mimic/_wip/` declares one network backend for each of six vendor families. None of them has an emit path. No vendor libraries: zero cuBLAS, zero cuDNN, zero NCCL, zero libtpu — only kernel-driver ioctls.

**MAP-Elites kernel search** (planned, MIMIC.md) replaces autotuning. Six behavior axes (occupancy, register usage, smem usage, pipeline depth, MMA shape family, warp-group split) × 8 buckets each = ~260K cells, typically 500-5K populated per kernel family. Per-vendor three-tier simulator (fast ~1-5 ms / medium ~10-30 ms / accurate ~100-500 ms) calibrated to 95-98% against real silicon via hardware-counter probes (CUPTI / rocprof / PJRT profiler / neuron-profile). Insight-driven mutations — structured diagnostics (WGMMA_UNDERUTILIZED, REGISTER_PRESSURE_HIGH, L2_QUEUE_SATURATED, ~40 kinds) map to concrete mutation operators. Hybrid mode validates top-K archive cells on real hardware.

**Cross-vendor numerics CI (MIMIC.md §41, planned)** will enforce the portability contract. Every (KernelKind × NumericalRecipe × target) triple compiled, executed, output-compared pairwise against CPU scalar-FMA oracle. `BITEXACT_STRICT` permits a difference of 0 bytes, and `ORDERED` permits the tolerance of its recipe. A backend that violates tolerance will fail the build. The CI does not run at this time, because no compute backend exists.

**The limit of `BITEXACT_TC`.** `include/crucible/NumericalRecipe.h` defines `BITEXACT_TC` as short tensor-core fragments with a pinned outer scalar reduction, and at most one ULP of difference. That bound holds only for exact fragments: integer matrix multiply-accumulate (MMA), FP64 or FP32 MMA, or inputs split so that each product is exact. FP16 and BF16 tensor-core fragments are not bit-identical across vendors or across GPU generations. Their fused width, the truncation of the fused sum and the rounding are different for the same inputs.

Bit-accurate models of ten GPU architectures (MMA-Sim, arXiv 2511.10909) and the FTTN tests (arXiv 2403.00232) show this. In MMA-Sim, only FP64 and FP32 MMA agree on every architecture. The starter recipes `f16_f32accum_tc` and `bf16_f32accum_tc` in `include/crucible/RecipeRegistry.h` have `BITEXACT_TC`, so the hardware does not give the tier that they have.

**KernelCache:** a lock-free open-addressing hash table in `include/crucible/KernelCache.h` that maps (content_hash, row_hash) → CompiledKernel. The row hash is the effect row of the region, so two regions with identical ops and different rows take different slots. Content-addressing: identical ops on identical shapes produce identical hashes. No production code reads or writes the table at this time, because no code compiles a kernel: `CompiledKernel` has a declaration and no definition. The reuse across runs, models and organizations, the variants for each device and the growth across restarts are planned.

**Stream parallelism (planned):** DFG reveals independent ops → launch on different CUDA streams → concurrent SM execution. Schedule compiled statically from topological sort + earliest-start-time assignment. Zero scheduling overhead at runtime.

**Deterministic Philox RNG:** cuRAND is hardware-dependent — different sequences on H100 vs 3090. `include/crucible/Philox.h` gives Philox4x32: counter-based, platform-independent, stateless. `Philox::op_key_det` derives the key of an op from `(master_counter, op_index, content_hash)`. In the planned kernels, each thread computes `philox(thread_idx, op_key)` — ~10 integer instructions in registers. For memory-bound kernels like dropout: runs free in otherwise-wasted ALU cycles. No kernel uses it at this time, because no compute backend exists. Same (counter, key) → same bits on any architecture.

**Kernel fusion (planned):** adjacent ops with single producer-consumer chain fuse into one kernel keeping intermediates in registers/shared memory, eliminating HBM round trips. Decision from DFG topology at compile time.

In the design, KernelCache is part of the **Cipher** — write-once, persists across reincarnations. At this time the Cipher stores regions of the DAG, the head hash and a log of committed steps on a local disk (`include/crucible/Cipher.h`). It holds no kernel.

---

## L3 — Memory

**Where tensors live. 2ns allocation vs 2000ns.**

PyTorch's CUDACachingAllocator: freelist search, splitting, coalescing, mutex contention. 200-2000ns/alloc. For 1000-op models: ~2000 allocs/iter × 500ns = 2ms pure overhead = 13% of a 15ms iteration.

**Crucible: static memory plan** from DFG lifetimes. Background thread computes offline:
- Birth = producer op index; Death = last consumer op index; Size = shape × dtype, aligned 256B
- A sweep line over the birth and death events, which a counting sort puts in order, with first-fit reuse of freed blocks
- Output: `MemoryPlan { slots, pool_bytes, num_slots, num_external, ... }`, where each `TensorSlot` holds `offset_bytes`, `nbytes`, `birth_op` and `death_op`

`PoolAllocator` makes one aligned host allocation of `pool_bytes` when a region becomes active. Every "allocation" = `base_ptr + offset`: no mutex, no fragmentation, no contention. Plan is read-only at runtime. The PyTorch Vessel puts no tensor there at this time, because COMPILED mode still does each operation eagerly (refer to L4). The device pool, one `cudaMalloc(pool_bytes)` for each plan, is planned.

**Deterministic:** same DFG → same plan → same addresses. When compiled kernels use the plan (planned), this removes the history-dependent allocator non-determinism of PyTorch. **Arena allocator** for DAG metadata: bump-pointer, ~2ns/alloc, bulk reset. **Aliased tensors** (views, transposes): one offset for base storage, aliases reference with different (offset, sizes, strides). Zero-cost. **Dynamic shapes:** new plan built by background thread, swapped atomically at iteration boundary.

**Automatic activation checkpointing** (planned) from measured data:
```
For each forward activation needed in backward:
    if store_cost / recompute_cost > threshold: recompute
    else: store
```
Per-tensor, optimal, no manual `torch.utils.checkpoint()`. Threshold adapts to memory pressure.

**Per-Relay planning (planned):** same DFG, different plans per device capacity (H100 80GB vs 3090 24GB vs MI300X 192GB). Memory heterogeneity handled by adapting the plan.

**OOM is structurally impossible (planned, Phase 5).** In the design, the Keeper has the plan BEFORE execution. One check: `plan.pool_bytes ≤ device_memory - reserved`. If it won't fit: adapt plan first (more checkpointing, smaller batch, offload optimizer state), then proceed. `cudaMalloc` never fails. **Predictive adaptation:** track pool_bytes growth across iterations, extrapolate, preemptively adapt before limits approach.

---

## L4 — Operations

**The atomic unit. What happens when Python says `x + y`.**

Every PyTorch op dispatches through the Dispatcher's priority-ordered function pointer table. `DispatchKey::Crucible` intercepts each operation above the backend keys. The patched PyTorch fork adds that key, and `vessel/torch/register.cpp` registers one recording kernel for each of the 3110 ATen operators of the fork.

**RECORD mode** (6 steps). `bench/baselines/record_leaf.json` gives the measured costs of the recording path, without the eager execution of step 3:
1. Snapshot input TensorMeta (shapes, strides, dtype, device, data_ptr). Handle TensorList unpacking. Encode scalars as int64 (up to 5 inline).
2. Compute schema_hash (op name) and shape_hash (input sizes).
3. Execute eagerly via redispatch.
4. Snapshot output TensorMeta.
5. Append to MetaLog (an SPSC buffer of 1M entries at 168 bytes each, about 176 MB).
6. Record to TraceRing (64-byte cache-line-aligned entry).

**COMPILED mode, as built.** The backend still does each operation immediately, in eager mode. The kernel in `vessel/torch/record_kernel.h` redispatches the operation first, and then it gives the trace entry to `Vigil::dispatch_op`. The Vigil compares the schema hash and the shape hash with the next operation of the compiled region, and it moves the replay cursor forward. If the hashes are different, the Vigil tries a cached region that agrees with the operation at that position. If no cached region agrees, the Vigil changes to RECORDING mode and does not record the divergent operation.

**COMPILED mode, as planned** (Phase 4 of the Development Plan, ~2ns total):
1. Advance op index
2. Check guard: `compiled_trace[idx].schema_hash == current?` — if not, DIVERGE
3. Push pre-allocated shadow handles (a shadow TensorImpl with correct metadata, pointing into memory plan)
4. Return. No execution, no allocation.

In the plan, the compiled kernels operate on GPU streams, independently of Python.

**Graduated divergence detection:**
- schema_hash mismatch → hard diverge, immediate eager fallback
- shape_hash mismatch → hard diverge (dynamic shapes changed)
- scope_hash mismatch → soft warning (different module, same ATen op)
- callsite_hash mismatch → softest warning (refactored code, identical behavior)

The replay guard compares only the schema hash and the shape hash. The two warnings on the scope hash and the callsite hash are planned, and no code gives them.

Pre-emptive: prepare the eager path before the guard confirms a divergence. At this time the eager path always runs first, because COMPILED mode redispatches each operation.

**Matrix structure discovery per layer (planned):**
- Full-rank → dense matmul
- Low-rank (r << d) → A(d×r)·B(r×d), 2× cheaper at r=d/4
- Near-Toeplitz → depthwise conv + correction, 10× cheaper
- Sparse (>95%) → cuSPARSE
- Block-diagonal → smaller independent matmuls

In the design, replacements are DAG branches with quality verification.

**Communication ops** (all_reduce, all_gather, etc.) that go through the Dispatcher outside the aten namespace reach the boxed fallback of `vessel/torch/crucible_fallback.cpp`. The fallback records them on the same path as the other ops. In the design, the recording pipeline is **event sourcing**: the Cipher persists the event log for deterministic replay and reincarnation. At this time `Vigil::persist` stores the active region in the Cipher and moves its head, and `Vigil::load` activates the head region again.

No training/inference distinction at L4 — same fallback, same recording, same compiled execution.

---

## L5 — Tensors

**Metadata, shadow handles, and the latent space.**

**Shadow handle (planned, Phase 4).** No shadow handle exists at this time, and COMPILED mode gives real tensors (refer to L4). The plan: a real PyTorch tensor with correct metadata (shape, strides, dtype, device) but storage points into pre-planned memory pool. Data written asynchronously by compiled kernels. Python holds the shadow, inspects metadata, passes to next op (which returns another shadow in COMPILED mode). Not a future — a full TensorImpl with `DispatchKey::Crucible`.

**Sync points** (in the plan, the only moments Python blocks): `.item()`, `.cpu()`, `.numpy()`, `print()`, conditionals on values, unrecognized ops. Everything else is shadow. 1000 ops with 1 `loss.item()`: 999 shadow returns (~2μs) + 1 sync (~10μs). Python wall time: ~12μs vs ~15ms eager.

**TensorMeta:** 168 bytes/tensor (`include/crucible/TensorMeta.h`) — sizes[8], strides[8], data_ptr, ndim, dtype, device_type, device_idx, layout, requires_grad, flags, output_nr, storage_offset, version, storage_nbytes, grad_fn_hash. Lives in MetaLog parallel to TraceRing. In the plan, sparse tensor shadows (COO, CSR/CSC/BSR/BSC) extend the same pattern.

**Latent space is observable** (planned; during recording, actual data is available). No code does these analyses at this time:
- **Intrinsic dimensionality:** PCA on activations reveals effective rank per layer. A 4096-dim state might use only 600 dims → 3496 wasted.
- **Dead dimensions:** per-dimension variance < ε → carries zero information → maskable (20% savings if 847/4096 dead).
- **Representation collapse:** CKA ≈ 1.0 between adjacent layers → redundancy → prune or add auxiliary loss.
- **Representation steering:** direction vectors (mean truthful - mean hallucinated activations) added at inference for behavior control. One vector addition per layer, negligible cost.
- **Manifold Mixup:** interpolate hidden states between samples at intermediate layers → new training signal from latent geometry.
- **Tensor provenance:** complete causal ancestry through DFG — trace any output back to root cause.

In the plan, shadow handles are mode-agnostic: training and inference produce identical objects.

---

## L6 — Graphs

**The skeleton. Dataflow, aliases, edges, and cycles.**

**TraceGraph:** bidirectional CSR property graph from one iteration. Nodes = ops, edges = relationships:
- **DATA_FLOW:** data_ptr tracking via PtrMap (open-addressing, at least 4096 slots, a reused heap buffer that grows). Output ptr matches input ptr → producer-consumer edge.
- **ALIAS:** same data_ptr from different ops → view/in-place → shared storage.

Each node carries: schema/shape/scope/callsite hashes, TensorMeta arrays, scalar args, grad/inference flags. Built in single pass, O(V+E) via counting sort. ~50-100μs for 1000 ops.

**IterationDetector:** a signature of K=5 keys, where each key mixes the schema hash and the shape hash of one op. A signature match proposes a period P. The detector accepts P only when the P ops before the match equal the P ops before those. Handles warmup.

**LoopNodes for cyclic computation:** wraps acyclic body with feedback edges + termination (Repeat(N) | Until(ε)). `LoopNode` and `make_loop` are in `include/crucible/MerkleDag.h`. No production code builds a LoopNode at this time, so the uses that follow are planned:
- **Compiled recurrence:** RNN as one body × 1000 reps, no Python per timestep
- **Convergence execution:** DEQ fixed-points, diffusion denoising — stop when converged
- **Cross-iteration pipelining:** overlap N+1's forward with N's optimizer via double-buffering
- **Nested loops:** DiLoCo inner/outer as nested LoopNodes, independently compilable
- **Self-referential:** Crucible's own autotuning loop as a LoopNode

Graph's fixed execution order is a pillar of deterministic replay.

---

## L7 — The Merkle DAG

**Content-addressable, versioned computation graph.**

Central data structure. L3-L6 feed in at this time. In the design, L1 and L2 also feed in, and L8-L16 read and modify the DAG. L0 does not prove correctness: its contracts and safety wrappers are the discipline that the DAG code obeys. At the same time, the DAG is the computation specification, the compilation cache key, the guard system and the versioning mechanism. Its use as the deployment artifact is planned (Phase 5).

**RegionNodes:** compilable op sequences. **content_hash** = hash(schema_hashes, input shapes/strides/dtypes/devices, scalar values). Identical computation → identical hash, even across models. **merkle_hash** = content_hash + child hashes → O(1) equality for entire subtrees (like git commits).

**BranchNodes:** dynamic behavior. Guard = the op sequence itself. Mismatch at op N → branch arms for different paths. Both arms independently compilable. Shared suffixes share content_hashes and kernels. `BranchNode` is in `include/crucible/MerkleDag.h`, and `add_branch` is in `include/crucible/KernelCache.h`. No production code calls `add_branch` at this time. At a divergence, the Vigil looks for another region in the region cache (`include/crucible/RegionCache.h`).

In the design, BranchNodes are THE mechanism for everything that changes: architecture mutation (L10), attention replacement (L9), hyperparameter changes (L11), continuous learning (L14). Every adaptation is a branch. Every branch is versioned and rollbackable.

**KernelCache:** (content_hash, row_hash) → CompiledKernel, with lock-free reads (refer to L2). The persistence across runs, models and organizations is planned. In the design it is the **computation genome**, and every run enriches it.

**Atomic swaps:** background thread builds new DAG structures → one atomic pointer swap at iteration boundary → zero-downtime activation. At this time the mechanism activates a new region with its memory plan. `Vigil::rollback` uses the same mechanism. The branch swaps, the topology changes and the coordination across Canopy at one iteration boundary are planned.

**The DAG IS the Vigil.** No torch.export(), ONNX, TorchScript. Same DAG trains and serves. A deploy that copies the Cipher to a Relay is planned (Phase 5).

**The DAG IS the audit trail.** Root merkle_hash captures entire computation state. The walk that finds a divergence in O(log N) is planned. The merkle hash mixes with fmix64, which is not a cryptographic hash, so a cryptographic provenance for regulatory compliance is planned too.

**Git operations on models (planned):** diff (which regions changed), merge (non-overlapping clean, overlapping = conflict), bisect (binary search through versions for regression), cherry-pick (select specific region updates), blame (trace value through version history).

**LoopNodes in the DAG:** cycle semantics within acyclic hash framework. `merkle_hash = hash(body.content_hash ⊕ "loop" ⊕ feedback_signature ⊕ termination)`. Transforms DAG from computation snapshot to computation PROGRAM. No production code builds a LoopNode at this time (refer to L6), so a training run as one compact cyclic graph is planned.

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

## L15 — Meridian + Observe / Warden

**Operational intelligence. Calibration, observation, and enforcement.**

Meridian = startup calibration (5-15s). Observe = continuous per-iteration
measurement. Warden = bounds-keeping enforcement around scheduling, hot-region
residency, quarantine, and hardening policy.

**Meridian (startup):**
- GPU profiling: GEMM→actual TFLOPS, streaming copy→HBM/PCIe BW, NVML→power/temp/ECC/memory
- Network probing: N×N latency/bandwidth matrix, topology detection
- Per-vendor calibration microbenchmarks populate `TargetCaps` + `OpcodeLatencyTable`; Mimic's MAP-Elites search warm-starts from Cipher-persisted archives for the measured hardware
- Discrete-search joint 5D partition optimization (FORGE.md §25.6): TP×DP×PP×EP×CP factorization + schedule + bucket size + per-link weight assignment, minimizing predicted step time from `CollectiveBenchmarks` + `mimic::fast_cost` + per-link congestion telemetry. Bounded branch-and-bound over the calibrated cost surface; no external SMT solver.
- Output: complete device-specific kernel set + MeridianConfig. Re-probes on topology change.

**Observe (continuous):**
- Digital twin: DAG + Mimic kernel predictions + Meridian corrections → iteration prediction (±5-10%)
- Per-kernel bottleneck classification: COMPUTE/MEMORY_BW/COMMUNICATION/BUBBLE/IMBALANCE
- Predicted vs actual monitoring. >10% drift → diagnose → trigger Meridian recalibration
- Recommendations ranked by expected_speedup × confidence, tagged auto-hot/auto-cold/manual
- Model intelligence (periodic): Hessian spectrum (Lanczos), gradient health, effective rank (randomized SVD), CKA layer redundancy, convergence prediction, Chinchilla scaling laws

**Warden (enforcement):**
- Deadline and scheduler policy for hot/warm/cold Crucible threads.
- Hot memory region registration and hardening posture applied by Keeper or bench harnesses.
- Quarantine and lifecycle bounds for degraded Relays / Cogs.
- Warden does not own transport congestion control or observation metrics; those live in `cntp/`, `topology/`, and `observe/`.

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

**Phase 1: Foundation (DONE — 9.5K lines and 24 tests at the end of the phase, built then with Clang 22 and GCC 15. The tree builds only with GCC 16 at this time, §I)**

L4 Operations: TraceRing SPSC, MetaLog, recording pipeline. L6 Graphs: TraceGraph CSR. L7 Merkle DAG: RegionNode, BranchNode, content/merkle hashing. L3 Memory: MemoryPlan sweep-line, PoolAllocator. L4/L7 Compiled Tier 1: ReplayEngine, CrucibleContext, dispatch_op, divergence recovery. L2 Kernels: CKernel 146-op taxonomy. L14: Serialize/Deserialize, Cipher. L6 Graph IR: Graph.h, ExprPool, SymbolTable. L0 partial: Met(X) effect rows + cap tags (`foundation/effects/Effect.h`, which defines `Alloc / IO / Block` and `Bg / Init / Test`, `foundation/effects/Computation.h`, `foundation/effects/Row.h`), Reflect.h (reflect_hash, reflect_print). Vessel: PyTorch adapter.

**Phase 2a: Safety Foundation (IN FLIGHT)**

Goal: complete the L0 structural-guarantee layer — axioms, safety wrappers, session types, CSL permissions.

- **Safety wrappers** in `include/fixy/` and `include/foundation/permissions/` — Linear, Refined, Tagged, Secret, Permission, SessionHandle, ScopedView, Machine, Monotonic, AppendOnly, WriteOnceNonNull, and the `fixy::ct` constant-time primitives. Most of them cost nothing at run time (`sizeof(Wrapper<T>) == sizeof(T)`). §L0 gives the storage regime of each graded wrapper.
- **Session-type stack** in `include/fixy/session/` (25 headers, namespace `fixy::session`). It holds Honda 1998 binary types and HYC 2008 MPST with projection (`Global.h`, `Projection.h`). It holds Gay-Hole 2005 subtyping and an asynchronous fragment with a stated channel capacity (`Subtype.h`). It also holds liveness (`Liveness.h`), BSYZ22/BHYZ23 crash-stop (`Crash.h`, `CrashTransport.h`) and crash-stop association (`CrashAssociation.h`). The CSL × session layer is the `PS` parameter of `SessionHandle` (`Handle.h`). `utils/tools/session_oracle` tests the session relations against published mechanisations (Tirore-Bengtson-Carbone ITP 2023 and ECOOP 2025, PMY25), and `test/session_oracle/golden.csv` pins the answers.
- **CSL permissions** (misc/THREADING.md) — `Permission<Tag, Brand>`, `SharedPermission` + `SharedPermissionPool`, `mint_permission_fork` (CSL parallel rule as RAII fork-join), and the cache-tier rule of `fixy/concurrent/ParallelismRule.h` (L1/L2 → sequential, L3/DRAM → parallel).
- **Production refactors**: Vigil → Machine + Session, TraceRing → PermissionedSpscChannel, KernelCache → SwmrSession + ContentAddressed, Cipher tiers → Delegate + Tagged, CNTP layers → Session over Session. ~70 tracked tasks in the backlog.
- **`verify` preset (internal small SMT — deferred)** reserved for residual integer-arithmetic proof obligations only — scope matches TVM Analyzer PR #1367: bounds, divisibility, modular. No external solver dependency. Interim mode: contract enforcement only. Not a kernel-optimality engine.

**Phase 2b: Forge + Mimic Core (IN PARALLEL)**

Goal: vendor-agnostic optimizer + per-vendor backend framework per FORGE.md / MIMIC.md. No dependency on 2a; the two phases proceed in parallel.

- **IR002 scaffolding** (FORGE.md §18): `KernelGraph`, `KernelNode`, `NumericalRecipe` (interned), `TileSpec`, per-kind attrs pools, `ExecutionPlan`, PatchPoint taxonomy (8 kinds), ChainEdge semaphore pool. Of these, only `NumericalRecipe` exists at this time.
- **Recipe registry** (FORGE.md §20) — a compiled table in `include/crucible/RecipeRegistry.h`. It holds eight starter recipes, and the registry interns them into a `RecipePool`. Each recipe has one of four determinism tiers (UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT). A `native_on` bitmap per chip and a `tc_shape_constraint` for BITEXACT_TC recipes are planned, and no field holds them at this time.
- **Forge 12-phase pipeline** (FORGE.md §5): INGEST → ANALYZE → REWRITE → FUSE → LOWER_TO_KERNELS → TILE → MEMPLAN → COMPILE → SCHEDULE → EMIT → DISTRIBUTE → VALIDATE. Hard wall-clock budgets per phase. No phase runs at this time.
- **Mimic CPU reference backend first** (correctness oracle): x86_64 AVX512 / aarch64 NEON, scalar-FMA BITEXACT_STRICT always, every higher-tier recipe validated pairwise against CPU output.
- **Mimic NVIDIA backend** (M2-M9 of MIMIC.md build plan): IR003NV + SASS emitter + three-tier simulator + MAP-Elites + CUPTI calibration harness + runtime library (direct `/dev/nvidia*` ioctls, no libcuda) + collective library (CNTP, no NCCL).
- **Mimic AMD / TPU / Trainium backends** follow the same template (one self-contained subsystem per vendor).

**Phase 3: Meridian + Observe / Warden**

Goal: hardware calibration, continuous observation, and enforcement as separate
cooperating surfaces.

- GPU profiling + network probing at startup. Discrete-search joint 5D partition optimization (FORGE.md §25.6) picks topology from calibrated `CollectiveBenchmarks` + `mimic::fast_cost`. Per-link congestion telemetry (TX/RX bytes, drop rate, queue depth, sysctl-derived effective capacity) feeds into the cost surface so decisions adapt to heterogeneous-NIC fleets and live load.
- Digital twin: DAG + Mimic kernel predictions + calibration corrections → iteration prediction (±5-10%).
- Continuous monitoring, bottleneck diagnosis, recommendations engine. Observe drift detection triggers per-vendor Mimic recalibration when P95 residual > 10% for 100+ samples. Warden applies the bounded scheduling, residency, and quarantine posture the Keeper chooses.
- Model intelligence: Hessian spectrum, gradient health, effective rank, CKA, scaling laws.

**Phase 4: Compiled Tier 2-3**

Goal: shadow-handle dispatch and pushbuffer replay — push the foreground past
recording into a model where the user-visible work per op is just metadata.

- Shadow handles: a shadow TensorImpl with `DispatchKey::Crucible` and with metadata that points into PoolAllocator.
- Batched kernel launch: accumulate MAP-Elites-selected kernels, one doorbell write per ExecutionPlan.
- Pushbuffer + PatchPoint + ChainEdge replay: plan composition per CRUCIBLE.md §11.9 and FORGE.md §J.6.

**Phase 5: Keeper + Canopy + Cipher**

Goal: distributed, self-healing, persistent, cross-run-shareable.

- Keeper daemon: systemd service, health monitoring, self-updating. Executes the runtime observer's advice.
- Canopy mesh: SWIM gossip + Raft-scoped consensus, peer discovery. No master.
- Cipher: hot tier (RAID redundancy), warm tier (NVMe), cold tier (S3/GCS). Event-sourced. Three-level KernelCache: L1 IR002 snapshot federation-shareable cross-vendor; L2 IR003\* snapshot cross-chip within vendor family; L3 compiled bytes per-chip. **Federation cache-key portability bound:** the bound lives in the CONTENT half of the key, not the row half. The row half (`cipher/ComputationCacheFederation.h`) is constrained to an effect row, so it reaches only the `Row<Es...>` fold over `uint8_t` enum values, which is portable. The content half (`cipher/ComputationCache.h`) folds `display_string_of(reflect_constant(FnPtr))`, `stable_function_id<FnPtr>` and a `stable_type_id` per argument, so three of its four contributions are toolchain-bound. Cross-vendor and cross-run hold on both halves, because neither folds a vendor, device or ISA identity, but cross-toolchain does not hold. So GCC ↔ Clang and major-version rolls must key through `federation_key_with_toolchain<T>()` (disjoint by construction), or stay out of scope until a V2 canonical type-walker lands. Under the one fold at `foundation/diag/RowHash.h` EVERY graded wrapper folds a reflected lattice name. `utils/tools/dump_row_hashes.cpp` and its committed golden are the cross-build witness, because an in-TU `static_assert` cannot see a reflected name move underneath it.
- TrainingCheckpoints (weights, optimizer, data cursor, seed, step_idx, fleet UUIDs at checkpoint) survive reincarnation. Hardware-specific kernels recompiled by Mimic on new hardware using the warm-started Cipher archive.

**Phase 6: L8-L12 Intelligence**

Goal: model-aware optimizations, guided by runtime observation, validated by cross-vendor CI.

- L8: Token merging, early exit, adaptive patching.
- L9: Attention head classification, local losses, per-layer gradient strategy.
- L10: Layer growing/pruning, width mutation, architecture evolution.
- L11: Meta-gradients, per-layer LR from curvature, optimizer evolution.
- L12: Curriculum learning, manifold mixup, pipeline absorption.
- All optimizations are DAG branches (L7). Forge Phase L + runtime observation measure improvement; the Keeper activates via atomic swap only if (a) the branch compiles cleanly through Forge + Mimic, (b) cross-vendor CI tolerance holds for the branch's recipe tier, and (c) the runtime observer's predicted improvement > threshold.

---

Contracts discipline the code. Safety wrappers carry invariants in the type system. Measurement — MAP-Elites + calibrated simulators + cross-vendor CI — replaces proof as the correctness-witnessing mechanism. Meridian maps hardware. Observe measures runtime reality. Warden enforces bounded operating posture. The Keeper acts on calibrated-optimal decisions. The Vigil thinks within typed-safe infrastructure. The Cipher remembers — the compiled kernels, the MAP-Elites archives, the calibration data, the TrainingCheckpoints. When the last Relay dies, the Cipher carries the model and everything needed to re-materialize it on whatever silicon comes next.

---

## The Layers (17 layers, L0–L16)

```
L16  Ecosystem        computation genome, federated learning, hardware co-design
L15  Meridian + Observe/Warden calibration, runtime observation, policy enforcement
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
L0   Safety           8 axioms + contracts + CSL permissions + session types + fixy/*.h wrappers
```

Safety disciplines the code. Meridian maps. runtime observation sees. Vessel intercepts. Keeper serves. Vigil thinks. Cipher remembers. Canopy protects. Relay executes.



# Crucible Code Guide

*The canonical reference for writing Crucible code. Every rule has a cost-of-violation, a compiler-enforcement mechanism, and a discipline fallback. Nothing is style; everything is measured.*

Design intent: **the lowest foreground recording and shadow-dispatch latency the hardware allows, zero UB, bit-identical across hardware under BITEXACT recipes**. We do not promise specific nanosecond numbers — those depend on workload, system load, cache state, NUMA topology, and contention; they are reported by the bench suite, not guaranteed by the docs. Every rule below serves those structural intents.

---

## I. Toolchain

| Preset    | Compiler              | Role                                          |
|-----------|-----------------------|-----------------------------------------------|
| `default` | GCC 16.2.1 (patched)  | Primary dev. Debug. Contracts + reflection.   |
| `release` | GCC 16.2.1 (patched)  | Production. `-O3 -march=native -DNDEBUG -g`, contracts `observe`. §V tells why the level is `-O3` |
| `bench`   | GCC 16.2.1 (patched)  | Release + `CRUCIBLE_BENCH=ON`                 |
| `tsan`    | GCC 16.2.1 (patched)  | ThreadSanitizer (mutually exclusive with ASan)|
| `verify`  | GCC 16.2.1 (patched)  | Release code with no `NDEBUG` and contracts `enforce` (§V). The internal small-SMT verification suite is deferred, and no external solver is used |

`cmake/Toolchain-gcc16.cmake` finds the patched compiler `g++-16p`. `utils/toolchain/gcc/build.sh` builds it from GCC 16.2.1 and the patches in `utils/toolchain/gcc/patches/`. The `libstdc++ 16.0.1 status` notes in §IV are probes of libstdc++ 16.0.1, and this guide does not repeat each probe for 16.2.1.

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

**Compiler enforcement:** `-Werror=conversion -Werror=sign-conversion -Werror=arith-conversion -Werror=enum-conversion -Werror=old-style-cast`, plus `utils/scripts/check-no-throw-no-rtti.sh`, which holds the no-RTTI property on the artifact.

**Discipline:**
- Every semantic value is a strong type. No raw `uint32_t` for anything with meaning.
- IDs: `OpIndex`, `SlotId`, `NodeId`, `SymbolId`, `MetaIndex`, `KernelId` (all `CRUCIBLE_STRONG_ID(Name)` → `explicit(uint32_t)`, `.raw()`, `.none()`, `<=>`, no arithmetic).
- Hashes: `SchemaHash`, `ShapeHash`, `ContentHash` (all `CRUCIBLE_STRONG_HASH(Name)`).
- Enums: `enum class` with explicit underlying type. Convert via `std::to_underlying()` only.
- Bit reinterpretation: `std::bit_cast<T>()` only. `reinterpret_cast` is BANNED.
- Arithmetic: `foundation::sat::add_sat` / `sub_sat` / `mul_sat` (`include/foundation/Saturate.h`) for all size/offset math. libstdc++ 16 spells the standard forms `std::saturating_add` / `saturating_sub` / `saturating_mul`. It has no `std::add_sat`.
- Casts: C-style cast is a compile error. `const_cast` is BANNED.

```cpp
// ✓ CORRECT — silent parameter swap impossible
void connect(OpIndex src, OpIndex dst, SlotId slot);

// ✗ WRONG — caller can swap src/dst/slot silently
void connect(uint32_t src, uint32_t dst, uint32_t slot);
```

### 3. NullSafe — `deref(v) ⇒ v ≠ null`

**Cost of violation:** crash (best case) or silent wrong answer.

**Compiler enforcement:** `-Werror=null-dereference -Werror=nonnull -Werror=nonnull-compare -Wanalyzer-null-dereference -Wanalyzer-possible-null-dereference`, `CRUCIBLE_PRE(p != nullptr)` on all pointer params.

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
void process(const TraceEntry* entry) {
    CRUCIBLE_PRE(entry != nullptr);
    // body can assume entry is non-null
}
```

### 4. MemSafe — `free(v) ⇒ ¬live(v)`

**Cost of violation:** use-after-free, double-free, memory corruption, RCE.

**Compiler enforcement:** `-fsanitize=address` in debug, `-Werror=use-after-free=3 -Werror=free-nonheap-object -Werror=dangling-pointer=2 -Werror=mismatched-new-delete -Wanalyzer-use-after-free -Wanalyzer-double-free`. Nothing in the tree throws, so there is no "destructor during stack unwind" class to eliminate — `utils/scripts/check-no-throw-no-rtti.sh` holds that on the artifact rather than a flag holding it on our TUs.

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
- Spin on atomic load with `CRUCIBLE_SPIN_PAUSE` (→ `__builtin_ia32_pause()`, the PAUSE that `_mm_pause` wraps, on x86, `yield` on ARM).
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
- The Cipher (`include/crucible/Cipher.h`) is a content-addressed object store in one local directory, and it deletes no stored object. Only its in-process cache of object bytes has a limit: at most 64 entries and 8 MB. When the cache is full, it removes the entry that was used least recently. Three tiers with their own eviction are planned: hot (LRU), warm (by age) and cold (an S3 lifecycle policy).

### 8. DetSafe — `same(inputs) ⇒ same(outputs)`

**Cost of violation:** replay breaks, bit-exactness CI reddens, cross-vendor equivalence lost.

**Compiler enforcement:** no `-ffast-math` family. `cmake/FpStrict.cmake` gives each target `-ffp-contract=off`, so GCC contracts no multiply and add into an FMA (§V). The recipe pins FTZ.

**Discipline:**
- DAG fixes execution order (topological sort with hash-based tiebreak).
- Memory plan fixes addresses (pool_base + offset, content-addressed).
- Philox4x32 RNG: counter-based, platform-independent. Zero RNG state anywhere.
- KernelCache keyed on `(content_hash, row_hash)` (`include/crucible/KernelCache.h`).
- Reduction topology (planned): a pinned binary tree sorted by UUID for BITEXACT recipes. No reduction code exists at this time, because no compute backend exists.
- No hash-table iteration order dependencies. Sort keys before iterating.
- No pointer-based ordering. Events have `(cycle, kind, sequence_number)`.

---

## III. C++26 Language Features — Opt Matrix

### Opt IN

| Feature | Paper | Usage |
|---|---|---|
| Contracts (`contract_assert`, through `CRUCIBLE_PRE`/`CRUCIBLE_POST`) | P2900R14 | Every boundary function. Debug `enforce`, Release `observe`. Only a TU that takes `CRUCIBLE_CONTRACT_IGNORE_OPTIONS` changes to `ignore` (§XII names each one). A `pre` or `post` specifier on a declaration is a compile error in each build (§XII) |
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
| Exceptions | **not a flag** — `utils/scripts/check-no-throw-no-rtti.sh` | `-fno-exceptions` is NOT in this build and never was. It would not compile `src/fixy/concurrent/Topology.cpp`, whose eight catch sites turn a failed sysfs read into a conservative topology rather than a crash. Nothing in the tree throws: error paths are `std::expected` or a cold `[[noreturn]]` helper that calls `std::abort()` (§XII). The guard reads each static library of the tree, and it refuses a reference to `__cxa_throw` or `__cxa_rethrow`, also one that a library header makes. It admits a throw of `std::bad_alloc` or `std::bad_array_new_length`, and `utils/scripts/no-throw-no-rtti-allowlist.txt` admits the function of each other such throw, with its reason |
| RTTI | **not a flag** — `utils/scripts/check-no-throw-no-rtti.sh` | `-fno-rtti` is NOT in this build either, and on this tree it is a no-op: zero `dynamic_cast`, zero `typeid`, zero `std::type_info`, and every `virtual` lives in a planning document. Dispatch is a `kind` enum plus `static_cast`. The guard checks that the artifact references no `__dynamic_cast` and defines no typeinfo or vtable, other than those of a class that `utils/scripts/no-throw-no-rtti-allowlist.txt` admits: the `std::thread::_State_impl` classes that `std::jthread` uses |
| Coroutines on hot path | discipline | Heap allocation, unpredictable latency |
| `volatile` for concurrency | P1152R4 deprecated | `volatile` does not order; use `std::atomic` |
| `[=]` capturing `this` | P0806R2 deprecated; `-Werror=deprecated-this-capture` | Lifetime footgun |
| `memory_order::consume` | P3475R2 deprecated; `-Werror=deprecated-declarations` | Compilers promote to acquire anyway |
| VLAs (`int arr[n]`) | `-Werror=vla` | Stack UB |
| C-style casts | `-Werror=old-style-cast` | Silent UB conversions |
| `reinterpret_cast` | AST guard `utils/scripts/check-banned-calls.py`, with a content-keyed allowlist in `utils/scripts/no-reinterpret-allowlist.txt`. A stale allowlist entry fails CI at exit 2 | Use `std::bit_cast<T>` for value reinterpretation; `std::start_lifetime_as<T>` for arena type-punning; `<simd>` first-class intrinsic interop for SIMD |
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
| `std::saturating_add` / `saturating_sub` / `saturating_mul` (C++26) | Saturation arithmetic at size-math sites. libstdc++ 16 declares these names in `<numeric>` and has no `std::add_sat`. Call sites use `foundation::sat::add_sat` / `sub_sat` / `mul_sat` (`include/foundation/Saturate.h`), which return the exact result and call the library only on overflow |
| `std::breakpoint()` (C++26) | Hardware breakpoint for debug asserts. **libstdc++ 16 status:** the `<debugging>` header declares the symbol, and the library ships no definition — use the `foundation::detail::breakpoint*` stand-ins in `include/foundation/Platform.h` |
| `std::unreachable()` (C++23) | After exhaustive switch to eliminate default branch |
| `std::countr_zero` / `popcount` (C++20) | Bit manipulation primitives |
| `std::span` (C++20) | Pointer+count replacement |
| `std::jthread` (C++20) | Auto-joining thread, no destructor-terminate |
| `<simd>` (C++26 P1928R15) | The library reference. Project code does not include `<simd>`: the `foundation::simd` facade (§VIII) builds on the GCC vector extensions, which compile the same way on x86 and ARM. The library does per-ISA dispatch internally. **CRITICAL spelling note:** libstdc++ 16 ships under `namespace std::simd` (NOT `std::datapar` from ISO; no bridging aliases). **Vec is `std::simd::vec<T,N>`** (alias for `basic_vec<T,Abi>`); ISO `basic_simd`/`simd` names DO NOT exist. **Mask is `std::simd::mask<T,N>`** (alias for `basic_mask<Bytes,Abi>` — first param is byte size, NOT element type); ISO `basic_simd_mask`/`simd_mask` DO NOT exist. **Vec subscript is value-returning const** — use the generator constructor `V([](auto lane){ return ...; })` for per-lane construction, NOT `result[i] = x`. **Compare returns mask, NOT bool:** `v == w` yields `mask_type`; `if (v == w)` fails. Wrap with `all_of(v == w)`, `none_of(v != w)`, etc. Mask also has NO `operator bool` — `if (mask)` fails; use `if (any_of(mask))`. **`select` is NOT re-exported into plain `std::`** (only `min`, `max`, `minmax`, `clamp` are). **x86-only gate:** `bits/version.h` requires `__SSE2__` — on AArch64/Graviton, `<simd>` is empty. Use `<experimental/simd>` (`std::experimental::parallelism_v2`) for ARM + math; see separate row. **FTM:** `__cpp_lib_simd` is NEVER defined; internal gate is `__glibcxx_simd 202506L` requiring `__SSE2__` + structured bindings ≥202411 + expansion statements ≥202411. We don't gate per the no-feature-guards rule. **Shipped:** all loads/stores (6 overloads each: range / iter+n / iter+sentinel × masked/unmasked), all reductions (`reduce(v[, mask][, op[, identity]])`, `reduce_min/max(v[, mask])` — noexcept; `reduce(v, op)`/`reduce_{min,max}_index` NOT noexcept), all mask reductions (`all_of`, `any_of`, `none_of`, `reduce_count`, `reduce_min_index`, `reduce_max_index`), all element-wise algorithms (`min`, `max`, `minmax`, `clamp`, `select`), `chunk`, `cat` (signature under open LWG review), `permute`, public struct templates `rebind<T,V>` / `resize<N,V>` / `alignment<V,T>` plus `_t`/`_v` aliases, `zero_element`, `uninit_element`, all flag constants (`flag_default`, `flag_aligned`, `flag_overaligned<N>`, `flag_convert`). **First-class intrinsic interop:** `basic_vec` has ctors AND `operator _NativeVecType()` conversions for raw `[[gnu::vector_size]]` builtins AND x86 `__m128`/`__m256`/`__m512` — drop in/out of `<immintrin.h>` at zero cost without `bit_cast`. **Missing entirely:** all math (`sin`/`cos`/`sqrt`/`fma`/`abs`(fp)/...), all bit-manipulation (`popcount`/`rotl`/`byteswap`/`bit_ceil`/...), all complex math (`real`/`conj`/`polar`/...), all Bessel/special functions, public `iota` (libstdc++ has `__iota` as private); `foundation::simd::iota_v<V>()` gives it for the facade. **DetSafe rule:** integer reductions only — FP reductions are forbidden because a chunked fold reorders operations and IEEE rounding diverges across AVX-512 / AVX2 / NEON. The `foundation::simd::DetSafeSimd<V>` concept keeps FP types out of every facade reduction |
| `<experimental/simd>` (parallelism v2, TS) | Fallback for (a) ARM/Power/SVE targets where `<simd>` is gated out by `__SSE2__` and (b) math functions (`sin`/`cos`/`sqrt`/`exp`/`log` on vec) that `<simd>` does not provide. Namespace `std::experimental::parallelism_v2` (inline under `std::experimental`). FTM `__cpp_lib_experimental_parallel_simd = 201803`. Not deprecated — still maintained, has dedicated `simd_neon.h`, `simd_sve.h`, `simd_ppc.h`, `simd_math.h` (1501 lines). Use only where `<simd>` is unavailable; DetSafe rule still applies (no FP reductions) |
| `std::atomic<T>::fetch_max` / `fetch_min` (C++26 P0493R5) | Monotonic update without CAS retry loop; replaces the `Monotonic<T>::bump` CAS pattern. **libstdc++ 16.0.1 status:** shipped (`__cpp_lib_atomic_min_max = 202403L`) — for `atomic<integral>`, `atomic<T*>`, `atomic<floating>` (including `_Float16/32/64/128`, `__bf16`), AND all three `atomic_ref` specializations. Uses `__atomic_fetch_min/max` builtins when available; else CAS-loop fallback. Free-fn `atomic_fetch_{min,max}[_explicit]` exists for `atomic<T>*` (NOT for `atomic_ref`, per spec) |
| `std::atomic_ref<T>` (C++20 P0019R8, C++26 bump) | Atomic operations on externally-owned storage. **libstdc++ 16.0.1 status:** shipped. FTM `__cpp_lib_atomic_ref = 201806L` in C++20 mode, bumps to `202603L` in C++26 mode. Full API: `load`/`store`/`exchange`/`compare_exchange_*`/`fetch_*`/`wait`/`notify_one`/`notify_all`. `atomic_ref<const T>` exposes read-only subset. Padding-bits handling (P3475R2) is behaviorally present via `__compare_exchange<_AtomicRef=true>` CAS-retry using `__builtin_clear_padding`, though `__cpp_lib_atomic_ref_padding_bits` FTM is NOT advertised |
| `std::atomic_ref<T>::address()` (C++26 P2929R1) | Bench/diagnostic — verify the atomic points where the planner said it does. **libstdc++ 16.0.1 status:** shipped in C++26 mode. Rides on `__cpp_lib_atomic_ref >= 202603L` bump; no dedicated FTM. `constexpr noexcept`; returns `const void*`/`void*` (volatile-qualified as applicable) |
| `std::atomic<T>::wait` / `notify_one` / `notify_all` (C++20 P1135R6) | Efficient wait-for-change on atomic. **libstdc++ 16.0.1 status:** shipped. FTM `__cpp_lib_atomic_wait = 201907L`. On Linux uses raw `futex` syscall (4-byte aligned `__platform_wait_t`); on FreeBSD 64-bit uses its native 64-bit futex; elsewhere falls back to mutex+condvar proxy wait (so latency is higher on non-Linux). `atomic_flag` + free-fn `atomic_wait[_explicit]`/`atomic_notify_*` also shipped. **Hot-path rule still stands:** this is futex-backed, latency is 1-5 µs — keep spinning with `_mm_pause` for intra-core waits ≤40 ns |
| `std::atomic<shared_ptr<T>>` / `std::atomic<weak_ptr<T>>` (C++20 P0718R2) | Atomic reference-counted pointer — one-stop publication when a DAG branch replaces a shared object. **libstdc++ 16.0.1 status:** shipped. FTM `__cpp_lib_atomic_shared_ptr = 201711L`. `is_always_lock_free = false` — uses internal `_Sp_atomic` with packed refcount+pointer. Full API incl. `wait`/`notify_*`. Not appropriate for hot-path atomic — the refcount CAS serializes readers. Use for Keeper/Cipher warm-tier shared state updates |
| `std::atomic_signed_lock_free` / `std::atomic_unsigned_lock_free` (C++20 P1135R6) | Type aliases to the widest integer atomic that is always-lock-free on the target. **libstdc++ 16.0.1 status:** shipped. FTM `__cpp_lib_atomic_lock_free_type_aliases = 201907L`. Use for counters where portability of the lock-free guarantee matters more than exact width |
| `<debugging>` — `breakpoint_if_debugging`, `is_debugger_present` (C++26) | Pause when debugger attached, continue otherwise; tighten `CRUCIBLE_INVARIANT`. **libstdc++ 16 status:** header declares but symbols absent from libstdc++.so — use the `foundation::detail::*` stand-ins in `include/foundation/Platform.h` |
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
| `std::vector::reserve` (anywhere) | **Banned** (enforced by CI: the AST guard `utils/scripts/check-banned-calls.py`, with a content-keyed allowlist in `utils/scripts/no-reserve-allowlist.txt`) — signals wrong container choice. `vector::push_back` past capacity is **O(n)** (copies every existing element to the new buffer); reserve only delays the first O(n) spike, doesn't eliminate it. The "amortized O(1)" story hides three real costs: (1) **tail-latency**: each individual growth event is O(n), fatal for p99 budgets; (2) **silent perf cliff**: move-during-growth requires `noexcept` move ctors or falls back to **copy** without warning; (3) **heap churn**: every growth allocates new + frees old, fragments the allocator. Replacements: known max → `std::inplace_vector<T, N>` (compile-time bound, zero heap, true O(1) push_back, contract-checked overflow); known exact size at construction → `vector<T> v(N)` and fill by index (one allocation, no growth); truly unbounded → plain `vector<T>` and accept amortization (rare in practice; usually means you should have used arena-backed storage). Hot path: arena, never vector at all (HS10 in §XVIII) |
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

**Neither `-fno-exceptions` nor `-fno-rtti` is in this build.** Both were listed
here as common flags and neither has ever been on a compile line. The properties
they stood for do hold — measured 2026-09-20: zero `__cxa_throw` references and
zero typeinfo or vtable definitions in `libcrucible.a` in every preset — and
`utils/scripts/check-no-throw-no-rtti.sh` is what holds them, on the artifact, where a
throw arriving through an instantiated library header is also visible. Adding the
flags is the wrong repair; the opt-out table in §III says why for each.

```
-std=c++26                           C++26, with no GNU dialect
-fcontracts                          P2900 contracts
-freflection                         P2996 reflection
-fno-strict-overflow                 signed overflow wraps (not in the ubsan-strict preset)
-fno-delete-null-pointer-checks      the optimizer keeps each null check
-ftrivial-auto-var-init=zero         P2795R5: zero fill of each uninitialized stack variable
-fstack-protector-strong             stack canaries
-fstack-clash-protection             stack clash guard pages
-fcf-protection=full                 Intel CET (aarch64: -mbranch-protection=standard)
-fno-omit-frame-pointer              readable traces
-fno-plt                             direct calls
-fno-semantic-interposition          inlining across translation units
-fvisibility=hidden                  hidden symbols
-fvisibility-inlines-hidden          hidden inline functions
-ffunction-sections                  one function in each section
-fdata-sections                      one variable in each section
-fno-common                          no tentative definitions
-fstrict-flex-arrays=3               strict rules for flexible arrays
-fsized-deallocation                 sized `delete`
-fstrict-enums                       the optimizer uses the range of each enum
-fconstexpr-ops-limit=33554432       the error threshold of the row constexpr-ops (§XV)
-D_FORTIFY_SOURCE=3                  glibc bounds checks
```

`cmake/FpStrict.cmake` gives each target the FP floor: `-fno-fast-math`,
`-ffp-contract=off`, `-fno-associative-math`, `-fno-reciprocal-math`,
`-fno-finite-math-only`, `-fsignaling-nans`, `-frounding-math` and
`-ftrapping-math`. With `-ffp-contract=off`, GCC contracts no multiply and add
into an FMA, also in one statement. No build takes `-fno-math-errno`.

Each build type other than Debug also takes `-fharden-compares` and
`-fharden-conditional-branches` (`CMakeLists.txt` section 3.13). No build takes
`-fharden-control-flow-redundancy`. These flags defend a binary against fault
injection, and they find no defect in a test. On a heavy translation unit, they
cost 20% to 46% of the back end.

In each preset, `cmake/BuildLauncher.cmake` puts `utils/scripts/build-launcher.py`
in front of each compile command and in front of ccache. It also puts the
launcher in front of each link of an executable or a shared library. The
launcher gives the command to the compiler or the linker unchanged. It writes
the CPU time, the wall time and the peak memory of the step to `<output>.cost`,
and a record of a ccache hit holds no time. The record of a compile also holds
its user instructions, from a hardware counter, when the host gives an exact
count. A step over the memory error
threshold of the row `compile-memory` or `link-memory` in
`utils/scripts/budgets.txt` fails, unless a row of the ledger of that check
admits its output with a reason. `RLIMIT_CPU` stops a step at three times the
error threshold of the row `compile-cpu` or `link-time`, except on a GitHub
runner (§XV "Compile time", rule 13). Its soft limit and its
hard limit are equal, so the kernel sends SIGKILL and not SIGXCPU. The GCC 16
driver crashes when SIGXCPU stops the compiler. The launcher adds
approximately 10 ms to each step, and the compile database does not show it.
`utils/scripts/cost_meter.py` holds the measurement and the record format, and
each launcher imports it.

In each preset, `utils/tools/quarantine/Quarantine.cmake` builds one GCC
plugin of `utils/tools/quarantine/` at configure time, and puts `-fplugin=` and
the plugin arguments on each C++ compile. With `CRUCIBLE_QUARANTINE=OFF`, the
build loads the contract plugin of `contract.cpp`, which applies only the
contract rule of §XII. With `REPORT` or `ERROR`, it loads the quarantine plugin
of `quarantine.cpp`, which applies the quarantine rule and the contract rule.
The contract rule is in `plugin_core.h`, so a change of `quarantine.cpp`
compiles no object of an `OFF` build again. The load of the plugin adds
approximately 6 ms to each compile, and the rule takes less than 0.2% of the
CPU time of a heavy unit. ccache ignores the paths in the plugin arguments, so
the build directories of two work trees share the cache.

### Debug preset

```
Common flags +
-O1 -g                                section 0 of CMakeLists.txt. The wrappers inline at -O1, not at -Og
-D_GLIBCXX_ASSERTIONS=1               libstdc++ bounds checks (section 3.15)
-D_GLIBCXX_SANITIZE_VECTOR=1          ASan annotations of the vector capacity (section 3.15)
```

A Debug build gives no contract semantic flag, so the GCC default `enforce`
applies. The `default` preset puts `-fsanitize=address` and
`-fno-sanitize-recover=all` on each target, and
`-fsanitize=undefined,bounds-strict` on each test executable. The `tsan` preset
puts `-fsanitize=thread` and `-Wno-tsan` on each target. The `ubsan-strict`
preset puts `-fsanitize=undefined,float-cast-overflow,float-divide-by-zero,bounds-strict`
on each target, and it removes `-fno-strict-overflow`. No build sets
`-D_GLIBCXX_DEBUG`, because that macro changes the layout of each container.

### Release preset

The `release` preset sets `CMAKE_CXX_FLAGS` to `-O1 -march=native -DNDEBUG -g`.
CMake appends `CMAKE_CXX_FLAGS_RELEASE` (`-O3 -DNDEBUG`) after it, and GCC uses
the last `-O`, so each Release TU compiles at `-O3`. The `pgo` and `pgo-release`
presets add the profile flags of `CRUCIBLE_PGO` (§VIII).

```
Common flags +
-O1 ... -O3                           GCC uses the last level, -O3
-march=native
-DNDEBUG
-g                                    frame information for profiling
-fharden-compares                     section 3.13 of CMakeLists.txt
-fharden-conditional-branches         section 3.13 of CMakeLists.txt
-fcontract-evaluation-semantic=observe  the Release default. It does the check
                                      and reports a violation. The handler
                                      aborts, so a violation ends the process.
                                      §XII names the TUs outside this policy.
```

No build sets `-mtune=native`, `-flto=auto`, `-fvect-cost-model=unlimited`,
`-mprefer-vector-width=512`, `-fipa-pta`, `-fgraphite-identity` or
`-floop-nest-optimize`. No build sets `-fno-trapping-math`, because the FP
floor sets `-ftrapping-math`. A Release build has no `-D_GLIBCXX_ASSERTIONS`.
That macro adds a bounds check to each `operator[]` of `std::array`,
`std::span` and `std::vector`, also on the hot path.

### Verify preset

The `verify` preset inherits `release` and makes two changes. It removes `-DNDEBUG` from `CMAKE_CXX_FLAGS` and `CMAKE_CXX_FLAGS_RELEASE`, so `CRUCIBLE_INVARIANT` and `CRUCIBLE_DEBUG_ASSERT` do their checks in the Release code. It sets `CRUCIBLE_VERIFY=ON`, which changes the Release contract semantic from `observe` to `enforce` (§XII). The preset does not add `-fanalyzer`. The separate `analyzer` preset sets `CRUCIBLE_ANALYZER=ON`, which adds `-fanalyzer` to a Debug build. The internal small-SMT verification tier is reserved for residual integer-arithmetic obligations (deferred — interim: contracts-only). No external solver dependency: Crucible ships no Z3, no CVC, no proprietary SMT engine, period.

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
-fwrapv                           ~1% global perf cost; use `foundation::sat::*_sat` at sites
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
- `-Wpadded` (off). A class can have a padding byte, except the element type of a large fixed list, which `utils/scripts/check-padded-lists.py` rejects (§XV "Compile time")

The `-Wsuggest-*` flags of sections 1.8 and 3.10 of `CMakeLists.txt` are on, and `-Werror` makes each one an error. GCC suggests the `cold` attribute for a function that calls a cold function on each path. So a constructor, a destructor or another function on the normal path of a hot caller has no `cold` attribute.

---

## VII. Attributes and Macros

`include/foundation/Platform.h` provides these. Use them deliberately.

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
// x86: __builtin_ia32_pause(), the PAUSE that _mm_pause() wraps, with no
//      intrinsics header — 10-40ns via MESI invalidation
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
2. **The `foundation::simd` facade** (`include/foundation/Simd.h`) — the default for new portable SIMD. It does not include `<simd>`, because the libstdc++ `<simd>` is empty on ARM. It builds its own `vec<T, N>` and `mask` types over the GCC vector extensions. It gives `load` / `store` (and their aligned forms), `iota_v`, `prefix_mask`, the `DetSafeSimd` concept, and AVX-512 detection (`kAvx512Available`, `runtime_supports_avx512`). Its reductions are integer only: `reduce_add` / `reduce_and` / `reduce_or` / `reduce_xor` / `reduce_min` / `reduce_max`. §IV documents the library `<simd>` surface.
3. **GCC builtins** (`__builtin_ia32_*`) — only when the facade cannot express the operation (`vpshufb`, `vpternlog`, `vpcompressd`, `vpgatherdd`, `vpopcntq`) or when math/bit-manip on vec is needed (those C++26 sections aren't shipped in libstdc++ 16). The canonical example is `SwissTable.h`'s `vpcmpeqb + vpmovmskb` probe. A header under `include/` includes no x86 intrinsics header, because the builtin that an intrinsic calls gives the same instruction (§XV "Compile time", rule 4). Compile-time `#ifdef __AVX2__ / __SSE2__` selection — single ISA per build, matches the deployment microarch chosen at `-march=` time.

Crucible's vectorizable hot paths: Philox RNG, hash mixing, TensorMeta extraction, reduction kernels.

### Link-Time Optimization (LTO)

`-flto=auto` — parallel LTO: whole-program inlining, dead code elimination, cross-TU constant propagation. No preset and no CMake file sets it at this time. §V gives what a Release build does: every TU compiles at `-O3`, and the link has no LTO.

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
| TraceRing push | one acquire/release pair on isolated cache lines | SPSC + `_mm_pause` + `alignas(64)` head/tail |
| Arena bump allocation | bump + mask, no branch, no lock | one cache line touch |
| MetaLog append | one acquire/release pair on isolated cache lines | SPSC, write-combined |
| Cross-core signal wait | bounded by MESI cache-line transfer cost | floor is the interconnect; cross-socket worse than intra-socket |
| Swiss-table lookup (hit) | one open-addressed probe with SIMD compare | Open addressing + SIMD probe |
| Contract check at boundary | one branch under `semantic=observe`; nothing under `ignore` | only four bench TUs and four test targets use `ignore` (§XII) |
| Syscall | kernel-mediated transition | Banned on hot path |
| `malloc` | allocator round-trip | Banned on hot path |

The operations that follow are planned (Phase 4). No shadow handle and no `ExecutionPlan` exist at this time, so the table gives the design shape and not a measured path.

| Planned operation | Per-call shape | Notes |
|---|---|---|
| Shadow handle dispatch | metadata write, no function call | `[[gnu::flatten]]` |
| ExecutionPlan submit (warm) | cache lookup + doorbell write | Cache hit + doorbell |
| ExecutionPlan submit (cold, ≤5 patches) | plan lookup + patch writes + SFENCE + doorbell | one plan creation per fresh shape |

A regression in measured latency on the bench suite is investigated like any other regression — root-cause first, then fix. There is no fixed "budget number" promised in this guide; the bench-suite outputs are the source of truth for the current state of the world on each hardware target.

### Hot functions fit in one cache line

Target: every hot-path function compiles to ≤64 bytes of machine code (one I-cache line fetch). Audit via `objdump -d`. The audit is a review step. No build step or CI guard measures the size of a hot function, and the repository holds no disassembly snapshot. Configure with `-DCRUCIBLE_DUMP_ASM=ON` to write the disassembly of each test and bench binary into the build tree, and read the hot functions there.

If a hot function exceeds 64 bytes of machine code, either (a) the design is wrong (split into hot + cold), (b) a dependency is bloated (eliminate), or (c) the budget was optimistic (document the new budget and notify).

### Cold-path outlining is mandatory

Every `[[unlikely]]` branch body that is more than one return statement or one early-abort is **outlined** to a `[[gnu::cold, gnu::noinline]]` helper function. Rationale: `[[unlikely]]` only steers the branch predictor; it does not prevent the cold body's code from occupying an I-cache line adjacent to the hot path.

Rule: if a `[[unlikely]]` block has more than 8 lines of non-trivial work, outline it. The cold helper sits in `.text.unlikely` (a separate section) and never touches the L1 I-cache during normal execution.

### PGO + AutoFDO for release

Release builds are profile-guided; non-PGO release is a dev build, not production. Typical gain on branchy code: **5-15%**.

```bash
# 1. Collect the profile for this host's tier: configures pgo-generate,
#    builds the benches, runs them, writes pgo/gcc-<version>/<tier>/
utils/scripts/pgo-bootstrap.sh

# 2. Build with it
cmake --preset pgo && cmake --build --preset pgo
```

The tier is the name the compiler resolves native to (`znver5`, `sapphirerapids`; on aarch64 the `-mcpu=native` name), so the same two commands serve every host and no source file names the hardware. `CRUCIBLE_PGO=use` refuses a profile from a different compiler, tier or flag set, and a function whose control flow changed after collection stops the build with `-Wcoverage-mismatch`. Collect again. The CMake block in the root `CMakeLists.txt` holds the flag rationale.

A release artifact is built with the `pgo-release` preset. It adds `CRUCIBLE_PGO_STRICT=ON`, which requires a profile collected at HEAD from a clean tree, and it builds no benches. The release process is: commit, `utils/scripts/pgo-bootstrap.sh`, `cmake --preset pgo-release && cmake --build --preset pgo-release && ctest --preset pgo-release`, then archive `pgo/gcc-<version>/<tier>/` next to the artifact. The counters are an input of the binary. Every TU carries `CRUCIBLE_PGO_PROFILE` (tier, time, commit, tree state), and every bench report prints it as `pgo:`, so a number from a profiled build is never mistaken for a plain one. `ctest --preset pgo` is the proof that the profile-guided passes preserved every DetSafe result.

Alternative (continuous profiling from production runs): **AutoFDO** via `-fauto-profile=<profile.afdo>` fed from `perf record`. Same wins, no instrumented build.

### Per-deployment microarchitecture targeting

- **Local development**: `-march=native -mtune=native`. Exact features of the build machine.
- **Distribution / production**: the specific minimum microarch of the deployment fleet, declared in the build manifest. No project-wide default — the choice is deployment-local and documented alongside the fleet's hardware spec.
- **Single-target binaries only.** Crucible does not use `[[gnu::target_clones]]` or any other multi-target / function-multiversioning mechanism. Each binary is compiled for one ISA tier; fleets that span multiple microarchs ship multiple binaries (one per tier), not a single fat binary with runtime dispatch. This keeps icache pressure predictable, eliminates the indirect-jump-on-first-call cost, and means every hot function compiles to a single straight-line code path the optimizer fully sees through.

No single `-march=` default ships with Crucible. The build owner picks it for their fleet.

---

## IX. Concurrency Patterns

### The thread set

```
Foreground (hot):   records each op into TraceRing and MetaLog
Background (warm):  one pipeline thread, which starts four stage threads and joins them
  drain     pops batches of entries from TraceRing
  detect    finds the iteration boundaries
  build     builds the TraceGraph of an iteration
  publish   makes the RegionNode and its memory plan, and activates the region
```

`BackgroundThread::start` (`include/crucible/BackgroundThread.h`, with its body in `src/BackgroundThread.cpp`) starts the pipeline thread. The pipeline gives each stage a thread of its own, and SPSC channels connect the stages. Each stage spins on its input channel. The build stage and the publish stage also share `arena_alloc_gate_`, a blocking lock around the bump cursor of the arena. A waiter on that lock sleeps in the kernel. `bench/baselines/record_leaf.json` holds the measured cost of a foreground record. The Vigil starts no other thread. The ledger refresh daemon and its cache-tier probe in `include/crucible/ledger/` have threads of their own, and only their tests start them at this time. Apart from these, only the OS and OS-adjacent code (systemd, signal handlers) add threads. Background workers for parallel kernel compilation inside Mimic are planned.

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
| KernelCache slots (planned use) | bg | fg | CAS claim of the content hash, release publish, acquire read |
| RegionNode::compiled (planned use) | bg | fg | one release publish (`PublishOnce`), acquire read |
| Cipher warm writes (planned) | bg | peers | Raft commit (planned) |

Three rows describe planned use. `KernelCache` (`include/crucible/KernelCache.h`) has the synchronization of its row, but only tests and benches write or read its slots at this time. The one publish of `RegionNode::compiled` is in `add_branch`, which nothing calls, and no code reads the field. The Cipher has no replication and no Raft. A `Cipher` is not thread-safe, and one thread owns it. Only tests call `Vigil::persist`, which writes to the Cipher, and nothing calls `Vigil::load`.

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

For atomic CAS on an element of a plain array, without the per-element cost of `std::atomic<T>`, use `std::atomic_ref`. No file in the tree uses it at this time, and the KernelCache slots hold `std::atomic` fields. This example shows the pattern:

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

The TraceRing between the foreground and the drain stage is the *floor* of concurrency in Crucible. It is also the easy case: one fg producer, one bg consumer, one SPSC ring. Beyond that — the background pipeline, multi-reader snapshots, and the planned kernel compile pools and sharded dispatch — the discipline must scale, and "scale" means the type system has to do the bookkeeping the human stops doing.

Crucible encodes **Concurrent Separation Logic** (O'Hearn 2007) as a family of zero-cost C++ types. The discipline is mechanical: tokens prove ownership at the type level; the compiler enforces who can call what. The runtime cost is exactly the underlying primitive's cost (SpscRing acquire/release, AtomicSnapshot seqlock, etc.) — no extra mutex, no extra CAS, because the type system already proved the access pattern is sound.

#### The CSL → C++ mapping

| CSL concept | C++ encoding | File | When to use |
|---|---|---|---|
| Separating conjunction `*` | `mint_permission_split<L,R>(Permission<In>&&)` | `foundation/permissions/Permission.h` | Splitting a region into disjoint subregions |
| Frame rule | Linearity (move-only `Permission<Tag>`) | `foundation/permissions/Permission.h` | Every exclusive ownership claim |
| Parallel composition rule | `mint_permission_fork<Children...>(ctx, parent, callables...)` | `foundation/permissions/PermissionFork.h` | Spawning N threads with disjoint sub-permissions |
| Fractional permissions `e ↦_p v` | `SharedPermission<Tag>` + `SharedPermissionPool` (atomic refcount) | `foundation/permissions/Permission.h` | Multi-reader / single-writer with mode upgrade |
| Lifetime-bound borrow | `with_read_view(source, body)` gives the body a `ReadView<Tag, Brand> const&` of the brand of the source. `mint_read_loan` parks the token while a `ReadLoan` travels | `foundation/permissions/ReadView.h` | Scoped read borrow: the view exists only for the body's call |
| Resource invariants (Brookes) | DEFERRED — needs `LockedResource<T, Inv>` | (future) | Mutex-protected shared state |
| Logical atomicity (TaDA) | Implicit — every consume-and-return cycle | (free) | All Permission-typed operations |

`permission_row_t<Tag>` binds CSL ownership to Met(X) effect rows. A tag
gets its row from one of three sources: an edge in
`foundation::permissions::permission_rows`, a `using permission_row = Row<...>;`
member, or the row of its `parent_type`. A tag with no row has no mint. A tag
with a non-empty row needs a context that admits the row at each mint and
transfer (`mint_permission_root(ctx)`, the split and combine mints,
`mint_permission_share`, `permission_handoff`, `SharedPermissionPool::lend` and
`try_upgrade`, and `mint_permission_fork`). The forms without a context work
only for a tag whose row is `Row<>`. Canonical row-bearing tags:
`DiskSpilledRegionTag -> Row<IO, Block>`, `HugePageTag -> Row<IO>`,
`MmapRegionTag -> Row<IO>`, `GpuMemoryTag -> Row<Alloc>`,
`NetworkBufferTag -> Row<IO>`.

#### "Just right amount of concurrency" — the cache-tier rule

Parallelism only wins when memory bandwidth is the bottleneck. When the working set is L1/L2-resident, adding cores adds nothing but cache-line ping-pong, instruction-cache cold misses, and TLB shootdowns — strictly worse than a single core that already has the data hot. When the working set lives in L3 or DRAM, memory latency is the bottleneck and adding cores adds independent cache hierarchies (each thread's L1/L2 preloads its share) plus parallel memory-controller channels.

The decision rule (`fixy::concurrent::ParallelismRule::recommend` in `include/fixy/concurrent/ParallelismRule.h`):

| Working set | Decision | Reason |
|---|---|---|
| `≤ L1d per core` | **SEQUENTIAL** | Already hot in one core's L1; threading thrashes |
| `≤ L2 per core` | **SEQUENTIAL** | L2 is private; thread #2 cold-misses everything |
| `≤ L3 total` | **PARALLEL = min(cores per socket, usable CPUs, 4)**, NUMA-local | Within socket, cores share L3 bandwidth |
| `> L3` (DRAM-bound) | **PARALLEL = min(usable CPUs, ws / L2 per core)**, NUMA spread on a host with more than one node | Memory-channel-bound; scale until channels saturate |

The working set is the saturated sum of the read and written bytes. The factor rounds down to the ladder 1, 2, 4, 8, 16, so no factor is more than 16. "Usable CPUs" is the CPU count of the process, which a CPU quota can make smaller than the host count. The rule reads no per-item time and no caller hint. A caller that wants parallelism for expensive compute over cheap data is outside the rule and dispatches directly.

The promise: **never regresses**. If the rule says sequential, parallel must not be measurably faster (within 5%, measured by `bench/bench_no_regression.cpp`). If it says parallel, the speedup must justify the sync cost.

`fixy::concurrent::Topology` (`include/fixy/concurrent/Topology.h`) probes the cache sizes, the core counts and the NUMA nodes from sysfs one time. `fixy::spawn::mint_parallel_for` (`include/fixy/os/Spawn.h`) asks the rule for a decision on each call. A sequential decision runs every shard on the calling thread. A parallel decision starts no more threads than its factor. The NUMA policy of a decision is an intent, and no dispatcher binds the workers to a node. `fixy::spawn::mint_spawn` chooses between `mint_permission_fork_inline` and `mint_permission_fork` from the same rule.

#### Decision matrix — which Permission primitive

```
Need exclusive single-thread ownership?
    → Permission<Tag>                       (linear, move-only, sizeof = 1)

Need shared-read scoped to a function call (lifetime fits inside the caller's stack)?
    → with_read_view(std::move(source), body)  (the body gets a ReadView<Tag, Brand> const& of the
                                             brand of the source. No code can copy, move or store
                                             the view, and the caller gets the source again after
                                             the body. foundation/permissions/ReadView.h)

Need shared-read across threads (lifetime escapes)?
    → SharedPermission<Tag> via SharedPermissionPool::lend()
                                            (RAII guard, atomic refcount, mode upgrade via try_upgrade)

Need structured fork-join (split parent → N children → rejoin)?
    → mint_permission_fork<Children...>(ctx, parent, callables...)
                                            (CSL parallel rule, jthread-based, RAII join)

Need one writer that publishes the latest value to many readers?
    → SwmrSession<T, WriterTag, ReaderTag, ReaderBrand, WriterBrand>
                                            (fixy/concurrent/SwmrSession.h, over AtomicSnapshot)

Need a queue from one or many producers to one consumer?
    → PermissionedSpscChannel<T, Capacity, UserTag, Brand> / PermissionedMpscChannel<T, Capacity, UserTag, Brand>
                                            (fixy/concurrent/, typed sessions via mint_substrate_session;
                                             Brand is the brand of the root site, and spsc_channel_t /
                                             mpsc_channel_t read it off the root type)
```

#### Anti-patterns (review-rejected)

- **Storing `Permission<Tag>` in a long-lived struct field** that is shared between threads. Defeats linearity — the struct may be aliased and the type system can't see it. Permissions belong in handles (Pinned), thread-local stacks, or function parameters.
- **Passing `SharedPermission` by value across functions** without lifetime context. Lifetime gets confusing fast. Prefer `ReadView<Tag>` for scoped borrows; use `SharedPermissionGuard` (RAII) when crossing thread boundaries.
- **Manually spawning `std::jthread` with a Permission inside** instead of using `mint_permission_fork`. Bypasses the CSL parallel-rule encoding and skips static verification of `can_split_into_pack`. Use `mint_permission_fork` and let the type system check.
- **Parallelizing a workload smaller than L2** without explicit override. `ParallelismRule` keeps it sequential. A bypass almost always regresses (icache cold, MESI ping-pong, TLB shootdowns).
- **`new`-allocating a Permission or ReadView**. Heap allocation defeats the lifetime contract. `ReadView`, `Permission` and `SharedPermissionGuard` delete their class `operator new` and `operator delete`, so `new`, `std::make_unique` and `std::unique_ptr` refuse them. A global `::new` and a standard allocator, as in `std::make_shared` or `std::vector`, do not use those functions, and review is the only gate for them. Stack only.

#### Composition rules

- `Permission<Tag>` IS already linear → wrapping in `Linear<Permission>` is redundant, and a `static_assert` on `is_already_linear_v` in `fixy/Qtt.h` refuses it.
- Handles holding a `Permission` should be `Pinned` — the handle's existence is the proof of permission; moving the handle would break the proof.
- Use `[[no_unique_address]] Permission<Tag>` on handle members — collapses to 0 bytes via EBO.
- `SharedPermissionGuard` is move-only RAII → do NOT also wrap in `Linear<>`.
- `SharedPermissionPool` is `Pinned` — the atomic refcount IS the channel identity.
- `PermissionedSpscChannel` / `PermissionedMpscChannel` / `SwmrSession` are all `Pinned` for the same reason — the underlying primitive's atomics ARE the channel.

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
// Boundary: contract-checked.  The checks are statements of the body, because
// each build rejects a pre or post specifier (§XII).
std::expected<PlanId, Error> submit_plan(PlanId id, std::span<const PatchValue> patches) {
    CRUCIBLE_PRE(!id.is_none());
    CRUCIBLE_PRE(std::all_of(patches.begin(), patches.end(), valid_patch));
    std::expected<PlanId, Error> result = runtime::submit_plan_inner(id, patches);
    CRUCIBLE_POST(result, !result.has_value() || !result->is_none());
    return result;
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

Nothing throws. Not because a flag forbids it — `-fno-exceptions` is not in this build — but because every error path is one of the three tiers below. The ci_guard test `no_throw_no_rtti` (`utils/scripts/check-no-throw-no-rtti.sh`) reads each static library that the root `CMakeLists.txt` defines (`test/CMakeLists.txt` finds them in the build system, so a new library joins with no edit). It fails when a library references `__cxa_throw` or `__cxa_rethrow` in a function that a link can keep. It admits a throw of `std::bad_alloc` or `std::bad_array_new_length`, the failure of an allocation inside libstdc++, which ends the process as an exhausted memory does. `utils/scripts/no-throw-no-rtti-allowlist.txt` admits each other throw that a library header makes, with its reason. The test reads no shared library, no test executable and not the vessel. Three tiers of error response:

| Class | Mechanism | Runtime cost | Example |
|---|---|---|---|
| **Impossible** (contract violation) | `CRUCIBLE_PRE` / `CRUCIBLE_POST` / `contract_assert` | Debug and Release do the check, and both end the process, because the handler aborts. Its cost is 0 ns only in a TU that takes `CRUCIBLE_CONTRACT_IGNORE_OPTIONS` | Null pointer, OOB index, invariant violation |
| **Expected-but-rare** | `std::expected<T, E>` return | ~1 ns (branch on `.has_value()`) | Parse error, shape out of bucket, peer timeout |
| **Catastrophic** | a cold `[[noreturn]]` helper that prints the diagnostic and calls `std::abort()` | — | OOM, hardware fault, corrupt state, FLR failure |

### Contract semantics per TU

The semantic is set by the build system, never in the source. Debug gets the
compiler default `enforce`. Release gets `observe`, which evaluates the clause
and reports through `handle_contract_violation`. The `verify` preset sets
`CRUCIBLE_VERIFY`, and its Release build gets `enforce`. A translation unit
leaves that policy only through `CRUCIBLE_CONTRACT_IGNORE_OPTIONS`, in one of
two ways:

- SECTION 6b at the foot of `CMakeLists.txt` applies it to the four bench TUs
  in `CRUCIBLE_CONTRACT_IGNORE_TUS`, and only in a Release build with
  `CRUCIBLE_BENCH`.
- `crucible_contract_ignore_target()` of `cmake/ContractSemantic.cmake`
  applies it to four test targets in every build. In
  `test/foundation/CMakeLists.txt`, `test_pre_post_cost` measures the ignore
  arm of `CRUCIBLE_PRE`, and `test_swiss_table_buffer_ignore` shows that the
  capacity check and the size check of the Swiss table buffer still abort when
  no contract clause checks. In `test/CMakeLists.txt`,
  `test_crucible_context_migration_ignore` and `test_expr_pool_capacity_ignore`
  run the tests of their source file under the ignore semantic.

`observe` does not mean the program keeps running. P2900 says the handler returns
and execution resumes, but this project's `handle_contract_violation`
(`src/foundation/ContractHandler.cpp`) is `[[gnu::weak, noreturn]]` and ends in
`std::abort()`. A Release binary therefore checks and dies. The handler is weak,
so a program that wants true log-and-continue overrides it with a returning
definition — that is a production failure-policy decision, not a build flag.

```cmake
# CMakeLists.txt SECTION 6 — Release default, INTERFACE on the crucible_dialect target,
# for each consumer without the property CRUCIBLE_CONTRACT_IGNORE
-fcontract-evaluation-semantic=observe

# CMakeLists.txt SECTION 6b — the opt-out list, one line per exempt TU
set_source_files_properties(${CRUCIBLE_CONTRACT_IGNORE_TUS}
  DIRECTORY bench
  PROPERTIES COMPILE_OPTIONS "${CRUCIBLE_CONTRACT_IGNORE_OPTIONS}")
```

In SECTION 6b, the mechanism is the source-file property `COMPILE_OPTIONS`.
CMake puts it last on the compile line, and GCC uses the last
`-fcontract-evaluation-semantic` that it reads. The options of a target come
before the usage requirements of the targets that it links, so they cannot
change the semantic of `crucible_dialect`. `crucible_contract_ignore_target()`
gives the target the property `CRUCIBLE_CONTRACT_IGNORE`, and SECTION 6 gives no
semantic flag to a target with that property. The configure step rejects a
target whose options hold the ignore flag without that property. The test
`contract_semantic` (`utils/scripts/check-contract-semantic.py`) reads the
compile database, and it fails when the last semantic flag of a compile does not
agree with the define `CRUCIBLE_CONTRACT_SEMANTIC_IGNORE`.

Do NOT use `#pragma GCC contract_evaluation_semantic`. No file in `include/` or
`src/` uses it, and it cannot work for this tree. The hot path is header-only,
and a pragma inside a header would silence that header's cold callers along with
its hot ones.

`CRUCIBLE_PRE` and `CRUCIBLE_POST` (`foundation/contracts/Pre.h`, `Post.h`) obey
the same semantic. Their runtime arm is a `contract_assert`, and a Release
library does their check as Debug does. `NDEBUG` has no effect on them.

A translation unit outside that policy gets `CRUCIBLE_CONTRACT_IGNORE_OPTIONS`
from `cmake/ContractSemantic.cmake`. The list sets the flag and the define
`CRUCIBLE_CONTRACT_SEMANTIC_IGNORE`, because GCC gives no macro for the
semantic. With the define, the two macros keep only their consteval trap and
their `[[assume]]` hint. The configure step rejects a target or a source file
that has one item of the list without the other.

`CRUCIBLE_INVARIANT` and `CRUCIBLE_DEBUG_ASSERT` are `NDEBUG`-keyed by design,
and they do no check in Release. `CRUCIBLE_FATAL_INVARIANT` does its check in
every mode, and the semantic has no effect on it.

Test executables compile with `-UNDEBUG` (`test/CMakeLists.txt`,
`test/foundation/CMakeLists.txt`, `test/fixy/CMakeLists.txt`), so those
`NDEBUG`-keyed families are armed in a Release test binary and are not armed in
a Release library or vessel binary. A green Release test run says less about
production than it appears to.

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
compile_kernel(::foundation::effects::Bg const& bg, Arena& arena,
               const KernelNode& k, const TargetCaps& caps)
{
    CRUCIBLE_PRE(k.recipe != nullptr);
    CRUCIBLE_PRE(k.tile != nullptr);
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

### Assertion macro quartet

```cpp
// ── CRUCIBLE_ASSERT ────────────────────────────────────────────
// Boundary precondition. NOT always-on: it expands to a P2900
// `contract_assert`, so the build-system semantic decides whether it
// checks. Debug enforces. Release observes, which reports through a
// handler that aborts. Both therefore end the process on a violation.
// In a TU that takes CRUCIBLE_CONTRACT_IGNORE_OPTIONS, the macro
// compiles to nothing at all.
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
// The debug branch calls foundation::detail::fail_invariant, a cold
// noreturn helper in include/foundation/Platform.h. The helper reads
// the tracer one time: with no debugger it prints the predicate, and
// with a debugger it traps. Then it calls std::abort(). The
// foundation::detail stand-ins exist because libstdc++ 16 declares
// <debugging> and ships no definition for it.
#ifdef NDEBUG
  #define CRUCIBLE_INVARIANT(cond) [[assume(cond)]]
#else
  #define CRUCIBLE_INVARIANT(cond)                                                          \
      do {                                                                                  \
          if (!(cond)) [[unlikely]] {                                                       \
              ::foundation::detail::fail_invariant("invariant", #cond, __FILE__, __LINE__); \
          }                                                                                 \
      } while (0)
#endif

// ── CRUCIBLE_PRE / CRUCIBLE_POST ───────────────────────────────
// Boundary pre/postcondition that fires at consteval AND runtime,
// regardless of return-type shape or whether the predicate
// references `this->` members. Closes the GCC 16.1.1 consteval-
// bypass hole that vanilla P2900 `pre()` / `post (r:...)` leave
// for foldable-bodied functions whose predicates touch class
// members through `this->` (silently bypassed at consteval).
//
// The quartet's "always-fire" rail. The runtime arm is a
// contract_assert, and it obeys the contract semantic of the
// translation unit. Under the ignore semantic the clause becomes
// [[assume(cond)]] for the optimizer. Under static_assert/consteval it
// triggers __builtin_trap(), which is not constexpr and stops the
// surrounding consteval call.
//
// GCC 16 also does not keep the pre or post specifier of a template
// in a header unit or in a precompiled header. So the tree has no
// specifier, and each build rejects one (below).
#define CRUCIBLE_PRE(cond)         /* see foundation/contracts/Pre.h */
#define CRUCIBLE_POST(retvar, cond) /* see foundation/contracts/Post.h */
```

**When to use which:**
- `CRUCIBLE_ASSERT` — public API entry. Contracts handle it.
- `CRUCIBLE_DEBUG_ASSERT` — SPSC ring bounds, arena bump sanity, RNG counter — hot path, can't afford a branch.
- `CRUCIBLE_INVARIANT` — loop trip counts, alignment, range bounds. The optimizer uses it.
- `CRUCIBLE_PRE` / `CRUCIBLE_POST` — each precondition and each postcondition of a function. `CRUCIBLE_PRE` is a statement at the start of the body, and `CRUCIBLE_POST` is a statement before each return. The decide-catalog predicates and the dual-side audit of pre and post use this rail.

**The contract rule.** A P2900 `pre` or `post` specifier on a function declaration is a compile error in each build. `utils/tools/quarantine/plugin_core.h` holds the rule, and the two GCC plugins of that directory apply it. `utils/tools/quarantine/Quarantine.cmake` builds one plugin at configure time and loads it into each C++ compile of the tree. When `CRUCIBLE_QUARANTINE` is `OFF`, the build loads the contract plugin (`contract.cpp`), which applies only this rule. When it is `REPORT` or `ERROR`, the build loads the quarantine plugin, which applies this rule too. The rule applies to each file under the source root, also to `include/foundation/` and `include/fixy/`. The error names `CRUCIBLE_PRE` or `CRUCIBLE_POST`, and two notes give the reasons and the opt-out region. A test of the specifier itself puts the specifier in a `#pragma crucible I_KNOW_WHAT_IM_DOING("reason")` region. No file of the tree uses the region for this rule. `cmake/probes/contract_cache.cpp` holds a specifier, because it is a probe of a compiler fix to the specifier, and `execute_process` compiles it without the plugin. The fixtures `neg_contract_specifier_pre`, `neg_contract_specifier_post` and `neg_contract_specifier_template_member` show the error, and the test `quarantine_plugin` holds each form of the specifier. The plugin cannot see a specifier in a preprocessor arm that the unit does not compile, or on a member function of a local class in a template when the class declares the function and does not define it. `utils/scripts/check-contract-form.py` (the test `contract_form`) reads the parse tree of each tracked C++ file, so it finds these specifiers and a specifier in a file that no build compiles. `utils/scripts/contract-form-allowlist.txt` admits the files whose subject is the specifier, and the list only shrinks. The parse tree does not show a specifier that a macro spells, and the plugin finds that one.

#### VC discharge framing — three layers stack

`CRUCIBLE_PRE` / `CRUCIBLE_POST` are the production-level discharge mechanism for verification conditions (VCs) that the type system cannot statically prove. Three layers stack from cheapest to most expensive:

1. **Type-level proof (always-discharge):** `Refined<bounded_above<8>, uint8_t>` proves at construction that the wrapped value is in [0, 8]. Downstream functions that take `Refined<...>` need NO `CRUCIBLE_PRE` — the type IS the proof. Cheapest, most preferred form.
2. **Named predicate cite (catalog discharge):** `CRUCIBLE_PRE(decide::in_range<uint8_t>(idx, 0, 7))` names one of the 13 predicates in `include/foundation/contracts/Decide.h`, in namespace `foundation::decide`. A search finds each name. When a subsequent change lifts `idx` to `Refined`, that change goes through the predicate name one time.
3. **Anonymous predicate (one-off discharge):** `CRUCIBLE_PRE(p != nullptr && p->ready)` — direct expression, no catalog cite. Use it only for an invariant that no catalog predicate names. Use (2) when you can, because an audit can then count the integer-overflow checks with `grep decide::no_overflow_sum`.

**Dual-side discipline:** each migration to these macros does an audit of both `CRUCIBLE_PRE` and `CRUCIBLE_POST`. Skip post only with documented rationale: tautological (body IS the post), racy (atomic CAS re-read opens TOCTOU), or structurally-not-guaranteed (XOR-collision corner case). See `feedback_pre_post_dual_discipline.md` for the pattern. Three classes of post recur: state-mutation (`state == new_value` after a setter), result-shape (returned value satisfies a structural invariant), lifecycle reset (ctor/init/clear/destroy returns the structure to a documented invariant).

**Two known traps:**
- **Disjunction-vs-implies for null-guarded post:** `decide::implies(p != nullptr, p->status == X)` evaluates BOTH args eagerly under C++ function-call semantics — `p->status` derefs null when p is null. Use C++ short-circuit `||` (`p == nullptr || p->status == X`) when the consequent dereferences a witnessed non-null pointer. See `feedback_decide_implies_eager_eval.md` (UBSan-caught regression on `Tx::activate`, fixed in `9a0fc58`).
- **Consteval-bypass on `this->` member predicates (GCC 16.1.1):** vanilla P2900 `pre()` / `post (r:...)` referencing class members through `this->` silently bypasses at consteval for foldable bodies. This is one reason for the contract rule above. The shim macros use `__builtin_trap()` (non-constexpr) to poison the surrounding consteval call.

Full per-axiom enforcement story for `CRUCIBLE_PRE` / `CRUCIBLE_POST` lives in `include/foundation/contracts/Pre.h` and `include/foundation/contracts/Post.h` docstrings.

### Abort path

```cpp
// include/foundation/permissions/Permission.h
[[noreturn]] CRUCIBLE_COLD inline void shared_permission_pool_outlived_abort_() noexcept {
    std::fputs("crucible: fatal contract violation: a SharedPermissionPool ended while shares were out.  ...\n",
               stderr);
    std::abort();                    // SIGABRT → core dump if enabled
}
```

Each abort site has its own helper of this shape. The tree has no shared abort function. `CRUCIBLE_COLD` expands to `[[gnu::cold, gnu::noinline]]`, so the helper lives in a cold section and does not pollute hot icache. The helper prints one diagnostic and calls `std::abort()`.

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
| `bit_exact_replay_invariant` — `test_bit_exact_replay_invariant`, label `determinism` | DetSafe | Every PR |
| `cross_vendor_step_invariant` — not buildable until two compute backends exist; no compute backend exists at this time, and the CPU oracle is planned too | DetSafe | Release gate (multi-backend) |
| `fleet_reshard_replay` — not buildable until a fleet exists | DetSafe + BorrowSafe | Release gate |
| `bit_exact_recovery_invariant` — planned. No test reloads a stored region into a Vigil and compares a replay. `test_cipher` stores and loads one region, and it compares the content hash and the op count | DetSafe + MemSafe | Release gate |
| `checkpoint_format_stability` — `test_serialize` and `test_serialize_release_gate`, label `determinism`; no `TrainingCheckpoint` type exists, so the DAG wire format is the checkpoint format | DetSafe + LeakSafe | Every PR |
| SPSC rings under ThreadSanitizer — the `tsan` preset runs the full suite. The ring and channel tests that start threads are `test_trace_ring`, `test_trace_ring_pop_batch`, `test_meta_log`, `test_concurrent_rings` and `test_concurrent_channels` | ThreadSafe + BorrowSafe | Every PR (tsan preset) |
| Arena lifetime under ASan — the `default` preset puts ASan on every target of the Debug build. The arena tests are `test_arena`, `test_arena_alloc_class` and, with `CRUCIBLE_FUZZ=ON`, `prop_arena_alloc_invariants` | MemSafe + LeakSafe | Every PR (default preset) |
| Numeric checks under UBSan — the `default` preset puts UBSan on each test executable. The `ubsan-strict` preset puts the full UBSan set on every target, with signed overflow undefined. The numeric tests are `test_saturate_foundation`, `test_saturate_fixy`, `test_saturated`, `test_checked` and, with `CRUCIBLE_FUZZ=ON`, `prop_checked_arith` and `prop_saturate_math_invariants` | TypeSafe + InitSafe | Every PR (default and ubsan-strict presets) |
| Repeat until failure — the `stress` test preset runs the `default` suite with `repeat until-fail:50`. No CI job runs it, and the CI has no nightly schedule | ThreadSafe (race detector) | Manual (nightly run planned) |

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
6. **Every contract has a test that violates it.** The tree has no `EXPECT_DEATH`. Do the violation in a child process (`fork`), and make sure that `waitpid` shows that the child ended on `SIGABRT`.
7. **A death test writes no core dump.** On the build host, `kernel.core_pattern` sends each core dump to systemd-coredump, and the kernel then ignores a core limit of 0. Each dump costs approximately 0.2 s in the test and leaves a core that only root can remove. `utils/scripts/test-launcher.py` gives each executable test a soft core limit of 1 byte, and each child of the test gets it. At that limit, the kernel writes no core dump, and a debugger can still attach. To get the core of a crash, set `CRUCIBLE_TEST_CORES=keep`, and the test keeps the core limit of your environment. A script test gets no launcher, so it must not start a child that aborts.

### Sanitizer preset matrix

```bash
# ASan on every target, UBSan on the tests (the analyzer preset is separate): MemSafe, NullSafe, TypeSafe, InitSafe
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
- Every `CRUCIBLE_PRE` / `CRUCIBLE_POST` has a test that exercises both success AND violation.
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
| Min ARM baseline | **ARMv8.2-A + NEON** | Graviton 2+. Apple cores have 128-byte cache lines, and the build refuses an Apple target (below) |
| Float representation | **IEEE 754** | `-fno-fast-math` enforces |
| Stack size | **8 MB** (Linux default) | Large arrays → arena, not stack |
| Canonical VA bits | **48** (x86-64 without LA57) | Pointer-tagging schemes assume this |
| Thread API | **pthreads** (via `std::jthread`) | Linux only |
| Syscall ABI | **Linux x86_64 / aarch64** | No Windows, no macOS in production |

### Platform checks at build time

`include/foundation/Platform.h` holds the platform floor, and each translation unit of the tree includes that header. Each check stops the build on a target that this section excludes. Its message states the rule and the code to audit before the rule changes. The block that follows shows the checks, with the messages shortened:

```cpp
// Before the first include. On a target outside the floor, a system header can fail first.
#if !defined(__x86_64__) && !defined(__aarch64__)
#error "foundation supports x86_64 and aarch64 only. Before you add an architecture, audit ..."
#endif

#if defined(__APPLE__) && defined(__aarch64__)
#error "Apple aarch64 cores have 128-byte cache lines, and the tree assumes 64-byte lines. ..."
#endif

// After the includes.
static_assert(sizeof(void*) == 8, "foundation supports 64-bit targets only. ...");
static_assert(std::endian::native == std::endian::little, "foundation supports little-endian targets only. ...");
static_assert(__GCC_CONSTRUCTIVE_SIZE == 64, "the tree assumes 64-byte cache lines, and GCC gives a longer line ...");
#if defined(__x86_64__)
static_assert(__GCC_DESTRUCTIVE_SIZE == 64, "the tree separates two shared atomics by 64 bytes, ...");
#endif
```

The two cache-line checks read the interference sizes that GCC gives the target. For each x86 target, GCC sets the two sizes to 64. For an aarch64 core whose GCC tuning gives an L1 line, GCC sets the two sizes to that line. For each other aarch64 target, GCC sets the constructive size to 64 and the destructive size to 256, which names no line. For that reason, the destructive check applies only on x86_64. The checks read the macros, because `-Winterference-size` refuses a use of `std::hardware_destructive_interference_size` in a header.

No macro identifies an Apple core under Linux, and GCC gives no longer line for an Apple core. A Linux build for an Apple core passes the floor, although its cache lines have 128 bytes.

Four negative-compile fixtures in `test/foundation/neg/` show that the checks fire. `neg_platform_unsupported_architecture` and `neg_platform_apple_aarch64` set the architecture macros with `-U` and `-D`. `neg_platform_destructive_interference_size` and `neg_platform_longer_cache_line` set the interference sizes with `--param`. The test gives these flags to the compiler through `CRUCIBLE_NEG_EXTRA_FLAGS` (`test/neg_compile_driver.py`). The compile database then holds a unit of the host, which each guard that preprocesses the database can read. No fixture makes the pointer-size check or the endian check fail, because the host compiler has no 32-bit multilib and no big-endian target.

The two counter reads in `include/fixy/os/Time.h` each keep an `#error` arm. The arm marks the code that must get a counter read for a new architecture.

### When the assumptions change

- **New architecture** (RISC-V, Power): audit every `alignas(64)`, `CRUCIBLE_SPIN_PAUSE`, every counter read in `fixy/os/Time.h`, every endian-sensitive `bit_cast`.
- **Apple Silicon target**: cache line is 128 B. All `alignas(64)` becomes `alignas(CRUCIBLE_CACHE_LINE)` where the macro resolves per platform.
- **ARM 16 KB pages** (Apple, some Android): audit huge-page / `mmap` / `MADV_HUGEPAGE` code.
- **32-bit or 128-bit target**: not supported. Reject.

### Determinism across platforms

Under `BITEXACT_STRICT`, the same IR + same seed produces byte-identical output on any supported platform. This works because:

- IEEE 754 + `RN` rounding + `-fno-fast-math` = deterministic FP
- Philox4x32 is platform-independent (counter-based, bit-exact spec)
- Memory plan offsets are content-addressed (no alloc-order dependency)
- Canonical reduction topology (a UUID-sorted binary tree, planned: no reduction code exists at this time)
- `std::bit_cast` for serialization — no endian-dependent `reinterpret_cast`

The CI test for this property, `cross_vendor_step_invariant`, is planned. It cannot be built until two compute backends exist (§XIII). When it exists, a new platform must pass this test before shipping.

---

## XV. Headers, Includes, and Modules

### Header discipline

1. **`#pragma once`** on every header. No include guards.
2. **Self-contained.** Every header compiles standalone. Add required includes directly; never rely on transitive pull-in. The sentinels of `test/layer/CMakeLists.txt` compile each header of `include/` alone in each build. A crucible header that cannot compile alone has a row in `test/layer/crucible-not-standalone.txt` with its reason. The test `layer_not_standalone` fails when a listed header compiles alone, so the list can only become shorter.
3. **IWYU** (include-what-you-use). If a `.cpp` uses `std::span`, include `<span>` — not via some project header that happens to pull it.
4. **No circular includes.** Refactor: one side gets a forward declaration, full include in the `.cpp` only.
5. **Forward declare in headers whenever possible.** Full definitions only when needed (inline methods, templates, `sizeof`).
6. **No precompiled headers.** PCH hides dependency bugs and complicates CMake. If build is slow, audit headers.

### Compile time

Each translation unit that includes a header compiles that header again, and each negative fixture compiles its headers again on each cold run. These rules keep that cost low. "The budgets of the tree" below gives each budget of the tree in one table: what it measures, its two levels, the check that holds it, and where the check runs.

1. **A header does not check itself in each includer.** A header holds no self-test namespace and no `static_assert` at namespace scope, and it invokes no macro that writes one there. It holds no `static_assert` in the body of a function that is not a template, because the compiler evaluates that check in each includer too. The check file of the header holds them: `test/layer/checks/<layer>/<path>.cpp` for `include/<layer>/<path>.h`. Its first line of code includes its own header, and it keeps each check unchanged, in the same enclosing namespaces. `test/layer/CMakeLists.txt` compiles the check file in each build, in place of the sentinel of the header. A walk in a check file can read a namespace that another header adds to, so `test/layer/CMakeLists.txt` compiles some check files a second time, in the walk units of `test/layer/walks_across_headers.cpp`, after each header that can add to such a namespace. `test/layer/walk-checks.txt` lists these check files: each check file whose checks can call `members_of` on the reflection of a namespace that a header opens. `utils/scripts/check-walk-units.py` writes the list from the parse tree, and the test `walk_units` fails when the list and the tree disagree. A `static_assert` inside a class or a template stays in the header, because it applies to each instantiation. `utils/scripts/check-header-checks.py` (the test `header_checks`) enforces the rule. `utils/scripts/header-checks-ledger.txt` holds the checks that have not moved. Each of them gives a warning on each run, and the counts can only decrease. A check whose result depends on the translation unit that includes the header stays, with a keep row and its reason.
2. **Compile-time tables are lazy.** A reflection walk, a roster or a table that not every includer reads is a variable template, or it is inside a template. Then only a translation unit that reads it evaluates it. The initializer of such a variable template must depend on the template parameter, as `make_gf_tables<Field>()` in `include/crucible/cntp/Fec.h` does. GCC 16 evaluates a consteval initializer that does not depend on the parameter where the template is declared, and no operation limit reports that cost. A non-template inline function that reads the table also makes each includer evaluate the table. A call inside the template that does not name the parameter can have the same effect: the call `make_nibble_tables(coeff)` in the AVX2 arm of `Fec.h` made each includer evaluate the table. So `Fec.h` names the parameter in each such call. The test `header_checks` reads the parse tree and finds each reflection walk outside a template, because the operation count of GCC does not see it. A reflection query is a call of a `std::meta` or `std::define_static_*` function, a call that takes a `^^X` reflection, or a splice in a loop. The test reports a query in the initializer of a constexpr, constinit or const variable or static data member, and in the type of an alias. In a function body, it reports a query in a constexpr local, and each expansion statement. A plain call, a cheap constant and a `^^X` alone are no finding. Each walk in the ledger gives a warning, and a new one is an error. For a plain call, the compiler is the check: each header also compiles alone in a one-line unit at the operation limit of the row `header-constexpr-ops`. `all` compiles the units of the headers that have no check file, and the target `layer_alone`, which the default leg and the release leg of CI build, compiles the others. The release leg compiles the SIMD arms that `-march=native` selects. A row of `test/layer/header-constexpr-ops.txt` gives one header a higher limit with its reason, and the test `header_constexpr_ops` refuses a row that its header no longer needs.
3. **No heavy inline body in a header.** Each includer analyzes a non-template inline function again. A body that is cold or large goes to a source file.
4. **Builtins before headers.** A base header does not include an intrinsics header, `<thread>` or `<chrono>` for one function. Use the builtin, for example `__builtin_ia32_pause`, `__builtin_ia32_rdtsc`, `__atomic_load_n` or `__builtin_memcpy`. `utils/scripts/check-heavy-includes.py` (the test `heavy_includes`) rejects a new include of a heavy standard header in a header under `include/`. A standard header is heavy when its time alone, in `utils/scripts/header-costs.txt`, is more than the time of `<meta>` alone times the factor of the row `heavy-include`. `utils/scripts/check-compile-cost.py` (the test `header_alone`) adds the bytes that each header of `include/` includes when its sentinel compiles it alone, from the ninja dependency log. It reads the row of the layer of the header, and the rows of the base layers are the tightest, because each translation unit of the tree reads the base headers. The SIMD arms of `crucible/SwissTable.h`, `crucible/cntp/Fec.h` and `crucible/ledger/probes/VectorWidth.h` call GCC builtins, and no header includes an x86 intrinsics header. So the Debug build and the Release build give each header the same bytes alone. Only an aarch64 arm includes `<arm_neon.h>`. `utils/scripts/heavy-includes-ledger.txt` keeps a row for each of its two includes, because no aarch64 compiler is on the build host to test builtins in their place.
5. **No giant translation unit and no giant function.** A generator writes its output as shards. Split a test file that compiles for more than 60 s at `-j1` by subject. Split a function that is large enough to make the debug-information passes explode. The build launcher (§V) writes the CPU time and the peak memory of each compile and each link, and the user instruction count of each compile, beside the output, and the tests of the table judge these records and the objects. The tests do not judge a ccache hit. The CPU time rises with the load of the host, and the instruction count of `utils/scripts/cost_meter.py` does not. So when a record holds a count, `compile_instructions` holds the error level of the compile, and `compile_cpu` gives a warning only.
6. **No unity build and no precompiled header.** The reflection walks of this tree read each header that a translation unit includes, so a unity build or a precompiled header can change a result. `utils/scripts/check-no-unity-pch.py` (the test `no_unity_pch`) rejects each one, in the compile database of the build and in the CMake files of the tree.
7. **No padding bit in the element of a large fixed list.** With `-ftrivial-auto-var-init=zero`, GCC writes one store for each padding hole of each element of a fixed list in an automatic object, with no loop. Make each padding byte a member, as `include/crucible/ledger/Verdict.h` does with `pad`, and call `expect_no_padding_byte` of `test/padding_bytes.h` in a test of the type. `utils/scripts/check-padded-lists.py` (the test `padded_lists`) rejects a new fixed list of more elements than the row `padded-list` of `utils/scripts/budgets.txt` permits, when its element type has a padding bit. `utils/scripts/padded-lists-ledger.txt` holds the padded lists that the tree has today, and each one gives a warning.
8. **A second configure compiles nothing.** A configure run writes a generated file or a link only when its content or its target changes. The compiler launcher gives ccache the source tree as `base_dir`, so a cache hit records no dependency path of another build directory. `utils/scripts/check-reconfigure-noop.py`, a step of the CI build job, configures a build again and requires that the two builds after it compile nothing and that each dependency in the log of Ninja exists. Its self-test is the test `reconfigure_noop_self_test`.
9. **A cold fixture compile is fast.** A fixture compiles cold on its first run and after each change of a file that it reads. `test/neg_compile_driver.py` measures the user CPU time and the user instructions of each such compile against the rows `fixture-cpu` and `fixture-instructions`, and the store entry keeps the measures. The instruction count holds the error level when the compile has one, and the CPU time then gives a warning only. A fixture writes its warnings to its own files, `fixture-cpu.NAME.txt` and `fixture-instructions.NAME.txt`, in the warnings directory. After each run, the driver writes `NAME.inputs` with the files that the compile read, for the totals of rule 16. The parts of `fixture_store_self_test` test the three levels of each row and the record.
10. **A constant evaluation is small.** Each translation unit and each negative fixture compiles with `-fconstexpr-ops-limit` at the error threshold of the row `constexpr-ops`, the GCC default of 33,554,432 operations. `cmake/Budgets.cmake` reads the row for the build, and the session oracle and the atom roster guard read the same row. A translation unit that needs more takes a source-file `COMPILE_OPTIONS` property with its reason, beside the flag in `CMakeLists.txt`. Compile-time code in a loop does not change a `std::vector` in each step. In a constant evaluation of this GCC 16 build, one `push_back` with one `pop_back` costs about 455,000 instructions, a read through `operator[]` about 37,000, and a read through a pointer about 10,500. Use a pool that only grows, or a fixed array, and read it through a pointer. Commit 6f70a05fe did this in the bounded asynchronous subtype search, and the slowest subtype shard fell from 88 G to 36 G instructions with the same answers.
11. **A test is fast and small.** `cmake/TestLauncher.cmake` sets `CMAKE_TEST_LAUNCHER`, so `utils/scripts/test-launcher.py` measures the wall time and the peak memory of each test whose command is an executable target. A test that passed fails over an error threshold, unless a ledger row admits it for the kind of the build. A test writes its warnings to `ROW.TEST.txt` in the warnings directory. In a TSan build, a wall time over the error threshold gives a warning only. CMake gives no launcher to a script test, so `utils/scripts/check-test-time.py`, a step of the CI build job, reads the wall time of a script test from the JUnit report of the run.
12. **Each test preset has a timeout.** Each test preset of `CMakePresets.json` sets a timeout for each test, as the hard stop. Never raise a timeout to make a slow test pass.
13. **A time budget only warns on a GitHub runner.** With `GITHUB_ACTIONS=true`, `utils/scripts/cost_meter.py` changes each time error to a warning and sets no CPU limit, because the thresholds apply to the 384-thread build host.
14. **The inner loop.** Use the `default` preset and its build directory `build`. The command `cmake --build build` builds the tree. The command `python3 utils/scripts/run-affected-tests.py build -j N` builds the tree too. Then it runs each negative fixture, each script test, and each executable test with an input that changed after its last pass. It prints the number of tests that it skipped and the reason, and it reports no skipped test as passed. CI and the checks before a landing run the full suite with ctest, and the test `affected_tests_self_test` holds the script to its rules.
15. **ccache hashes the preprocessed text.** The launcher of ccache in the root `CMakeLists.txt` sets `depend_mode=false`. When the hashes of the included files give no hit, ccache hashes the output of the preprocessor. Then a change of a comment on its own line in a header gets a hit, and a change of code pays one more run of the preprocessor. The result store of the negative fixtures keys on the bytes of each file, so each fixture that includes the header compiles again.
16. **The totals stay small.** Each budget of one job can pass while the sum of all jobs increases, for example through one more include in a base header. On 192 cores the wall time of a build or a test run is at least its total CPU time divided by 192. `utils/scripts/check-parse-cost.py` adds, over each object of the target `all` and each negative fixture, the bytes that its compile reads, from the ninja dependency log and from the record of the fixture driver. The test `parse_total` compares the sum with the baseline of the build kind in `utils/scripts/parse-total-ledger.txt`, and it names the files whose size times readers changed the most. To accept a growth, raise the baseline in the same commit with `--write --reason TEXT`. A fall over the warning threshold also needs a new baseline in the same commit, with `--write`. Then the baseline does not stay above the sum. The test `header_fanout` prints the ten headers whose bytes alone times readers are the largest, the list of the headers that cost the build the most. The test `instruction_total` adds the instructions of each compile, and `utils/scripts/report-check-warnings.py` adds the CPU time of the compiles and of the executable tests. These totals give warnings only.

### The budgets of the tree

`utils/scripts/budgets.txt` holds the two thresholds of each budget with a row. A value over the warning threshold gives a warning, which does not fail. A value over the error threshold gives an error, which fails the test or the step. Change a threshold only with a measurement, and give the numbers in the commit. A ledger `utils/scripts/<check>-ledger.txt` admits an item over its error threshold with a reason, and the item gives a warning on each run, so each admitted item stays in the output. A check writes its warnings to `<build>/check-warnings`, because ctest shows no output of a test that passes. `utils/scripts/report-check-warnings.py BUILD_DIR` prints them, and the CI step "Check warnings" runs it after the tests. Each test of the table has the label `ci_guard`, so it runs in each local test run and in the step "Test" of the CI build job. On a GitHub runner, each error of a time row is a warning (rule 13).

| Budget | What it measures | Warning | Error | Held by | Runs in |
|---|---|---|---|---|---|
| `compile-cpu` | The user and system CPU time of one compile job | 10 s | 20 s. A warning only when the record holds an instruction count. The launcher stops a job at 60 s | The build launcher and the test `compile_cpu` | Build, test run |
| `compile-instructions` | The user instructions of one compile job | 55 G | 110 G | The test `compile_instructions` | Test run |
| `compile-memory` | The peak memory of one compile job | 2 GB | 4 GB. The launcher fails the job | The build launcher and the test `compile_memory` | Build, test run |
| `link-time` | The user and system CPU time of one link | 2 s | 5 s. The launcher stops a link at 15 s | The build launcher and the test `link_time` | Build, test run |
| `link-memory` | The peak memory of one link | 0.5 GB | 1 GB. The launcher fails the link | The build launcher and the test `link_memory` | Build, test run |
| `function-size` | The largest function of one object | 64 KB | 256 KB | The test `function_size` | Test run |
| `object-text` | The machine code of one object | 512 KB | 768 KB | The test `object_text` | Test run |
| `header-alone-foundation`, `header-alone-fixy`, `header-alone-crucible` | The bytes that one header of the layer includes alone | 3.25, 6 and 7 MB | 3.5, 6.5 and 7.35 MB | The test `header_alone` | Test run |
| `heavy-include` | The time alone of a standard header that a header of `include/` includes, as a multiple of the time of `<meta>` | Each ledger row | More than 1.25 | The test `heavy_includes` | Test run |
| Header self-checks | The self-test namespaces and the namespace-scope checks of a header (rule 1) and its reflection walks (rule 2) | Each ledger row | A new one | The test `header_checks` | Test run |
| `header-constexpr-ops` | The operations of one constant evaluation in a header that compiles alone | None | 1,000,000 | The limit flag of the one-line units of `test/layer`, and the test `header_constexpr_ops` | Build (`layer_alone` in the default leg and the release leg of CI) |
| `constexpr-ops` | The operations of one constant evaluation in a translation unit or a fixture | None | 33,554,432 | The limit flag of each compile | Build, test run |
| `padded-list` | The elements of a fixed list whose element type has a padding bit | Each ledger row | More than 63 | The test `padded_lists` | Test run |
| Unity build and precompiled header | A unity build or a precompiled header in the tree | None | Each one | The test `no_unity_pch` | Test run |
| Re-configure | The compiles after a second configure | None | One compile | `utils/scripts/check-reconfigure-noop.py` | CI build job |
| `test-time` | The wall time of one test | 5 s | 30 s. A warning only in a TSan build | The test launcher, and `utils/scripts/check-test-time.py` for a script test | Test run, CI build job |
| `test-memory` | The peak memory of one executable test | 0.5 GB | 1.5 GB | The test launcher | Test run |
| Test timeout | The wall time of one test | None | The timeout of the test preset stops the test | ctest | Test run |
| `fixture-cpu` | The user CPU time of one cold fixture compile | 5 s | 15 s. A warning only when the compile has an instruction count | The fixture driver | Test run |
| `fixture-instructions` | The user instructions of one cold fixture compile | 28 G | 45 G | The fixture driver | Test run |
| `parse-total` | The bytes that the compiles of the objects of `all` and of the fixtures read, against the baseline | A growth over 2 % | A growth over 5 %, or a fall over 2 % | The test `parse_total` | Test run, after the fixtures |
| `header-fanout` | The bytes alone times the readers of one header, against the baseline, and an entry into the ten largest | A change over 10 % | None, warnings only | The test `header_fanout` | Test run, after the fixtures |
| `instruction-total` | The user instructions of the objects of `all` and of the fixtures, against the baseline | A change over 2 % | None, warnings only | The test `instruction_total` | Test run, after the fixtures |
| `total-compile-cpu` | The CPU time of the last real compile of each object of `all`, added | 3,000 s | None, warnings only | `utils/scripts/report-check-warnings.py` | CI step "Check warnings" |
| `total-fixture-cpu` | The CPU time of the last real compile of each fixture, added | 3,000 s | None, warnings only | `utils/scripts/report-check-warnings.py` | CI step "Check warnings" |
| `total-test-cpu` | The CPU time of each executable test in its last run, added | 120 s | None, warnings only | `utils/scripts/report-check-warnings.py` | CI step "Check warnings" |

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

- All code is in one of three root namespaces, or in a namespace nested in one of them. `foundation` holds `include/foundation/` (`foundation::permissions`), `fixy` holds `include/fixy/` (`fixy::session`), and `crucible` holds the runtime (`crucible::mimic::nv`, `crucible::forge::phase_d`).
- **No** `using namespace std;` at file or namespace scope, ever. Inside function body OK when the benefit is clear.
- Anonymous namespaces for TU-local helpers. Not the `static` keyword (deprecated for this purpose).
- Don't use `using X::foo;` to leak implementation details from a nested namespace to a parent.

### C++20 Modules and precompiled headers — not used

The tree uses `#include` only. A measurement on 2026-10-01 tried a precompiled header for each include prefix of the negative fixtures, and C++20 header units with include translation for the whole tree. Both changed results, so neither landed:

- GCC 16 drops the native `pre` and `post` specifiers of a function template and of a member of a class template when it writes a header unit or a precompiled header. This is one reason for the rule that the tree uses `CRUCIBLE_PRE` and `CRUCIBLE_POST` only. Each build holds the rule (§XII "The contract rule"). The test `contract_header_unit` shows the defect, and it fails when a compiler keeps the specifier.
- A header unit does not see what its includer declares before the include, so an eager seal check in a header stops working. `source_location_of` can also name a different file.
- GCC 16 has many module defects: internal compiler errors, a module file that GCC cannot read again in C++26 mode, a lost `= delete("reason")` text, and template instances that do not merge.

Header units cut the CPU time of the fixtures by about 4.5 times, so try them again with GCC 17. Check the reflection results and the contract results first.

### One-definition rule (ODR) discipline

- `inline` on every function in a header (including single-line getters).
- `inline` on every `constexpr` variable in a header.
- Template specializations: inline + in a header.
- Anonymous namespace for TU-local: never in a header.

---

## XVI. Safety Wrappers

These library types make the axioms from §II true at compile time. They are in `include/fixy/` and in `include/foundation/permissions/`. Most wrappers add no cost at run time: `sizeof(Wrapper<T>) == sizeof(T)`, and under `-O3` the machine code is that of the bare primitive. A wrapper whose lattice stores its grade per instance, such as `fixy::RecipeSpec<T>`, adds the size of that grade.

### Header catalog

| Header | Axioms it enforces | Role |
|---|---|---|
| `fixy/Qtt.h` | MemSafe, LeakSafe, BorrowSafe | Move-only `Linear<T>`. `.consume() &&` takes ownership; `.peek() const&` borrows. Construction is `[[nodiscard]]`. |
| `fixy/Refined.h` | InitSafe, NullSafe, TypeSafe | `Refined<Pred, T>`. The mint `mint_refined` checks the predicate with a precondition that obeys the contract semantic. Downstream bodies trust the invariant. |
| `fixy/Secret.h` | DetSafe + information-flow discipline | Classified-by-default `Secret<T>`. Escapes only via `declassify<Policy>()` with a grep-able `secret_policy::*` tag. |
| `fixy/Tagged.h` | TypeSafe | Phantom tags in `fixy::tags` for provenance (`source::FromUser`, `source::FromDb`, `source::FromInternal`) and trust (`trust::Verified`, `trust::Unverified`). Mismatch at call sites = compile error. |
| `fixy/session/Handle.h` | BorrowSafe | `SessionHandle<Proto, Resource>`: type-state protocol channels. Each `.send()` / `.recv()` returns a new type carrying the remaining protocol. Wrong order or missing step = compile error. State lives in the type; zero runtime cost. |
| `fixy/Checked.h` | TypeSafe, DetSafe | `checked_add` / `wrapping_add` / `trapping_add` over `__builtin_*_overflow`. `saturating_add` / `saturating_sub` / `saturating_mul` call `foundation::sat::add_sat` / `sub_sat` / `mul_sat`, which call `std::saturating_*` only on overflow. |
| `fixy/Mutation.h` | MemSafe, DetSafe | `AppendOnly<T>` — no erase/resize. `Monotonic<T, Cmp>` — advance-only with contract guard on the step. |
| `fixy/ConstantTime.h` | DetSafe (side-channel resistance) | `ct::select`, `ct::eq`, branch-free primitives for crypto paths and Cipher key handling. |
| `foundation/permissions/Permission.h` | BorrowSafe, ThreadSafe, MemSafe | `Permission<Tag>` — phantom-typed move-only token (sizeof = 1, EBO-collapsible) encoding CSL frame rule. `SharedPermission<Tag>` + `SharedPermissionPool` for fractional read sharing (atomic refcount + mode upgrade). `ReadView<Tag>` in `ReadView.h` for lifetime-bound borrows. Factories: `mint_permission_root`, `mint_permission_split`, `mint_permission_combine`, `mint_permission_split_n`, `mint_permission_combine_n` and `mint_permission_share`. |
| `foundation/permissions/PermissionFork.h` | ThreadSafe, BorrowSafe | `mint_permission_fork<Children...>(ctx, parent, callables...)` is the CSL parallel composition rule, as a fork-join with one `std::jthread` for each child. Its constraint is `CtxFitsPermissionFork<Ctx, Parent, Children...>`, which asks for `can_split_into_pack_v<Parent, Children...>` and a context that has `Effect::Bg`. `mint_permission_fork_inline` does the bodies inline in child order. Each mint gives back the parent permission after all bodies complete. |

Every header is self-contained. Two cold bodies are in the library `foundation`, so
that an includer does not compile them: the abort of `Permission.h` in
`src/foundation/Permission.cpp`, and the thread start of
`foundation/permissions/ForkTasks.h` in `src/foundation/PermissionFork.cpp`. Two
cold bodies are in the library `fixy`: the sysfs probe of
`fixy/concurrent/Topology.h` in `src/fixy/concurrent/Topology.cpp`, and the door
of `fixy/os/ThreadTasks.h` in `src/fixy/os/ThreadTasks.cpp`. The door starts the
stage threads of `fixy/concurrent/Pipeline.h` and the workers of
`fixy/os/Spawn.h`, and it calls the thread start of `ForkTasks.h`, so the tree
has one copy of that body.
The other headers are header-only. The dependency rule is the layer
rule, and `utils/scripts/check-layer-boundary.py` enforces it: `foundation` names only
`foundation` and `std`, `fixy` names `foundation`, `fixy` and `std`, `crucible`
names anything below it.

**There is no line cap**, because a line count cannot tell a table from a
god-header. Measured 2026-09-29: `foundation/diag/Catalog.h` is 1,918 lines at
5% comment, and it is a catalog. It is long by nature, and a split on a line
count would cut a table in an arbitrary place. `foundation/permissions/Permission.h`
is 1,730 lines, with 1,111 lines of code and 24 includes, 12 of them project
headers. It spans `Permission`, `SharedPermission`, the pool, the `ReadView`
interaction, brands, `permission_row_t` and the splits, which is a real
multi-concern header. The two headers have a similar size and deserve opposite
verdicts, so the instrument that ranks them equal is the wrong one.

For scale, `fixy/Collision.h` is 2,523 lines and `fixy/Refined.h` is 1,879. The
headers of `include/foundation/` and `include/fixy/` are 29% comment, so their
size is code and not prose. Header size is a review question about concerns, not
a gate. `Permission.h` is
the one header whose growth is a standing concern, and a split decided on
concerns rather than lines is owed.

### Usage rules

1. **Public API params wrap raw primitives.** `fn(Refined<positive, int> n)` — never `fn(int n)`. Bodies then trust the invariant without re-validating.
2. **Every resource type wraps in `Linear<T>`** — file handles, mmap regions, TraceRing, channel endpoints, arena-owned objects with drop semantics.
3. **Every load-bearing predicate gets a named alias** — `PositiveInt`, `NonNullTraceEntry`, `ValidSlotId`, `NonEmptySpan<T>`. Not anonymous refinements at call sites.
4. **Every classified value wraps in `Secret<T>`** — Philox keys, Cipher encryption keys, private weights, credentials. Declassification requires a `secret_policy::*` tag.
5. **Every trust-boundary crossing uses `Tagged<T, source::*>`** — deserialized input, network payload, FFI return. An API that takes only sanitized input asks for `source::FromInternal`.
6. **Every fixed-order protocol uses `SessionHandle<...>`** — handshakes, init sequences, channel lifecycles, plan-chain acquisition.
7. **Every append-only or monotonic structure wraps** in `AppendOnly<>` / `Monotonic<T, Cmp>` — event logs, generation counters, version numbers, Cipher warm writes.
8. **Every crypto path uses `ct::*` primitives** for comparisons and selections. Non-CT code in a `with Crypto` context is a review reject.
9. **Every concurrent producer/consumer endpoint wraps in `Permission<Tag>`** — handles holding a Permission are `Pinned`; cross-thread handoff goes through `mint_permission_fork` (structured concurrency), `SharedPermissionPool::lend()` (refcounted shared read), or move-into-`std::jthread`-lambda (single owner). Never a raw `std::thread` without a Permission token; never two threads simultaneously calling the same `try_push` on a shared queue without a Permission split. The cache-tier rule (§IX) decides whether to actually parallelize — Permissions just prove the access pattern is sound.

### Compiler enforcement

- `-Werror=conversion` + the wrapper types together prevent accidental unwrapping across boundaries.
- `Linear<>`'s deleted copy constructor refuses a second copy of a linear value at compile time. GCC 16 has no `-Wuse-after-move` (it rejects `-Werror=use-after-move` as an unknown option), so the language does not see a second `std::move` of one local. The `use_after_move` ci_guard (`utils/scripts/check-use-after-move.py`) is the enforcement: it refuses any use of a local after `std::move`, on any path, across the tree. It catches `Permission<Tag>` double-use after split/fork, and it lists what it cannot see (a use through a pointer or a reference, a move inside a callee that takes `T&`).
- `[[nodiscard]]` on every wrapper type's constructor forces the caller to capture the return value.
- Contracts on `Refined<>` and `Monotonic<>` constructors fire at construction sites under `semantic=enforce` (Debug) and `semantic=observe` (Release, through a handler that aborts). Under `semantic=ignore` (CRUCIBLE_CONTRACT_IGNORE_OPTIONS) they compile to `[[assume]]` hints, optimizing downstream code as if the invariant always holds.
- Deleted copy + defaulted move on `Linear<>` / `Secret<>` / `SessionHandle<>` / `Permission<Tag>` means the compiler rejects accidental duplication.
- `mint_permission_split`, `mint_permission_combine`, `mint_permission_split_n` and `mint_permission_combine_n` have a `static_assert` on `can_split_into_v` or `can_split_into_pack_v` in their bodies. `mint_permission_fork` has `can_split_into_pack_v` in its constraint `CtxFitsPermissionFork`. A split into subregions that no manifest declares is a compile error.
- A contract violation ends the process: the handler in `src/foundation/ContractHandler.cpp` calls `std::abort()`. A violation never causes undefined behavior (P1494R5).

### Review enforcement

Rules for code review and grep-guards:

- `declassify<` without a `secret_policy::*` policy tag → reject.
- `const_cast` → reject (banned per §III).
- `reinterpret_cast` → reject; use `std::bit_cast`.
- `std::chrono::system_clock` → reject (use `steady_clock` or `rdtsc`).
- A new public API taking raw `int`, `size_t`, `void*`, or `T*` without a wrapper → questioned on review; almost always rewritten.
- A new resource-carrying type without `Linear<>` → questioned; must have justification.
- Any `[[unlikely]]` body of more than 8 non-trivial lines without being outlined into a `CRUCIBLE_COLD` helper → reject.
- A new concurrent producer/consumer pair without a `Permission<Tag>` discipline → questioned; bare `std::thread` + raw atomic SPSC is an old-style pattern; new code uses `Permission<Tag>` for the static safety + `mint_permission_fork` for handoff.
- A `mint_permission_root<X>()` call site outside `main()` / a Vessel/Keeper init function → reject; root-mint is once-per-program-per-tag and review-discoverable via `grep mint_permission_root<` exactly because of this rule.
- A new `can_split_into<...>` or `can_split_into_pack<...>` specialization in a header far from its tag tree's declaration → questioned; the manifest belongs in the same TU as the tags so reviewers see the whole region tree at one glance.
- A `Permission<Tag>` stored in a struct field of a type that is itself shared between threads (i.e., not Pinned + not handle-pattern) → reject; defeats linearity.
- Bypassing `ParallelismRule` (through `mint_parallel_for` or `mint_spawn`) to spawn N raw threads when working set is L2-resident → questioned; cache-tier rule (§IX) says sequential wins. Override requires bench evidence and a justification comment.

### How a graded wrapper is built

A grade is a claim about the value, so each door that attaches a grade to a value takes a key. `foundation::algebra::grade_key<Authority>` in `include/foundation/algebra/Graded.h` is that passkey. Its constructors are private and `Authority` is its one friend, so only a member of `Authority` can build a key. Three doors of `Graded` take the key: the two-argument constructor, `inject` of a relative monad, and the keyed `peek_mut`. A search for `grade_key<` therefore lists every site that attaches a grade.

Some doors take no key. If the grade of a lattice says nothing about the bytes, the lattice opens `peek_mut`, the default constructor and `at_bottom()` with no key. Where the grade is the value, or `grade_of` calculates it from the value, the constructor from the value alone takes no key.

A band is an alias over `Graded<Absolute, L::At<Tier>, T>` in `include/fixy/Bands.h`, for example `fixy::DetSafe<Tier, T>` or `fixy::NumericalTier<Tolerance, T>`. The one door of a band is `fixy::mint_band<Band>(value)`. `Band` must name the band exactly, and the authority of the key is a class local to the mint. A search for `mint_band<` therefore lists every site that sets a tier. `fixy::relax<WeakerTier>(band)` moves a band down its lattice and never up.

`fixy::RecipeSpec<T>` is not a band. It stores the tolerance tier and the reduction family at run time, and its one door is `fixy::mint_recipe_spec(value, tier, family)`.

### Canonical wrapper-nesting order

Composition is **wrapper-nesting**, not one large product lattice. Each `Graded<Modality, Lattice, T>` instantiation is one algebraic slice, and a stack of wrappers puts several axes together, outer to inner. The order is canonical, because nesting is order-sensitive: `Stale<Tagged<T>>` and `Tagged<Stale<T>>` are two different types. The row hash folds along the stack, so a stack in a different order compiles but gets a different cache slot. `include/fixy/CanonicalOrder.h` states the order. A site that must have that order uses the concept `fixy::canonical_order::CanonicallyOrdered<Stack>` as a constraint. Review asks why a stack has a different order, unless a comment gives the reason for the separate cache slot.

```
HotPath ⊃ DetSafe ⊃ NumericalTier ⊃ Vendor ⊃ ResidencyHeat ⊃
  CipherTier ⊃ AllocClass ⊃ Wait ⊃ Stale ⊃ Tagged ⊃ Refined ⊃
  Secret ⊃ Linear ⊃ Computation
```

Outer wrappers carry "higher-level" properties (where in the system this runs, what tier it serves); inner wrappers are "closer to the value" (provenance tags, refinement predicates, classification, ownership). Reading example bottom-up: the value `T` is wrapped in `Computation<Row, T>` to declare its OS-effect row, then in `Linear<>` to declare exclusive ownership, then in `Secret<>` to mark as classified, and so on outward. Reading top-down: `HotPath<Hot, ...>` says "this lives on the hot path", and the rest of the stack refines what kind of hot-path value.

Worked example — a tensor that comes back from a Bg-context kernel, BITEXACT, NV vendor, hot-path:

```cpp
fixy::HotPath<fixy::HotPathTier_v::Hot,
    fixy::DetSafe<fixy::DetSafeTier_v::Pure,
        fixy::NumericalTier<fixy::Tolerance::BITEXACT,
            fixy::Vendor<fixy::VendorBackend_v::NV,
                foundation::effects::Computation<foundation::effects::Row<foundation::effects::Effect::Bg>,
                                                 ResultTensor>>>>>
```

Each band has a one-element grade, and the carrier `Computation<R, T>` adds no storage to `T`. The five-deep nest therefore has the size of `ResultTensor`.

**F\*-style named aliases** in `include/fixy/Aliases.h` give names to the common compositions. `Pure<T>` is `DetSafe<DetSafeTier_v::Pure, Computation<PureRow, T>>`, and `Tot<E_os, T>` is `DetSafe<DetSafeTier_v::Pure, Computation<E_os, T>>`. Use the aliases at production call sites, because the full stack is long for everyday code.

**Order-discipline summary:**

1. Wrapper authors construct stacks in canonical order. Deviations question on review unless commented with a deliberate cache-slot-separation rationale.
2. The fold mixes the salt, the modality and the lattice identity of each layer with the contribution of the layer inside it. The combiner is order-sensitive, so `W<X>` and `X` fold to different hashes, and `W1<W2<T>>` and `W2<W1<T>>` fold to different hashes too.
3. `Computation<R, T>` is the innermost member of every effect stack. It is the carrier, and each other layer is metadata about it. Its specialization in `foundation/diag/RowHash.h` folds the row `R` first and the payload `T` second.
4. **Append-only extension of the effect enum.** A new effect atom (for example `Effect::Refute`) takes the next free position, and no existing position changes. Cache invalidation then touches only the entries that contain the new atom. `Row<Effect::Bg>` keeps its hash, because the underlying value of `Effect::Bg` does not change.

`Permission<Tag>` is not itself a `Graded` wrapper and does not enter the
wrapper-nesting stack. `permission_row_t<Tag>` connects effectful ownership to
Met(X) rows instead (§IX). A tag whose row is `Row<>` can mint without a context.
A row-bearing tag needs ctx-bound mints, transfers, splits, shares and pool
borrows, and the `ExecCtx::row_type` of that context must admit the tag row.

**How the row hash folds.** `include/foundation/diag/RowHash.h` holds one fold for every graded wrapper, so no wrapper has a specialization of its own. The fold reads the shape that a wrapper publishes: `modality`, `lattice_type` and `value_type`. It mixes a salt and the modality with the canonical identity of the lattice. Then it mixes the result with the contribution of the payload, which recurses into the next layer. A wrapper that also publishes `row_discipline`, such as a sealed refinement, folds that identity between the lattice and the payload. A bare type contributes zero.

Other carriers have folds of their own in the same header. The effect row, `Computation<R, T>`, a capability context, `ExecCtx` and `Capability` each have a specialization. A carrier that publishes `row_discipline` and `row_payload` goes to the discipline fold. A session handle publishes the Stepping modality and goes to the stepping fold. The multi-axis binding in `fixy/Fn.h` has its own specialization.

The lattice identity is a reflected name, so the graded fold is not portable across toolchains. Peers that can have different toolchains use `federation_key_with_toolchain<T>()` for their keys. `test/fixy/test_row_hash_wrappers.cpp` reads the carrier roster by reflection. It gives an error for each wrapper that folds to zero with no stated reason. `test/foundation/test_row_hash.cpp` holds the algebra of the fold. `utils/tools/dump_row_hashes.cpp` with its committed golden is the cross-build witness.

### GCC 16 contracts — implementation gotchas

Real-world issues encountered implementing the wrappers.  Document them here so the next person doesn't rediscover them.

**The tree has no `pre` or `post` specifier.**  Each build rejects one (§XII "The contract rule").  So the rules of the specifier syntax do not apply here: the place of a specifier after `noexcept`, and the `const` that a value parameter needs when a postcondition reads it.  `CRUCIBLE_POST` is a statement of the body, and its condition can read each parameter.

**`-fcontracts` and `-freflection` require `-std=c++26`.**  CMake's compiler-probe step runs before the project's `CMAKE_CXX_STANDARD` takes effect, so putting these flags in `CMAKE_CXX_FLAGS` via the preset breaks configuration.  The root `CMakeLists.txt` sets them at target level instead, with `target_compile_options(crucible_dialect INTERFACE -freflection -fcontracts)` after `project()` declares the standard.

**`handle_contract_violation` must be defined by the program.**  GCC 16 / libstdc++ 16 does not ship a default handler.  Every program that enables contracts must provide one; otherwise the link fails with `undefined reference to handle_contract_violation(std::contracts::contract_violation const&)`.  The project default is `src/foundation/ContractHandler.cpp`, a weak definition that ends in `std::abort()`.

```cpp
#include <contracts>
void handle_contract_violation(const std::contracts::contract_violation& v) {
    fprintf(stderr, "contract: %s\n", v.comment());
    std::abort();
}
```

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

**Runtime libstdc++ resolution.**  Binaries compiled with the local GCC 16 tree link against that tree's `libstdc++.so.6` which is newer than the system's.  Without an rpath, they fail at runtime.  `cmake/Toolchain-gcc16.cmake` sets `CMAKE_EXE_LINKER_FLAGS_INIT`, `CMAKE_SHARED_LINKER_FLAGS_INIT` and `CMAKE_MODULE_LINKER_FLAGS_INIT` to an rpath that it calculates from the GCC prefix: `<prefix>/usr/lib64` and `<prefix>/lib64`. To make sure of the rpath, use `readelf -d build/test/test_X | grep RUNPATH`.

### What the wrappers do not cover

- **Flow-sensitive refinement propagation.** GCC does not prove a `Refined<>` invariant holds across multiple function boundaries. The wrapper checks at construction; the body inside the function trusts the invariant. Chaining requires either re-wrapping at each boundary or an SMT-discharged proof via the `verify` preset (§I).
- **Alias analysis.** `Linear<>` catches double-consume on a single value but not two pointers to the same underlying object created through unsafe channels. Review + `-fsanitize=address` + the axioms from §II are the line of defense.
- **Compile-time information flow.** Branching on a `Secret<>` value is not rejected at compile time. `ct::*` primitives and the constant-time discipline (§III opt-out of `memory_order::consume`, crypto paths using `ct::select`) are opt-in.

For those properties, the `verify` preset reserves space for an internal small-SMT solver (deferred — interim: contract enforcement only); see §I. No external solver dependency.

---

## XVII. Identifier and Readability Discipline

Code tells a story. Reading a function should read like prose — a noun subject, a verb action, an adjective condition. Every identifier reads as a sentence fragment that narrates what the thing IS or DOES. The primary reader of this codebase is an agentic LLM — so names carry semantic weight equal to types. With no RTTI and no throw in the artifact, an identifier's spelling is often the only remaining signal an automated tool has about semantics; ambiguous names degrade grep, code review, and future refactoring equally.

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
- **Banned**: single-character identifiers (`g`, `t`, `e`, `s`, `x`, `b`, `n`, `r`) in any scope except numeric `for` loop induction over a range, and the capability parameter `a` (next section).
- **Banned**: two-character identifiers (`ty`, `ex`, `fn`, `st`, `pt`, `tc`, `ok`, `nf`). Abbreviations are not names.
- **Discouraged**: identifiers ≤ 3 characters. Prefer `scope` over `sc`, `param` over `p`, `binder` over `b`, `result` over `r`, `grade` over `g`, `index` over `i`.

### Allowed exceptions (canonical technical terminology)

Five categories of short names are exempt because they ARE the standard vocabulary in their domain — renaming them would make the code less recognizable, not more:

- **Crucible ontology primitives**: `Vigil`, `Keeper`, `Relay`, `Cipher`, `Canopy`, `Meridian`, `Observe`, `Warden`, `Vessel` are full words and fine at any length. Their short aliases in hot paths are not — use the full name.
- **Hot-path idiomatic short names** canonical in Crucible: `op` (Op), `args` (const Expr* const*), `nargs` (uint8_t), `ndim` (uint8_t), `dtype` (ScalarType), `a` (the `::foundation::effects::Alloc` capability parameter of every Arena/ExprPool/MerkleDag/Graph allocator function), `arena` (the `Arena&` beside it), `ctx` (an execution context: `foundation::effects::ExecCtx` in a ctx-bound mint, or `CrucibleContext`), `bg` (`::foundation::effects::Bg` context — the background-thread context that holds the Alloc, IO and Block atoms), `fg` (the foreground context. Its capability type is `foundation::effects::ctx_cap::Fg`, and it permits no effect atom, because hot-path code holds no capability), `ms` (MetaIndex strong ID), `ring` (TraceRing&). Established in TraceRing.h / ExprPool.h / MerkleDag.h; rename would be churn.
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

C++ test names and invariant names obey the same rule: compose a question: `isConsumed_impliesGradeZero`, `bitExact_ofReplay`, `wellFormed_ofChecked`. Never `lemma1`, `wf_thm`, `tc_test`. Test-suite names that describe the invariant should lead with the invariant name, not the test index.

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

**HS12.** Permission discipline on every new concurrent endpoint. New producer-consumer pairs use `Permission<Tag>` (or `SharedPermission` + Pool for SWMR). New thread-spawn sites either use `mint_permission_fork` (structured) or document why a raw `std::jthread` move is appropriate. Bypassing the discipline is questioned on review.

**HS13.** No regression at the chosen parallelism factor. If `ParallelismRule` or any new threading code chooses parallel(N), `bench/bench_no_regression.cpp` must show ≤5% regression vs sequential at that workload's footprint tier. Cache-resident workloads stay sequential by default; DRAM-bound workloads parallelize.

**HS14.** Every new mint factory ships with at least 2 negative-compile fixtures. Per the Universal Mint Pattern (§XXI) discipline, a `mint_X(ctx, args...)` factory's `requires` clause is the single load-bearing soundness gate — and a soundness gate without a witness that it FIRES is just a comment. Each fixture demonstrates a distinct mismatch class (unfit ctx residency, non-bridgeable direction, malformed parameter, etc.). It lives in the negative-compile directory of its layer: `test/fixy/neg/` or `test/foundation/neg/` for `include/fixy/` and `include/foundation/`, a `test/*_neg/` directory for `include/crucible/`. `utils/scripts/gen-mint-inventory.py` counts the fixtures of each mint in its own layer only. Its `--check-floor` mode fails CI on a mint with fewer than two, unless `utils/scripts/mint-hs14-floor-allowlist.txt` lists it.

---

## XIX. When to Update This Guide

Update rules here when:

1. A new UB class is discovered in production or CI → add to footgun catalog.
2. A new GCC/libstdc++ feature lands that measurably helps an axiom → add to opt-in.
3. A measurement invalidates a "perf wisdom" here → replace with the measured version.
4. A rule is consistently violated without consequence → investigate whether the rule is still necessary.
5. A new Permission tag tree, can_split_into specialization, or PermissionedFoo primitive lands → add a row to §IX or §XVI cataloging it.
6. The cache-tier rule (§IX) is invalidated by a new microarchitecture → re-measure with `bench/bench_no_regression.cpp`, update the table.

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
    Cache-tier appropriateness (ParallelismRule)  >  NUMA locality  >  Maximum throughput
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

- **Token mint** — synthesizes a fresh authoritative token whose authority derives from a parent token (or root authority). It has no `CtxFitsX` gate. A permission token mint takes an optional leading context, and a tag that declares a non-empty effect row makes that context necessary. Examples: `mint_permission_root<Tag>()`, `mint_permission_split<L,R>(parent)`, `mint_cap<E>(source)`, `mint_session_handle<Proto>(res)`.
  - Note that `mint_permission_split` and `mint_permission_combine` consume a parent token and produce fresh children/parent — the children/parent are authoritative tokens that didn't exist before the call. Mint applies even though the operation is shape-preserving decomposition/composition.

- **Ctx-bound mint** — threads ctx-driven policy through the constructed type. Ctx is the FIRST parameter; the requires-clause is a single `CtxFitsX<X, Ctx>` concept. Examples: `mint_from_ctx<E>(ctx)`, `mint_session<Proto>(ctx, res)`, `mint_permissioned_session<Proto>(ctx, res, perms...)`, `mint_substrate_session<...>(ctx, handle)`, `mint_endpoint<...>(ctx, handle)`.

### The canonical mints (status legend: ✅ shipped • 🚧 planned • 🔮 future tier)

| Status | Layer | Mint | Concept gate | Returns |
|---|---|---|---|---|
| ✅ | Permission token | `mint_permission_root<Tag>([ctx])` | `PermissionRootArgs<Tag, Args...>` | `Permission<Tag, Brand>` with a fresh brand |
| ✅ | Permission token | `mint_permission_split<L, R>([ctx,] parent)` | `PermissionSplitArgs<L, R, Args...>`; the body asserts `can_split_into_v<P, L, R>` | `pair<Permission<L, Brand>, Permission<R, Brand>>` |
| ✅ | Permission token | `mint_permission_combine<P>([ctx,] l, r)` | `PermissionCombineArgs<P, Args...>`; the body asserts `can_split_into_v<P, L, R>` | `Permission<P, Brand>` |
| ✅ | Permission token | `mint_permission_split_n<Children...>([ctx,] parent)` | `PermissionSplitNArgs<tuple<Children...>, Args...>`; the body asserts `can_split_into_pack_v<P, Children...>` | `tuple<Permission<Children, Brand>...>` |
| ✅ | Permission token | `mint_permission_combine_n<P>([ctx,] children...)` | `PermissionCombineNArgs<P, Args...>`; the body asserts `can_split_into_pack_v<P, Children...>` | `Permission<P, Brand>` |
| ✅ | Permission token | `mint_permission_share([ctx,] perm)` | `PermissionShareArgs<Args...>`; an untracked share, with no pool | `SharedPermission<Tag, Brand>` |
| ✅ | Ctx-bound permission token | `mint_permission_fork<Children...>(ctx, parent, callables...)` | `CtxFitsPermissionFork<Ctx, P, Children...>` (the inline gate plus `CtxOwnsCapability<Ctx, Effect::Bg>`) | `Permission<P, Brand>` after the threads join |
| ✅ | Ctx-bound permission token | `mint_permission_fork_inline<Children...>(ctx, parent, callables...)` | `CtxFitsPermissionForkInline<Ctx, P, Children...>` (`IsExecCtx`, the context admits each tag, `can_split_into_pack_v`) | `Permission<P, Brand>` after the bodies run in child order |
| ✅ | Capability token | `mint_cap<E>(source)` | `CanMintCap<E, S>` | `Capability<E, S>` |
| ✅ | Ctx-bound | `mint_from_ctx<E>(ctx)` | `CtxOwnsCapability<Ctx, E>`: the row of the context contains `E` | `Capability<E, cap_type_of_t<Ctx>>` |
| ✅ | Grade token | `fixy::mint_band<Band>(value)` | `ExactBand<Band>`; the key authority is a class local to the mint | `Band` |
| ✅ | Grade token | `fixy::mint_recipe_spec(value, tier, family)` | `RecipeSpecPayload<T>` | `RecipeSpec<T>` |
| ✅ | Session token | `mint_session_handle<Proto>(res)` | `WellFormedRunnableProtocol<Proto> ∧ SessionResource<Res> ∧ PermissionFlowCloses<Proto, EmptyPermSet>` | the first `SessionHandle` of `Proto` over `Res` |
| ✅ | Ctx-bound | `mint_session<Proto>(ctx, res)` | `CtxFitsSession<Ctx, Proto, Res>` | the first handle of `Proto`, with `EmptyPermSet` |
| ✅ | Ctx-bound | `mint_permissioned_session<Proto>(ctx, res, perms...)` | `CtxFitsPermissionedSession<Ctx, Proto, Res, Tags...>` (a non-empty tag set) | the first handle of `Proto`, with `PermSet<Tags...>` |
| ✅ | Ctx-bound | `mint_forked_channel<Proto, SelfTag, PeerTag>(ctx, parent, res_self, res_peer, self_body, peer_body)` | `CtxFitsForkedChannel<Ctx, Proto, Parent, SelfTag, PeerTag>`, plus checks of each resource and each body | `Permission<Parent, Brand>` after the two threads join |
| ✅ | Ctx-bound | `mint_substrate_session<Substr, Dir>(ctx, std::move(handle))` | `CtxFitsSubstrateSessionMint<Substr, Dir, Ctx>` (`IsBridgeableDirection` and `CtxFitsSession` of the default protocol) | the first handle of `default_proto_for_t<Substr, Dir>` |
| ✅ | Ctx-bound (Tier 2) | `mint_endpoint<Substr, Dir>(ctx, std::move(handle))` | `CtxFitsEndpointMint<Substr, Dir, Ctx>` (the substrate gate and a copyable context) | `Endpoint<Substr, Dir, Ctx>` |
| ✅ | Session wrap | `fixy::session::mint_recorded_session(handle, log, self, peer)` | `RecordableHandle<H>` | `Recorded<H>` |
| ✅ | Session wrap | `fixy::session::mint_crash_session<Proto, Self, Peer>(res, peer_cell)` | `CrashSessionAdmissible<Proto, Self, Peer, Reliable> ∧ SessionResource<Res>` | `CrashWatched<Handle, Self, Peer, Reliable>` over the first handle of `Proto` |
| ✅ | Tier 3 | `mint_stage<auto FnPtr>(ctx, in, out)` | `CtxFitsStage<FnPtr, Ctx>` (≡ `PipelineStage<FnPtr> ∧ IsExecCtx<Ctx> ∧ StageInputRowAdmitted<FnPtr, Ctx> ∧ StageOutputRowAdmitted<FnPtr, Ctx>`) | `Stage<FnPtr, Ctx>` |
| ✅ | Tier 3 | `mint_pipeline(ctx, stages...)` | `CtxFitsPipeline<Ctx, Stages...>` (≡ `IsExecCtx<Ctx> ∧ CtxStartsStageThreads<Ctx> ∧ pipeline_chain<Stages...> ∧ decide::row_subset(^^pipeline_row_union_t<Stages...>, ^^Ctx::row_type)`); `CtxStartsStageThreads` asks for a context that owns `Effect::Bg` or `Effect::Init` | `Pipeline<Stages...>` |
| ✅ | Tier 2→3 bridge | `mint_stage_from_endpoints<auto FnPtr>(ctx, in_ep, out_ep)` | `CtxFitsStageFromEndpoints<FnPtr, Ctx, ConsumerEp, ProducerEp>` (≡ `CtxFitsStage<FnPtr, Ctx> ∧ IsConsumerEndpoint<ConsumerEp> ∧ IsProducerEndpoint<ProducerEp> ∧ IsMovedEndpoint` for each endpoint `∧ StageHandlesMatchEndpoints<FnPtr, ConsumerEp, ProducerEp>`); it takes each handle through `into_handle()` and calls `mint_stage` | `Stage<FnPtr, Ctx>` |
| 🔮 | Tier 4 | `mint_vigil<L, D, C>(ctx, parts...)` | per-component fit | `Vigil<L, D, C>` |
| 🔮 | Tier 5 | `mint_keeper<Vigils...>(ctx, vigils, topo)` | per-Vigil fit | `Keeper<Vigils..., ...>` |
| 🔮 | Tier 6 | `mint_canopy<Keepers...>(ctx, keepers, mesh)` | per-Keeper fit | `Canopy<Keepers..., ...>` |

A handle of an SPSC or MPSC channel goes into a session through `mint_substrate_session` or `mint_endpoint`. `mint_channel` is a deleted function in `fixy/session/Handle.h`, because it gave the two endpoints of one channel to one caller. A test that operates the two endpoints on one thread uses `mint_test_channel` with a test context.

**Passkeys.** A mint builds its product through a key or a door that only the mint can use. `cap_mint_key` controls the construction of `Capability`. A door class with private static members controls each session wrap. `grade_key<Authority>` controls each door of `Graded` that attaches a grade to a value. §XVI ("How a graded wrapper is built") describes `grade_key`, `mint_band` and `mint_recipe_spec`.

### Why the pattern is load-bearing

1. **Single grep target.** `grep "mint_"` finds every authorization point in the codebase. Every cross-tier composition is discoverable in O(1) review time.
2. **Construction-time validation.** The `requires` clause is the type-level proof that runs ONCE at the boundary. Subsequent ops on the returned X are concept-free — full speed, no per-call check.
3. **Uniform shape across tiers.** A production engineer who learns one mint learns them all. A maintainer who adds Tier 7 follows the template. New contributors recognize the pattern instantly.
4. **No naming drift.** `make_*` / `establish_*` / `create_*` / `build_*` are NOT minters and MUST NOT be used for cross-tier composition. They allocate or initialize bare structures; they don't carry the type-level fit-check semantics.

### Discipline rules

- **Every cross-tier composition factory MUST be named `mint_<noun>`.** No exceptions.
- **For ctx-bound mints, the first parameter MUST be `Ctx const&`.** Token mints (which derive authority from a parent token rather than from a ctx) take the parent/source as the first parameter.
- **The `requires` clause MUST be a single concept** (`CtxFitsX<X, Ctx>` for ctx-bound mints, or the equivalent token-validity concept for token mints). Multi-clause requires-lists belong INSIDE the concept definition, not at the call site.
- **Every mint MUST be `[[nodiscard]] constexpr noexcept`**, with two exceptions, and each exception is `[[nodiscard]] noexcept` only. The first is a factory that genuinely allocates: `constexpr` would lie about the runtime cost. The second is a factory whose body can never be constant evaluated, because it reads runtime state such as an atomic, a thread identity or a system call. The build has no `-fimplicit-constexpr`, and `-Winvalid-constexpr` with `-Werror` refuses `constexpr` on such a function. Never hide runtime state behind `if consteval` to keep the keyword: a constant evaluation would then read a value that the program never has.
- **Returned types are CONCRETE, not type-erased.** `mint_endpoint<...>(ctx, h)` returns `Endpoint<Substr, Dir, Ctx>`, not `auto`-erased-into-virtual. Concept-overloaded specialization downstream depends on the concrete type.
- **Diagnostics route through `foundation::diag::Category`.** A mint that fails its `requires` clause emits a category-tagged diagnostic so user-facing errors stay readable.
- **Internal helpers do NOT use the `mint_` prefix.** The convention marks USER-FACING authorization points; internal detail-namespace helpers carry the trailing-underscore convention (for example `PermissionForkRunner::spawn_` and `PermissionForkRunner::run_` in `foundation/permissions/PermissionFork.h`) so `grep "mint_"` returns only the public surface.
- **Session ctx-bound mints use the permissioned family.** `mint_session<Proto>(ctx, res)` is the empty-`PermSet` shim; `mint_permissioned_session<Proto>(ctx, res, perms...)` is the non-empty `PermSet` form. Both route through the same ctx row gate and local permission-flow closure gate.
- **Every new mint factory MUST ship at least 2 negative-compile fixtures** demonstrating the `requires` clause fires on each kind of mismatch. See HS14.
- **Inventory of every mint** lives in `misc/mint-inventory.md`, regenerated by `python3 utils/scripts/gen-mint-inventory.py --write`. It reads the AST mint model in `utils/scripts/mintmodel.py`, the same model the §XXI guard `utils/scripts/check-mint-pattern.py` reads, over all of `include/`. Each row gives the §XXI flags (nodiscard, constexpr, noexcept, a type-level constraint) and the authorization shape (ctx, token or member). It also says whether the constraint of a ctx-bound mint gates its context, and it counts the HS14 fixtures of the mint's own layer. `--check` fails CI on drift, and `--check-floor` fails CI on a mint under the HS14 floor that `utils/scripts/mint-hs14-floor-allowlist.txt` does not list.

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
