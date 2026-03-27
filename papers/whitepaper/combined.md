# Abstract

Machine learning runtimes dispatch each operation independently: the same kernel runs regardless of shape, device, or context. Memory is allocated dynamically per operation. Parallelism strategy is fixed at configuration time. Hardware failure loses all work since the last checkpoint. No runtime invariant is proved.

Crucible is an adaptive ML runtime organized around a content-addressed Merkle DAG that unifies compilation, memory planning, distribution, and persistence. It interposes on the framework dispatch layer (the *Vessel*) to record execution traces into a lock-free SPSC ring buffer at 3--5ns per operation on the hot path, constructs a bidirectional dataflow graph, and compiles execution plans where identical sub-computations produce identical content hashes --- enabling kernel reuse across models, runs, and organizations. SMT-based kernel configuration synthesis encodes hardware constraints and a roofline cost model as satisfiability problems, yielding configurations optimal within the analytical model with formal safety guarantees (no out-of-bounds access, no shared memory bank conflicts, coalesced memory access). Static memory planning from dataflow lifetimes replaces per-operation allocation with a single pre-computed pool layout. An event-sourced persistence model (the *Cipher*) enables deterministic replay and hardware migration.

The architecture spans seventeen layers from formal verification (FX) through ecosystem-wide kernel sharing. The prototype implements the recording pipeline (64-byte SPSC ring entries, 144-byte parallel metadata log), graph construction (bidirectional CSR with O(V+E) counting sort), Merkle DAG (RegionNode, BranchNode, LoopNode with content and Merkle hashing), memory planning (sweep-line lifetime analysis), compiled replay with graduated four-level divergence detection, a 146-operation kernel taxonomy, and a mutable Graph IR with dead code elimination, common subexpression elimination, and topological sort. An independent Lean 4 formalization proves properties of every core data structure and algorithm with no unresolved proof obligations.
# 1. Introduction

## 1.1 The Problem

PyTorch's eager execution model lets researchers write arbitrary Python control flow around tensor operations, debug interactively, and iterate rapidly. This design dominates ML research. It also structurally prevents several categories of optimization.

**Static kernel selection.** `torch.mm(A, B)` dispatches to a pre-compiled library kernel (cuBLAS, cuDNN, CUTLASS) selected by operation name and data type. The same kernel executes for 64x64 and 8192x8192 matrices, for H100 and 3090, for contiguous and transposed inputs, regardless of whether adjacent operations could be fused.

**Per-operation overhead.** Each operation independently allocates output memory, dispatches a kernel, and returns. A 1,000-operation model incurs 1,000 allocator calls (mutex acquisition, freelist search, block splitting), 1,000 kernel launches, and 1,000 Python-level returns with reference counting. PyTorch's CUDACachingAllocator averages 200--2000ns per allocation; at 1,000 allocations per iteration, allocation alone consumes 0.2--2ms.

**Training/inference discontinuity.** A model trained in PyTorch must be exported (`torch.export`, ONNX, TorchScript) to a serving runtime. Each export path has operator coverage gaps. The result is a separate engineering effort with no guarantee of numerical equivalence.

**Fixed configuration.** Parallelism strategy, learning rate schedules, precision choices, and batch sizes are fixed before training begins. If hardware degrades (thermal throttling, network congestion, node failure) or training dynamics shift (gradient health, effective rank, layer redundancy), the configuration cannot adapt.

**No formal guarantees.** Memory plans may overlap. Ring buffer protocols may deadlock. Kernel configurations may access out-of-bounds memory. Hash functions may have fixed points. Testing and fuzzing demonstrate the presence of bugs but not their absence.

These are consequences of design choices that prioritize other goals. Crucible explores a different point in the design space.

## 1.2 Approach

Crucible records what the framework does, then executes it better next time.

**Terminology.** The **Vessel** is the framework adapter --- currently PyTorch's ATen dispatch layer, though the architecture is framework-agnostic. The **Vigil** is the model as a persistent entity: computation graph, weights, and accumulated knowledge, outliving any hardware node. The **Cipher** is event-sourced persistent state (DAG chain, weight snapshots, kernel cache, RNG state) enabling hardware migration. A **Relay** is a compute node running a Crucible daemon. The **Keeper** is the per-Relay daemon for health monitoring, mesh participation, and executing optimization recommendations. The **Canopy** is the masterless Keeper mesh. **Longitude** is startup calibration (hardware measurement and configuration optimization). **Augur** is continuous monitoring (performance prediction, drift detection, bottleneck diagnosis). **FX** is the formal verification layer proving runtime invariants at build time.

**Three execution phases.** In **recording**, Crucible intercepts Vessel dispatch and records each operation's identity, input/output tensor metadata, and scalar arguments into a lock-free SPSC ring buffer (65,536 entries of 64 bytes each, with parallel arrays for scope and callsite hashes). The Vessel executes normally; recording adds 3--5ns per operation on the hot path (one cache-line write plus a release store on the head pointer). A separate MetaLog (1M entries, 144 bytes each) stores full tensor metadata. In **analysis**, a background thread drains the ring buffer in batches via memcpy, constructs a bidirectional CSR dataflow graph, detects iteration boundaries via K=5 schema hash signature matching, and builds a content-addressed Merkle DAG. From the DAG, it computes a static memory plan and prepares a compiled execution plan. In **compiled** mode, the Vessel interceptor advances an operation index, checks a guard hash, and returns a pre-allocated shadow handle --- no execution, no allocation, no redispatch.

**The Merkle DAG** is the central abstraction. Each node's content hash is computed from computation semantics: operation identities, shapes, data types, scalar arguments. Identical computations produce identical hashes regardless of origin. This enables kernel reuse across iterations, runs, models, and organizations. The DAG simultaneously serves as versioning mechanism (Merkle hashes enable O(log N) structural diff), guard system (hash mismatch triggers recompilation), deployment artifact (no export step), and persistence format (the Cipher is event-sourced from DAG state).

**Formal verification** is integrated at build time through four layers: Z3 SMT solver for universal proofs over all inputs, `consteval` for bounded model checking by the compiler's abstract machine, static reflection (C++26 P2996) for structural completeness checks, and the C++ type system for zero-cost API enforcement (capability tokens, strong types, phantom thread-affinity tags).

## 1.3 Contributions

1. A content-addressed Merkle DAG enabling cross-model, cross-run kernel reuse through semantic hashing of computation sub-graphs.
2. SMT-based kernel configuration synthesis formulating hardware constraints and roofline cost models as satisfiability problems, producing configurations with formal safety guarantees.
3. Static memory planning from dataflow lifetimes, replacing per-operation dynamic allocation with a single pre-computed pool layout.
4. Graduated divergence detection with four severity levels (schema, shape, scope, callsite hash mismatches), enabling seamless compiled-to-eager fallback.
5. A four-layer formal verification architecture (FX) proving runtime invariants at build time: Z3 universal proofs, `consteval` bounded model checking, reflection-based structural checks, and type-system enforcement via capability tokens and strong types.
6. An event-sourced persistence model (the Cipher) enabling deterministic replay, hardware migration, and recovery from any training step.
7. A prototype implementation (~9,500 lines of C++26, 24 tests, compiling clean on Clang 22 and GCC 15), accompanied by a Lean 4 formalization (36 modules, 1,312 theorems, zero `sorry`).

## 1.4 Paper Organization

Section 2: design principles. Section 3: recording and Merkle DAG representation. Section 4: compilation, memory planning, execution model. Section 5: formal verification (FX). Section 6: hardware adaptation (Longitude, Augur). Section 7: distribution (Canopy, Keepers, Relays) and persistence (Cipher). Section 8: model-aware optimizations. Section 9: implementation status. Section 10: related work. Section 11: roadmap. Section 12: conclusion.
# 2. Design Principles

## 2.1 Content-Addressed Computation

Every compilable sub-graph is identified by a content hash computed from its semantics: operation identities, shapes, data types, scalar parameters. Two sub-graphs performing identical computation produce identical hashes regardless of origin. The hash function uses wyhash-style 128-bit multiply mixing (wymix) over schema hashes, per-tensor dimension/stride products, packed dtype/device metadata, and scalar arguments --- approximately 6 cycles per tensor on x86-64.

Three consequences. First, the KernelCache maps `(ContentHash, device_capability)` to compiled artifacts; a kernel compiled for one model serves any future model containing the same sub-computation on the same hardware class. Second, structural comparison reduces to hash comparison: shared Merkle hashes imply identical sub-trees, enabling O(log N) diff analogous to Git. Third, content addressing provides a natural cache key at every level: compiled kernels, memory plans, calibration data, proof certificates.

## 2.2 Observe Before Optimizing

Crucible records one complete iteration of what the model actually does, then optimizes from observations. No shape inference, no symbolic tracing, no compiler-level analysis of Python control flow. The recording captures concrete execution: actual shapes, actual data flow (tracked via pointer identity in an open-addressing PtrMap), actual iteration boundaries (detected via K=5 schema hash signature matching with two-match confirmation).

When behavior changes --- dynamic shapes, data-dependent control flow, architecture mutations --- guard hashes detect the mismatch. The system falls back to eager execution, records the new behavior, and recompiles. The observe-compile-execute cycle is continuous and self-correcting.

## 2.3 Formal Verification Where Tractable

Crucible proves properties that are (a) expressible in a decidable theory, (b) critical for correctness, and (c) insufficiently covered by testing.

Memory plan non-overlap: bitvector satisfiability over tensor lifetimes. SPSC protocol safety: finite-state exhaustive exploration. Kernel access bounds: bitvector constraints over thread and block indices. Hash function quality: universal bitvector queries (no fixed points in 2^64 inputs, avalanche >= 20 bits per input bit flip). All proved at build time.

Properties depending on hardware timing (memory ordering), continuous quantities (floating-point stability), or unbounded state spaces (general correctness) fall outside static verification scope. For these: ThreadSanitizer at runtime, empirical calibration, and architectural mitigation (deterministic execution order). The boundary is documented: which properties are proved, which are tested, which are mitigated.

## 2.4 Hardware-Agnostic Representation, Hardware-Specific Execution

The Merkle DAG captures *what* to compute. The KernelCache contains *how* on specific hardware. A single DAG may have compiled kernels for H100 (sm_90), MI300X (gfx942, 64-wide wavefronts), A100 (sm_80), and consumer 3090 (sm_86), each keyed by `(ContentHash, device_capability)`. Migration to new hardware regenerates only kernel cache entries; the DAG is unchanged.

CostModel.h encodes hardware profiles (HardwareProfile struct: SM count, warp size, register file, shared memory, four-level bandwidth hierarchy, peak throughput per dtype) with presets for B200, H100, MI300X, and A100. Heterogeneous clusters are natural: different Relays execute different compiled kernels for the same logical computation.

## 2.5 No Training/Inference Distinction

The compiled Merkle DAG is both training and serving artifact. No export step, no format conversion, no operator coverage gap. Training and inference differ only in which sub-DAG executes (forward+backward+optimizer vs forward only). Both use the same compiled kernels, memory plans, and dispatch mechanism. Deploying a model means copying the Cipher to a serving Relay.
# 3. Recording and Representation

The recording pipeline, graph construction, and Merkle DAG are implemented and tested. Formal properties of the DAG are proved in Lean 4.

## 3.1 Vessel Interception

The Vessel adapts Crucible to the host framework. The current Vessel targets PyTorch's ATen dispatch mechanism --- a priority-ordered function pointer table indexed by dispatch keys. Crucible registers `DispatchKey::Conductor` above backend keys, observing every operation before it reaches the compute backend.

**Recording mode** (six steps per operation): (1) snapshot input TensorMeta (sizes[8], strides[8], data_ptr, ndim, dtype, device_type, device_idx, layout --- 144 bytes per tensor); (2) compute SchemaHash (from registered operator name) and ShapeHash (from input dimensions); (3) execute the operation via redispatch; (4) snapshot output TensorMeta; (5) append metadata to the MetaLog; (6) record a 64-byte entry in the TraceRing. **Compiled mode**: advance operation index, check guard hash, return pre-allocated shadow handle. No execution, no allocation, no redispatch.

The Vessel handles operations with zero tensor arguments (profiler hooks like `profiler::_record_function_enter_new`, scope markers) by recording them with `MetaIndex::none()`. TensorList inputs (concatenation, stacking) are unpacked into individual TensorMeta entries. Scalar arguments are encoded inline (up to five int64 values per TraceRing entry, covering 99.9% of operations). Communication operations (all-reduce, all-gather) use the identical recording pipeline.

## 3.2 Trace Recording

Two parallel SPSC (single-producer, single-consumer) structures communicate between the foreground thread (Python + Vessel recording) and the background thread (graph construction + compilation).

**TraceRing.** 65,536 entries (`CAPACITY = 1 << 16`), each exactly 64 bytes (one cache line): `SchemaHash` (8B), `ShapeHash` (8B), `num_inputs` (2B), `num_outputs` (2B), `num_scalar_args` (2B), `grad_enabled` (1B), `inference_mode` (1B), and `scalar_values[5]` (40B). Three parallel arrays alongside entries --- `meta_starts[]` (MetaIndex), `scope_hashes[]` (ScopeHash), `callsite_hashes[]` (CallsiteHash) --- are indexed by the same slot, providing the background thread with module context and source location. Total pre-allocated: ~5.25MB.

Bitmask indexing (`index & MASK`) replaces modulo. Head and tail are `alignas(64)` atomics on separate cache lines to prevent false sharing. The producer caches the last-read tail locally (`cached_tail_`); since drain only advances the tail forward, a stale cache is conservative. This avoids cross-core cache-line traffic on the common path --- approximately 20,000 appends between drains at 5ns/op and 100us drain intervals. The next write slot is prefetched via `__builtin_prefetch` (write hint, L1 locality) before publishing the head, hiding DRAM latency for the 4MB ring.

**MetaLog.** 1,048,576 entries (`CAPACITY = 1 << 20`) of 144-byte TensorMeta records, 64-byte aligned allocation (~144MB). Same SPSC protocol with cached tail. Producer hot path: ~12ns for 1 tensor, ~33ns for 3 tensors (measured). Bulk memcpy for contiguous regions compiles to AVX-512 stores. The background thread accesses entries zero-copy via `try_contiguous()` when the range does not wrap around the circular boundary (99.99% of calls with 1M capacity and ~1,500 metas per iteration).

Both structures are pre-allocated at initialization and never resized. The foreground recording path involves no dynamic allocation, no system calls, and no contention. All graph and DAG memory is allocated from a bump-pointer Arena on the background thread (~2ns per allocation, 1MB blocks, zero fragmentation).

## 3.3 Graph Construction

The background thread drains the TraceRing in batches of up to 4,096 entries (BATCH_SIZE) and constructs a TraceGraph: a bidirectional CSR property graph where nodes represent operations and edges represent relationships. Four edge kinds are defined: `DATA_FLOW` (producer-consumer), `ALIAS` (shared storage), `CONTROL_FLOW` (explicit ordering), and `SCALAR_FLOW` (scalar dependencies). Each Edge is 12 bytes: `OpIndex src`, `OpIndex dst`, `src_port` (uint8), `dst_port` (uint8), `EdgeKind`, padding.

**Data flow edges** are constructed by pointer tracking. A PtrMap (open-addressing, generation-counter-based to avoid memset between iterations) maps tensor `data_ptr` to the OpIndex that produced it. Input pointer matching a recorded output pointer creates a DATA_FLOW edge. **Alias edges** arise when multiple operations reference the same underlying storage (views, in-place ops, transposes).

Each graph node is a TraceEntry: SchemaHash, ShapeHash, ScopeHash, CallsiteHash (all strong uint64 types), arena-allocated TensorMeta arrays for inputs and outputs, arena-allocated scalar args, grad/inference flags, CKernelId classification, and SlotId arrays for memory planning. The graph is built in a single pass with O(V+E) CSR construction via counting sort (prefix sum + scatter). Both forward (src-sorted) and reverse (dst-sorted) CSR adjacency structures are constructed simultaneously.

Scratch buffers (PtrMap, SlotInfo, Edge arrays) are allocated once and reused across iterations --- zero per-call allocation. Content hash is fused into the main loop as a streaming accumulator, eliminating a redundant second pass.

**Iteration detection.** The IterationDetector uses a K=5 schema hash signature occupying exactly two cache lines (128 bytes). Steady-state hot path: one increment + one comparison + one well-predicted branch (~1ns). The expected next hash value is cached directly --- no rolling fingerprint, no ring buffer, no multiply. Two-match confirmation handles warmup: the first match locks the signature as a candidate; the second confirms a boundary. The last completed iteration length is computed via `std::sub_sat(ops_since_boundary, K)`. False positives require 5 consecutive hash collisions; with 64-bit hashes, probability is ~5 * 2^(-320).

## 3.4 The Merkle DAG

From the TraceGraph, the background thread constructs the Merkle DAG. All nodes inherit from TraceNode (24 bytes: `TraceNodeKind kind`, padding, `MerkleHash merkle_hash`, `TraceNode* next`). Four kinds: REGION, BRANCH, LOOP, TERMINAL.

**RegionNode.** A compilable operation sequence. Contains an arena-allocated array of TraceEntry structs, a ContentHash computed via `compute_content_hash()` (wymix-fold over schema hashes, per-tensor dimension products, packed dtype/device metadata, and scalar arguments), an atomic `CompiledKernel*` pointer (background writes with release, foreground reads with acquire), a `MemoryPlan*` for liveness analysis, `first_op_schema` for quick mismatch detection, and `measured_ms`/`variant_id` for runtime profiling. The Merkle hash extends the content hash with the continuation's Merkle hash via `fmix64(content_hash ^ next->merkle_hash)`.

**BranchNode.** A guard point with sorted arms for O(log n) binary search during replay. The Guard struct (12 bytes) supports five kinds: SHAPE_DIM, SCALAR_VALUE, DTYPE, DEVICE, OP_SEQUENCE. Each arm is a `(int64_t value, TraceNode* target)` pair. The `next` pointer points to the merge point where all arms reconverge. BranchNodes are the mechanism for all runtime adaptation: architecture mutation, attention head replacement, hyperparameter changes, continuous learning with A/B verification.

**LoopNode.** Cyclic computation within the acyclic DAG framework (64 bytes = one cache line). Contains a body sub-DAG (linked list ending in TERMINAL), an array of FeedbackEdge structs (4 bytes each: `output_idx`, `input_idx`), and termination: `LoopTermKind::REPEAT` (fixed N iterations) or `LoopTermKind::UNTIL` (converge within epsilon). The Merkle hash uses a "LOOPNODE" salt (0x4C4F4F504E4F4445) to distinguish from RegionNodes, incorporating `body_content_hash`, `feedback_signature()` (fmix64 fold over all feedback edges with edge count), and `loopterm_hash()` (termination kind + repeat count + epsilon bits). LoopNodes enable compiled recurrence (RNNs without Python per timestep), convergence-based execution (fixed-point iterations), and cross-iteration pipelining.

**Atomic activation.** The background thread prepares new DAG structures and publishes them via `active_region.store(ptr, release)`. The foreground thread loads with acquire. All zero-downtime updates --- compilation activation, branch swaps, memory plan updates, topology changes, rollbacks --- use this single mechanism.

## 3.5 Content Addressing and the Kernel Cache

The KernelCache is a lock-free open-addressing hash table (default capacity 4,096, power-of-two, calloc-initialized). Lookup: linear probing with bitmask indexing; `content_hash` slots are `atomic<uint64_t>` loaded with acquire; zero signals empty (miss). Insert: CAS on the content_hash slot with acq_rel ordering; if the key already exists, the kernel pointer is updated (newer variant wins). The size counter uses relaxed ordering (informational only).

Content addressing yields four reuse levels:

- **Cross-iteration.** Iteration N+1 typically has identical content hashes. All compiled kernels are cache hits.
- **Cross-run.** The KernelCache persists in the Cipher. Restarting training incurs zero compilation.
- **Cross-Vigil.** Different Vigils sharing sub-computations (e.g., same attention mechanism, same head dimension) produce identical content hashes. Compiled kernels transfer automatically.
- **Cross-device.** The same content hash maps to different compiled kernels on different hardware. Multiple device-specific variants coexist.

The cache grows monotonically and persists across Relay migrations.
# 4. Compilation and Execution

Memory planning and Tier 1 replay are implemented. SMT kernel synthesis and shadow handle execution are designs.

## 4.1 Static Memory Planning

Crucible replaces per-operation dynamic allocation with a static plan computed from the dataflow graph. During `build_trace()`, the background thread identifies unique tensor storages as TensorSlot structs (40 bytes each: `offset_bytes`, `nbytes`, `birth_op`, `death_op`, dtype, device, `is_external` flag, `slot_id`). Storage size is computed via `compute_storage_nbytes()`, which handles negative strides (e.g., `torch.flip`, `as_strided`) by tracking both max and min offset contributions. External tensors (parameters, data loader outputs) are excluded from the pool.

A sweep-line algorithm assigns offsets within a single contiguous pool using counting sort O(n+k) rather than insertion sort:

1. Sort tensors by birth index.
2. Maintain free intervals within the pool.
3. For each tensor in birth order: find the first fitting interval, assign offset, mark occupied until death.
4. At death points, return intervals to the free list.

The output is a MemoryPlan: `pool_bytes` (total), `num_slots`, `num_external`, device context (DeviceType, device_idx, device_capability), and distributed topology (rank, world_size). At runtime, one allocation obtains the pool. Every subsequent "allocation" is `base_ptr + offset`. No mutex, no freelist, no fragmentation.

**Correctness.** Non-overlap is proved in Lean 4 for the general case and verified by Z3 for configurations up to 64 tensors with arbitrary lifetimes.

**Dynamic shapes.** New MemoryPlan computed by the background thread and activated via atomic swap at the iteration boundary. Foreground never blocked.

**Activation checkpointing.** The memory plan provides exact per-tensor cost. For each forward activation needed in backward: if `store_cost / recompute_cost > threshold`, recompute. Per-tensor, adapts to available memory.

## 4.2 SMT-Based Kernel Synthesis

*Design.* Autotuning (Triton, AutoTVM/Ansor) searches configuration space by benchmarking candidates --- hundreds to thousands of trials, finding local optima. Crucible formulates kernel configuration as a constraint optimization problem.

For a given operation, shape, and target device, CostModel.h defines the complete problem:

- **Axioms** (`HardwareProfile`). Measured hardware parameters: `num_sms`, `warp_size`, `max_warps_per_sm`, `regs_per_sm`, `max_regs_per_thread` (255), `smem_per_sm`, `tmem_per_sm`, four-level bandwidth hierarchy (`smem_bw_per_sm`, `l2_bw`, `hbm_bw`), corresponding latencies, peak throughput per dtype (fp64 through fp4), and `launch_ns` (~3us). Presets exist for B200 (sm_100), H100 (sm_90), MI300X (gfx942), and A100 (sm_80). Longitude replaces presets with calibrated measurements.
- **Variables** (`KernelConfig`). `tile_m`, `tile_n`, `tile_k` (uint16), `pipeline_stages` (1--7), `warps_per_block` (1--32), `smem_bytes`, `regs_per_thread`, `vec_width`.
- **Constraints** (C1--C8). `smem_bytes <= smem_per_sm`. `regs_per_thread <= max_regs_per_thread`. `warps_per_block >= 1`. `warps_per_block * warp_size <= 1024`. Positive tile dimensions. `pipeline_stages` in [1,7]. Each maps mechanically to a Z3 `(assert ...)`.
- **Objective.** Minimize roofline execution time: `max(compute_ns, memory_ns) + launch_ns`. Compute: FLOPs / (peak_tflops * 1e3). Memory: bytes / hbm_bw. The ridge point (`peak_tflops * 1e3 / hbm_bw`) determines whether a kernel is compute-bound or memory-bound.

A Z3 `optimize` query finds the configuration minimizing the objective subject to all constraints. The solution is optimal within the analytical model.

**Qualification.** The roofline model does not capture L1/L2 cache behavior, instruction scheduling, warp divergence, or TLB pressure. Longitude calibration (Section 6) computes per-kernel-class correction factors. Formal safety guarantees --- no out-of-bounds access, no bank conflicts, coalesced access --- are model-independent and hold on actual hardware.

**Scope.** Applies to any kernel with bounded discrete configuration: GEMM (tile/pipeline/warp), convolution (tile/implicit_gemm), attention (tile/pipeline/TMA), normalization (vector_width/rows_per_block), reduction (block_dim/strategy), collective communication (algorithm/channels/chunk_size).

## 4.3 Graduated Divergence Detection

Four severity levels based on which hash component diverges:

1. **SchemaHash mismatch** --- different operation. Hard diverge: immediate eager fallback, re-record.
2. **ShapeHash mismatch** --- same operation, different shapes. Hard diverge: kernels and memory plan invalid. Fallback, re-record, recompile.
3. **ScopeHash mismatch** --- same operation, same shapes, different module context (code refactoring). Soft warning: compiled plan remains valid.
4. **CallsiteHash mismatch** --- same shapes, same module, different source location. Softest warning: logged only.

**Recovery protocol.** After divergence, the foreground thread sets `BackgroundThread::reset_requested` (atomic bool). The background thread detects this at the start of each drain cycle, discards accumulated trace and resets the IterationDetector --- preventing leftover signature ops from producing false iteration boundaries and garbage regions. Recording skips during alignment; the divergent op itself is not recorded. If the divergence was temporary, the system may re-encounter the original behavior and switch back via BranchNode arm selection.

## 4.4 Shadow Handle Execution Model

*Design.* In Tier 2, the Vessel interceptor returns *shadow handles*: full tensor objects (ConductorTensorImpl) with correct metadata (shape, strides, dtype, device) whose storage points into the pre-planned memory pool. The GPU executes compiled kernels asynchronously on CUDA streams; Python holds shadows and inspects only metadata.

Synchronization points (the only moments Python blocks): `.item()`, `.cpu()`, `.numpy()`, `print()`, data-dependent control flow, unrecognized operations. Everything else returns immediately. For 1,000 operations with one `loss.item()`: 999 shadow returns (~2us total) + 1 sync (~10us).

## 4.5 Execution Tiers

Three tiers of increasing optimization:

**Tier 1 (implemented).** Eager replay with divergence detection. Operations replay in recorded order using Vessel dispatch for execution but the static memory plan for allocation. Guards check hash consistency at each operation. Validates the recording and divergence infrastructure without custom kernel compilation.

**Tier 2 (designed).** Shadow handle execution. Foreground returns pre-allocated handles; background pre-schedules kernel launches on CUDA streams. Python and GPU fully decoupled. Target: ~2ns per Vessel dispatch (one pointer advance + one hash guard check).

**Tier 3 (designed).** CUDA Graph capture and replay. Entire compiled iteration captured as a CUDA Graph, replayed in one API call. Target: ~50ns per iteration, independent of operation count. Requires static shapes across iterations.

Each tier is a strict superset of the previous tier's correctness infrastructure. Divergence detection and fallback are identical across tiers.
# 5. Formal Verification (FX)

The type-system layer and effects system are implemented. Z3 integration has scaffolding and an enhanced solver fork. Reflection-based verification requires GCC 16 with `-freflection` and is partially implemented. The Lean 4 formalization accompanies the implementation as an independent proof artifact.

## 5.1 Verification Layers

| Layer | Mechanism | Guarantee | Scope |
|-------|-----------|-----------|-------|
| 4 | Z3 SMT solver | Universal (forall x. P(x)) | Mathematical properties over all inputs |
| 3 | `consteval` | Bounded (N inputs, UB-free) | Implementation correctness for exercised paths |
| 2 | Static reflection | Structural (every field, every struct) | Completeness and consistency of data layout |
| 1 | Type system | API boundaries (zero-cost) | Compile error on misuse |

Each layer proves what the layers below cannot. Layer 1 prevents calling the wrong function. Layer 2 verifies struct completeness. Layer 3 proves UB-freedom for exercised inputs. Layer 4 proves mathematical properties for all inputs.

Crucible proves: memory plan non-overlap, hash function quality, protocol deadlock freedom, kernel access safety, algebraic combining laws. Crucible does NOT prove: memory ordering on real hardware (ThreadSanitizer), floating-point stability (empirical bounds), global kernel optimality (model fidelity limitation).

## 5.2 Z3 Integration

Crucible integrates a Z3 fork enhanced with CaDiCaL dual-mode search (VSIDS + VMTF switching), ProbSAT local-search walker, on-the-fly self-subsumption, arena-based clause allocation, and several additional optimizations. The enhanced SAT solver provides speedups on bitvector problems that dominate Crucible's proof obligations.

**Build integration.** A CMake custom target (`crucible_verify`) compiles an executable that links `libz3` and Crucible headers, encodes properties as SMT formulas, and runs proofs. If any theorem fails, the build fails. All targets depend on verification passing.

**What Z3 proves --- memory.** Arena alignment: the expression `(base + offset + align - 1) & ~(align - 1)` produces a correctly aligned result for all 2^192 (base, offset, align) combinations where align is a power of two. Memory plan non-overlap: for N tensors (parameterized, up to a bound) with arbitrary lifetimes, sizes, and alignments, the sweep-line allocator produces non-overlapping assignments for all simultaneously live tensors. Saturation arithmetic: `mul_sat(a, b)` equals `min(a*b, UINT64_MAX)` for all 2^128 input pairs.

**What Z3 proves --- hashing.** `fmix64(x) ≠ x` for all x ∈ [0, 2^64). Each of 64 input bit flips changes at least 20 output bits, for all inputs. Content hash determinism: same field values produce same hash output.

**What Z3 proves --- protocol.** SPSC ring: bitmask indexing `h & MASK` equals `h % CAPACITY` for all power-of-two capacities and all head values. Enqueue preserves the invariant `head - tail ≤ CAPACITY` for all (head, tail) pairs. No index collision when `0 < used < CAPACITY`.

**What Z3 proves --- kernels.** Each compiled kernel configuration is verified for: no shared-memory bank conflicts (32 threads access different banks or broadcast), coalesced global memory access (minimum 128B transactions), no out-of-bounds access (thread address < buffer size for all valid block/thread indices), register count within limits.

**Z3 as optimizer.** Beyond verification, Z3's `optimize` module solves for optimal configurations: minimum-footprint memory plans, roofline-optimal kernel parameters, and topology-optimal parallelism factorizations. When used as an optimizer, the result is optimal within the encoded model; correctness constraints (no OOB, no bank conflicts) are simultaneously enforced.

## 5.3 Consteval Verification

A `consteval` function executes inside the C++ compiler's abstract machine, which is a sound interpreter: null dereference, out-of-bounds access, signed integer overflow, use-after-free, double-free, memory leak, and uninitialized read are all compile errors. If a `consteval` function completes, every execution path was free of undefined behavior for the supplied inputs.

**Dual-mode Arena.** The Arena allocator uses `if consteval` to select between compile-time tracking (compiler verifies alignment, bounds, no overlap, no leak) and runtime execution (zero-overhead bump pointer). Same algorithm, two execution modes.

**Consteval fuzzing.** Philox4x32 is pure integer arithmetic and trivially `constexpr`. Crucible generates deterministic random inputs at compile time: random memory plans (verify non-overlap), random topological sorts (verify edge ordering), random struct instances (verify serialization roundtrip and hash determinism). The compiler proves all trials are UB-free.

**Finite-state model checking.** The SPSC ring protocol has finite state: (fg_phase × bg_phase × ring_count). For small capacity, exhaustive BFS at compile time verifies deadlock freedom and liveness. The mode transition state machine (INACTIVE/RECORD/COMPILED/DIVERGED) is similarly exhausted.

## 5.4 Reflection-Based Structural Verification

Using C++26 static reflection (P2996, GCC 16 with `-freflection`), Crucible performs compile-time structural checks. Reflect.h (guarded by `CRUCIBLE_HAS_REFLECTION`) implements:

`reflect_hash<T>()` iterates all non-static data members via `nonstatic_data_members_of(^^T, access_context::unchecked())` and hashes each field with `fmix64`: integral types via cast, floats via `bit_cast`, pointers via `reinterpret_cast<uintptr_t>`, C arrays element-by-element, nested structs recursively. Adding a field automatically includes it in the hash; forgetting is structurally impossible.

`reflect_print<T>()` generates debug output for every field, dispatching on type category via the same reflection machinery.

Four structural checks per field: (1) `has_default_member_initializer` (InitSafe: every field has NSDMI), (2) offset sequence (no unintended padding holes), (3) `type_of` (TypeSafe: raw uint32_t/uint64_t with ID-like names triggers compile error), (4) `sizeof(T)` (MemSafe: catches silent layout changes). Cross-checks verify hand-written hash functions agree with reflected versions.

## 5.5 Type System Enforcement

**Capability tokens** (Effects.h). Three effect types with private constructors: `fx::Alloc` (heap allocation), `fx::IO` (file/network I/O), `fx::Block` (blocking operations). Three authorized contexts: `fx::Bg` (background thread: all three), `fx::Init` (initialization: Alloc + IO, no Block), `fx::Test` (unrestricted). Each context is a 1-byte struct with `[[no_unique_address]]` members --- `static_assert(sizeof(fx::Bg) == 1)`. C++20 concepts enforce requirements: `fx::CanAlloc<Ctx>` checks for an `alloc` member; `fx::Pure<Ctx>` checks for absence of all effects. Foreground hot-path code holds no tokens; the compiler rejects effectful calls. Example: `Arena::alloc(fx::Alloc, size_t, size_t)` --- the first parameter is a compile-time proof of authorization.

**Strong types** (Types.h). `CRUCIBLE_STRONG_ID(Name)` generates a uint32_t wrapper with explicit constructor, `.raw()` unwrap, `.none()` sentinel (UINT32_MAX), `.is_valid()` check, `operator<=>`, and no arithmetic. Five ID types: OpIndex, SlotId, NodeId, SymbolId, MetaIndex. `CRUCIBLE_STRONG_HASH(Name)` generates a uint64_t wrapper with explicit constructor, `.raw()`, `.sentinel()` (UINT64_MAX), `operator<=>`, and no arithmetic. Six hash types: SchemaHash, ShapeHash, ScopeHash, CallsiteHash, ContentHash, MerkleHash. All are `static_assert`-verified to have the same size as the underlying integer and are trivially relocatable.

**Typestate.** Mode transitions (INACTIVE -> RECORD -> COMPILED -> DIVERGED) are encoded as types. `start_recording(Inactive)` compiles; `start_recording(Compiled)` has no overload.

## 5.6 Lean 4 Formalization

Independent of the C++ implementation, Crucible maintains a Lean 4 formalization that proves properties of every core data structure and algorithm. The formalization covers:

- **Arena:** pairwise disjointness of allocations, alignment correctness, bounded fragmentation.
- **PoolAllocator:** slot disjointness for overlapping lifetimes, offset + size <= pool_bytes.
- **Memory plan:** sweep-line non-overlap, offset + size <= pool size, alignment waste bounds.
- **SPSC ring:** FIFO ordering for N push/pop sequences, batch drain correctness, capacity invariant preservation.
- **MetaLog:** bulk read-after-write correctness for all indices.
- **Iteration detector:** detection latency bounds (exactly K ops), false positive probability bounds under hash independence assumptions.
- **TraceGraph:** CSR construction correctness, bidirectional consistency.
- **Graph IR:** DCE fixpoint convergence, topological sort validity.
- **Merkle DAG:** collision probability bounds, structural diff correctness, replay completeness.
- **Scheduling:** Graham's list scheduling bound, Brent's theorem, critical path optimality.
- **Roofline:** multi-level cache model, wave quantization, correction factor model.
- **Fusion:** chain selection optimality, occupancy constraints.

All theorems are proved with no `sorry` (unresolved proof obligations). The formalization serves as a specification that the C++ implementation targets and as an independent check on algorithm correctness.
# 6. Hardware Adaptation

Longitude and Augur are designs; the hardware profiling interfaces they depend on (CUPTI, NVML, rocprofiler) are standard vendor APIs. CostModel.h implements the hardware specification and cost model structures.

## 6.1 Hardware Profiling

CUPTI (NVIDIA) and rocprofiler (AMD) provide per-kernel counters: SM/CU utilization, memory bandwidth saturation, achieved occupancy, tensor core utilization, register spill count, cache hit rates, warp execution efficiency, pipeline stall reasons. NVML and rocm-smi provide device-level metrics: clocks, power, temperature, ECC errors, throttle events, usable memory. Crucible abstracts these behind a vendor-agnostic profiling interface.

## 6.2 Longitude: Startup Calibration

Longitude runs at startup and on topology change (Relay failure, new Relay, sustained drift detected by Augur). It populates the HardwareProfile struct (CostModel.h) with measured values, replacing the nominal presets (B200, H100, MI300X, A100).

**Phase 1: GPU profiling.** Per-GPU, in parallel: GEMM benchmark measures sustained compute throughput (a thermally throttling GPU delivers less than spec). Streaming copy measures actual HBM bandwidth. Clock readings under load reveal true boost frequency. NVML/rocm-smi report power, temperature, ECC errors, usable memory after driver overhead. Output: a calibrated HardwareProfile per device with measured values for all fields (num_sms, smem_per_sm, hbm_bw, peak_fp16, etc.).

**Phase 2: Network probing.** All-pairs: ping-pong latency, unidirectional and bidirectional bandwidth, topology detection (NVSwitch, NVLink, PCIe, InfiniBand, RoCE, TCP). Output: weighted network graph of actual measured connectivity.

**Phase 3: Kernel calibration.** Representative kernels benchmarked at several shapes. Ratio of actual measured time to analytical prediction yields a correction factor per kernel class, absorbing cache behavior, instruction scheduling, TLB pressure. With correction factors, model predictions track measurements within a few percent.

**Phase 4: Configuration optimization.** Given measured profiles and topology, formulate system configuration as SMT constraint optimization. Variables: parallelism degrees (TP, DP, PP, EP, CP), GPU-to-group assignment, communication algorithm per collective per message size, gradient bucket sizes, per-tensor checkpointing decisions, per-op precision assignment, batch size. Constraints: memory limits, communication feasibility, hardware capabilities. Objective: minimize predicted iteration time. Result: complete configuration optimal within the encoded model.

## 6.3 Augur: Continuous Monitoring

Augur runs every iteration, comparing predictions against measurements.

**Digital twin.** From the Merkle DAG, kernel cost predictions (roofline model with correction factors), and the memory plan, Augur predicts each iteration. Per-kernel: execution time, utilization, bottleneck classification (COMPUTE, MEMORY_BW, COMMUNICATION, BUBBLE, IMBALANCE). Per-iteration: critical path through the DAG, forward/backward/optimizer/communication breakdown.

**Drift detection.** Compare predicted vs actual per iteration. Small sustained deviations update correction factors. Large sustained deviations trigger diagnosis: thermal throttling (NVML clock drop), hardware degradation (ECC error increase), network congestion (latency spike), workload change (dynamic shapes). Confirmed hardware change triggers Longitude recalibration.

**Bottleneck identification.** Classify dominant bottleneck, rank optimizations by expected_speedup * confidence. This drives Keeper recommendations.

## 6.4 Model Intelligence

Periodically, Augur computes diagnostics of the Vigil's training dynamics from actual tensor data:

- **Hessian spectrum.** Top eigenvalues via Lanczos iteration (Hessian-vector products, O(N) cost each). Yields smoothness, condition number, convergence rate bounds [Nesterov 1983].
- **Gradient health.** Per-layer gradient norms, signal-to-noise ratio, Jacobian singular values. Detects vanishing/exploding gradients with layer attribution.
- **Representation capacity.** Per-layer effective rank via randomized SVD [Halko et al. 2011]. Identifies overparameterization and dead neurons (near-zero variance).
- **Layer redundancy.** CKA [Kornblith et al. 2019] between adjacent layers. High CKA -> layers compute nearly identical functions -> pruning candidate.
- **Convergence prediction.** Exponential loss curve fit with confidence intervals. Chinchilla scaling law analysis [Hoffmann et al. 2022].

Each diagnostic is a standard technique; Crucible's contribution is integrating them into the runtime to drive automated optimization (Section 8).
# 7. Distribution and Persistence

These components are designs.

## 7.1 The Canopy: Masterless Mesh

One Keeper per Relay. Gossip for discovery, Raft for critical state consensus, CRDTs for eventually-consistent metrics. No master node.

Any Keeper can propose changes (topology updates, optimization activations, Relay eviction). Changes take effect at the same iteration boundary across all participating Relays, using the same atomic swap mechanism as local DAG updates (Section 3.4).

**Spot-aware.** On 30-second eviction signal, the Keeper notifies the Canopy. Reshard to N-1 Relays --- redundant copies already exist on other Relays (Section 7.3), so no data migration is needed. New instance: Keeper discovers Canopy, loads Cipher, compiles device-specific kernels, joins.

## 7.2 Heterogeneous Compute

A single Vigil executes across Relays with different GPUs. The MemoryPlan carries device context (`DeviceType`, `device_idx`, `device_capability`) so each plan is self-describing. Each Relay compiles kernels for its local device via `(ContentHash, device_capability)`. The DAG is shared; only compiled kernels and memory plans differ.

**Load balancing.** Micro-batches distributed proportionally to measured throughput (LOR). An H100 receives more micro-batches than a 3090; both fully utilized. Gradients weighted by actual batch size.

**Multi-backend transport.** UCX: GPUDirect RDMA (NVIDIA), ROCm-aware RDMA (AMD), host-staged (TPU). Transport selected per link from Longitude measurements. Not NCCL-locked: cross-vendor GPU-to-GPU transfers (AMD to NVIDIA) route through the InfiniBand fabric with zero CPU staging.

**Adaptive topology.** The Canopy probes the actual network state continuously (N*N latency and bandwidth matrices). Per-collective, per-message-size algorithm selection: ring for bandwidth-bound (gradient all-reduce, 128MB), tree for latency-bound (parameter broadcast, 2MB), recursive halving-doubling for balanced (activation all-gather, 32MB), direct for sparse (expert routing all-to-all). Topology swaps at iteration boundaries via the same atomic mechanism as DAG branches. The Canopy routes around degraded links.

**DiLoCo enhancement.** DiLoCo (Distributed Local Optimization) runs DDP within each island, with periodic outer sync across islands. Crucible enhances every axis: (1) adaptive H --- measure parameter drift between islands, sync sooner on high drift, less on low drift. (2) Heterogeneous islands --- each island runs at full speed with different inner step counts, pseudo-gradients weighted by actual work. (3) Selective sync --- skip parameters with small deltas across H steps, saving 60%+ WAN bandwidth. (4) Compressed pseudo-gradients --- top-K sparsification with error feedback plus int8 quantization yields 50--100x bandwidth reduction. (5) Async outer sync --- no barriers, staleness-aware weighting: `weight = 1/(1 + staleness/H)`. (6) Hierarchical --- NVLink every step, InfiniBand every 5, WAN every 50, H auto-tuned per level from measured latencies, expressed as nested LoopNodes.

**5D parallelism auto-tuning.** Crucible measures actual per-dimension costs at runtime: TP all-gather latency, PP pipeline bubble, DP reduce-scatter time, EP all-to-all time, CP chunk transfer time. Given measurements, simulate alternative configurations; if predicted improvement exceeds a threshold, try for a few iterations, commit or rollback. The parallelism configuration evolves during training.

## 7.3 Redundancy

When a Relay fails in current distributed training systems, work since the last checkpoint is lost (typically 3-8 minutes). The Canopy implements configurable redundancy through an overlapping factor α:

- α = 0: pure FSDP. Each Relay stores only its own parameter shard. Any failure is catastrophic.
- α = 0.125: each Relay stores its shard plus 1/8 of its neighbor's. Survives 1 Relay failure. Memory overhead: 12.5%.
- α = 1.0: pure DDP. Every Relay stores everything. Survives N-1 failures. Maximum memory overhead.

Redundancy updates are pipelined into communication dead time (the network is idle during forward pass, backward pass, and optimizer step; redundancy copies transfer during these windows). On Relay failure: surviving Relays already hold the failed Relay's data. Reshard to N-1, recompute memory plans. Resume from exactly where the failed Relay left off.

Dynamic α: the Keeper on each Relay monitors hardware health (ECC errors, thermal events). A Relay accumulating errors gets higher α for its neighbors. Healthy Relays reduce α to save memory. Topology-aware placement ensures redundant copies span failure domains (different racks, different power supplies).

## 7.4 The Cipher: Event-Sourced Persistence

Event-sourced, not snapshot-based. Each iteration: the DAG chain is persisted (a few KB per step). Weight snapshots are periodic. Recovery to step T+500 from snapshot at T: load snapshot, replay 500 steps deterministically (DAG fixes execution order, memory plan fixes addresses, Philox4x32 fixes RNG state --- counter-based, platform-independent).

**Three tiers:**

- **Hot:** other Relays' RAM from redundancy (Section 7.3). Single Relay failure: immediate recovery, zero cost.
- **Warm:** local NVMe per Relay. Relay reboot recovery: seconds.
- **Cold:** durable object storage (S3, GCS). Total Canopy failure recovery: minutes.

The Cipher also stores the KernelCache, FX proof certificates, Longitude calibration data, and Augur's history. On migration: universal proofs (hash, protocol) are inherited. Hardware-specific state (kernels, topology) is regenerated.

## 7.5 Deterministic Replay

Four invariants produce bit-identical execution on the same hardware: the Merkle DAG fixes execution order, the MemoryPlan fixes tensor addresses, the KernelCache fixes kernel selection, and Philox4x32 (counter-based, platform-independent, seeded from a master counter in the Cipher) fixes random state. Each op derives its key from `hash(master_counter, op_index, content_hash)`; each thread generates `philox(thread_idx, op_key)` --- ~10 integer instructions in registers.

Cross-hardware replay preserves semantics but not bits (floating-point non-associativity in different kernel implementations).

Applications: exact reproducibility for research, regression testing between DAG versions, time-travel debugging (replay to any step, extract any activation, trace anomalies backward through the dataflow graph to root cause).
# 8. Model-Aware Optimization

Designs drawing on established ML techniques. Crucible's contribution is the infrastructure: each optimization is a DAG branch (Section 3.4), verified before activation, rollbackable via atomic swap.

## 8.1 Token-Level Adaptation

**Token merging.** Pairwise cosine similarity between adjacent representations after layer N; tokens above threshold are merged (averaged). Attention is O(n^2); 4x fewer tokens = 16x less attention. Threshold adapts per input and per layer [Bolya et al. 2023].

**Early exit.** ||h_N - h_{N-1}|| per token below threshold -> freeze, skip remaining layers. Tokens bucketed into convergence groups for batch efficiency.

**Adaptive patching (images).** Quadtree decomposition by information content. Low-information regions get fewer, larger patches.

Each adaptation is a BranchNode with the non-adapted path as fallback arm.

**Per-token precision.** High-information tokens run in FP16; low-information tokens in INT8 or INT4. Separate kernels per precision group, dispatched from measured information content per token per layer.

## 8.2 Layer-Level Analysis

Operations grouped by ScopeHash; each layer analyzed independently via Augur diagnostics (Section 6.4).

**Attention head classification.** From recorded attention matrices: positional heads (diagonal band) -> depthwise convolution O(n*k) vs O(n^2); global heads (fixed landmarks) -> gather + broadcast O(n); averaging heads (uniform) -> mean pooling O(n); dead heads (near-zero output) -> removed. Content-routing heads retained or replaced with hash-based routing. For a 144-head transformer, typical result: ~85 sparse-attention + 15 conv + 20 pool + 10 removed + 14 routing. Total attention cost drops ~60%.

**Local learning signals.** Per-layer loss functions (predictive coding, contrastive, reconstruction) inserted as DAG modifications, providing gradient signal with depth-1 backpropagation paths.

**Per-layer gradient strategy.** From gradient SNR, Jacobian rank, gradient norms: high SNR -> standard backprop; moderate SNR -> K-FAC natural gradient [Martens and Grosse 2015]; near-zero SNR -> synthetic gradients; converged -> frozen. 50-70% of layers skippable late in training.

**Matrix structure discovery.** Not every dense matmul needs to be dense. Crucible observes weight matrices per layer and classifies: full-rank (keep dense); low-rank (effective rank r << d, replace W(d*d) with A(d*r)*B(r*d), 2x cheaper at r=d/4); near-Toeplitz (diagonal structure, replace with depthwise conv + small correction, 10x cheaper); highly sparse (>95% near-zero, cuSPARSE); block-diagonal (independent subspace matmuls, naturally parallelizable across streams). Each replacement is a DAG branch verified against the original.

**NaN/Inf early kill.** Lightweight `isfinite` checks at numerically sensitive points (softmax, log, exp, division) --- ~1us per check. The moment a NaN appears, Crucible catches it before propagation through 50 more ops. Response: rollback to previous iteration parameters, skip bad batch, continue. Current PyTorch: silent NaN propagation, user notices 20 minutes later.

## 8.3 Architecture Mutation

The DAG IS the architecture. Every modification is a BranchNode: old architecture (arm A) vs new (arm B), verified on validation data, committed or discarded via atomic swap.

**Layer growing.** Loss plateau + capacity analysis -> insert layer at highest-gradient position. Initialize via identity/distillation/random.

**Layer pruning.** CKA > 0.95 between adjacent layers -> skip redundant layer.

**Width mutation.** Effective rank << hidden dimension -> reduce width via SVD projection + adapters. A 4096-dim layer with effective rank 600 -> insert W_down(4096x600) * W_up(600x4096), ~3.4x cheaper.

**Progressive growing.** Start small (e.g. 4 layers, d=512), grow on plateaus, widen when rank saturates, prune when redundant. The model's size trajectory is determined by data and loss landscape, not by human guess at step 0. Typical trajectory: 4 layers at step 0 -> 6 at 10K (loss plateau) -> 8 layers, d=1024 at 30K (depth needs capacity) -> stable at 100K -> pruned to 9 layers at 150K (dead layer removed).

**Model composition.** Two pre-trained models composed by DAG splicing: connect vision encoder DAG to language model DAG with an adapter RegionNode. Both subgraphs retain content_hashes; compiled kernels reused from the KernelCache. Only the adapter needs compilation.

**Activation function evolution.** Per-layer: try SwiGLU, ReLU, GELU as branch arms, measure quality impact. Typical result: SwiGLU for early layers (expressiveness), ReLU for middle (cheaper, sufficient), GELU for final (refinement).

## 8.4 Training Optimization

The entire training loop is in the DAG and therefore observable and modifiable.

**Meta-gradients.** d(val_loss)/d(lr) via one additional backward pass. Learning rate, weight decay, momentum parameters tune themselves by gradient descent on validation loss. No search.

**Per-layer LR from curvature.** Hessian diagonal (periodic Hessian-vector products) gives per-parameter curvature. Optimal lr proportional to 1/H_ii.

**Curriculum learning.** Per-sample loss observable during recording. Order by difficulty (easy-to-hard, hard-first, random). The ordering is a DAG-level decision evaluated empirically.

**Automatic mixed precision.** Run each op in FP32, BF16, FP8; measure per-op quality impact; select cheapest precision within tolerance. Per-op, per-training-stage. Not a static allow-list --- discovered from this model on this data.

**Manifold Mixup.** Interpolate hidden states between samples at intermediate layer K: `h_mix = a*h_A + (1-a)*h_B`. Forward remainder, compute loss against interpolated label. Layer K chosen by linear probe accuracy. Generates training signal from latent geometry without new data.

**Representation steering (inference).** Compute direction vectors in latent space: mean truthful activations minus mean hallucinated activations. Add `alpha * direction` at optimal layer during inference. No weight changes; one vector addition per layer, negligible cost, measurable behavior modification. Different deployment contexts use different steering vectors.

**Loss function evolution.** Auxiliary losses (contrastive at layer 6, regularization against representation collapse) weighted by meta-gradients: `d(val_loss)/d(aux_weight)` determines whether the auxiliary loss helps.

**Optimizer evolution.** Adam, AdaFactor, Lion, and learned update rules as DAG branches. Crucible evaluates each on validation slices and activates the winner. The optimizer itself becomes a tunable component.

## 8.5 Verification and Activation

Uniform protocol for all optimizations:

1. Augur diagnoses and predicts expected improvement.
2. Optimization implemented as BranchNode or DAG modification.
3. FX verifies safety (memory bounds, kernel access).
4. New branch evaluated on validation data alongside current branch.
5. Quality maintained + improvement confirmed -> Keeper activates via atomic swap.
6. Quality degrades -> branch discarded.

Built-in A/B testing with instant rollback. No optimization is irreversible.
# 9. Implementation Status

## 9.1 What Is Built

~9,500 lines of C++26 across 17 headers in `include/crucible/`, 24 tests.

**Recording pipeline.** TraceRing: 65,536 entries x 64B = 4MB ring + 3 parallel arrays (MetaIndex, ScopeHash, CallsiteHash) totaling ~5.25MB. `alignas(64)` head/tail on separate cache lines, `cached_tail_` optimization, next-slot prefetch. MetaLog: 1M entries x 144B = ~144MB, `aligned_alloc(64, ...)`, same SPSC protocol with cached tail and bulk memcpy. Vessel adapter for PyTorch ATen dispatch.

**Graph construction.** TraceGraph: bidirectional CSR with four edge kinds (DATA_FLOW, ALIAS, CONTROL_FLOW, SCALAR_FLOW), 12-byte Edge structs, O(V+E) counting-sort construction. IterationDetector: K=5, 128 bytes (2 cache lines), ~1ns steady-state hot path, two-match confirmation. PtrMap: open-addressing with generation counter (no memset between iterations). Scratch buffers allocated once and reused.

**Merkle DAG.** TraceNode (24B base), RegionNode (content hash, atomic CompiledKernel*, TraceEntry array, MemoryPlan*, first_op_schema, measured_ms), BranchNode (12B Guard with 5 kinds, sorted arms for O(log n) binary search), LoopNode (64B = 1 cache line, FeedbackEdge array, REPEAT/UNTIL termination, body_content_hash). Content hashing via wymix-fold. Merkle hashing with salts to distinguish node kinds. KernelCache: lock-free open-addressing, default capacity 4096, CAS insert. Serialization and deserialization for Cipher.

**Memory planning.** TensorSlot (40B): offset_bytes, nbytes, birth_op, death_op, dtype, device, is_external, slot_id. MemoryPlan: pool_bytes, num_slots, num_external, device context, distributed topology. `compute_storage_nbytes()` handles negative strides. Counting sort O(n+k) sweep-line.

**Compiled Tier 1.** ReplayEngine, CrucibleContext, `dispatch_op` with four-level divergence detection (schema, shape, scope, callsite hash), divergence recovery via `reset_requested` atomic flag.

**Kernel taxonomy** (CKernel.h). 146 ops in two sections: Section 1 (1--99, frozen since CDAG_VERSION=3): linear algebra (8), convolution (6), attention (4), normalization (6), activations (13), elementwise binary (9), unary (10), reductions (8), pooling (8), data movement (16), embedding (2), copy (2), vision (3), fused (4). Section 2 (100--146): linalg decompositions (9), SSM/recurrence (6), production inference (6), 3D rendering (4), structured matrix/graph (5), collective comms (10), I/O (4), RNG (2), sync (1). Two-phase registration: Vessel registers schema_hash -> CKernelId at startup; `classify_kernel()` called during `build_trace()`.

**Graph IR** (Graph.h). GraphNode: 64B = 1 cache line, NodeId, NodeKind (INPUT/CONSTANT/POINTWISE/REDUCTION/SCAN/SORT/EXTERN/TEMPLATE/MUTATION/NOP), ndim, nred, dtype, ReduceOp, num_inputs, symbolic Expr** size/stride, ComputeBody* or ExternInfo* body, GraphNode** inputs, num_uses, schedule_order, group_hash, fused_group_id. Inst: 8B SSA micro-op (MicroOp enum with ~50 opcodes, ScalarType dtype, 3 uint16 operands). ComputeBody: flat instruction array directly emittable to CUDA C++. FuseKind classification (NONE/REGISTER/SMEM/EPILOGUE/PROLOGUE/BROADCAST). ExprPool: Swiss table interned expressions (32B Expr nodes, wyhash-style hashing, pointer equality). SymbolTable for per-symbol metadata. Transforms: DCE (fixpoint), CSE, topological sort (Kahn's O(V+E)), RAUW.

**Effects system** (Effects.h). `fx::Alloc`, `fx::IO`, `fx::Block` with private constructors, friend access for `fx::Bg`/`fx::Init`/`fx::Test`. C++20 concepts: `CanAlloc`, `CanIO`, `CanBlock`, `Pure`. All contexts verified at 1 byte via `static_assert`.

**Reflection** (Reflect.h, GCC 16 only). `reflect_hash<T>()` and `reflect_print<T>()` via P2996 `nonstatic_data_members_of` + expansion statements.

**Cost model** (CostModel.h). HardwareProfile struct (compute fabric, register file, 4-level memory hierarchy, bandwidth/latency, peak throughput per dtype). Presets: B200 (sm_100), H100 (sm_90), MI300X (gfx942), A100 (sm_80). KernelConfig (tile_m/n/k, pipeline_stages, warps_per_block, smem_bytes, regs_per_thread, vec_width). `validate_config()` with C1--C8 constraints. `evaluate_cost()` roofline model.

**Toolchain.** C++26 (`-std=c++26`). Primary: Clang 22.1.0 + libc++ 22. Fallback: GCC 15.2.1 + libstdc++ 15. Bleeding-edge: GCC 16.0.1 for reflection. All headers compile clean on both primary toolchains with zero warnings.

## 9.2 Lean 4 Formalization

36 modules, 18,231 lines, 1,312 theorems, zero `sorry`. Covers Arena (pairwise disjointness, alignment), MemoryPlan (sweep-line non-overlap), PoolAllocator, SPSC ring (FIFO ordering, batch drain), MetaLog, IterationDetector (detection latency bounds), TraceGraph (CSR consistency), Merkle DAG (collision probability, structural diff), Graph IR (DCE fixpoint, topological sort validity), scheduling (Graham's bound, Brent's theorem), roofline model (multi-level cache, wave quantization, correction factors), and fusion (chain optimality, occupancy). Built with Lean 4.28.0 + local Mathlib.

Correspondence between Lean specification and C++ implementation is maintained by design discipline and cross-referenced invariant names.

## 9.3 What Is Designed but Not Built

- **FX Z3 integration:** Enhanced Z3 fork exists. Build scaffolding (`verify/`) exists. Proof suites partially encoded.
- **SMT kernel synthesis:** CostModel.h complete (HardwareProfile, KernelConfig, validate_config, evaluate_cost). Z3 encoding in progress.
- **Shadow handles (Tier 2):** ConductorTensorImpl design complete. Not integrated with Vessel.
- **CUDA Graph replay (Tier 3):** Depends on Tier 2.
- **Longitude:** Calibration protocol designed. Depends on CUPTI/NVML integration.
- **Augur:** Digital twin designed. Depends on Longitude.
- **Canopy:** Mesh protocol designed. Not implemented.
- **Cipher (full):** Serialization exists. Three-tier persistence and distributed replication not implemented.
- **Model-aware optimizations:** Depend on Augur diagnostics.

## 9.4 What Is Next

Phase 2: FX core (Z3 proof suites, consteval infrastructure, reflection checks). Phase 3: Longitude (calibration, topology optimization). Phase 4: Augur (digital twin, monitoring). Phase 5: Tiers 2--3 (shadow handles, CUDA Graph). Phase 6: Canopy, Keepers, full Cipher. Phase 7: model-aware optimizations.
# 10. Related Work

## 10.1 ML Compilers and Runtimes

**XLA** [Google 2017] compiles whole-program HLO graphs with static shapes --- fusion, layout assignment, memory planning at compile time. Crucible shares whole-program compilation goals but differs: recording-based (no tracing/symbolic execution), content-addressed caching (cross-model reuse), graduated divergence handling (not requiring static graphs).

**TVM/Ansor** [Chen et al. 2018, Zheng et al. 2020]. Search-based autotuning with learned cost models. Crucible proposes SMT constraint optimization as an alternative: trading search time for solver time, gaining formal safety guarantees. Empirical comparison is future work.

**Triton** [Tillet et al. 2019]. Python-based kernel language with programmer-guided tiling. Crucible operates at a different level (framework dispatch interception vs kernel authoring). SMT-generated configurations could target Triton's tiling model as a backend.

**TorchInductor/torch.compile** [PyTorch 2.0, 2023]. Traces Python into FX graphs, generates Triton kernels. Crucible records at ATen dispatch level, avoiding graph breaks on unsupported operations. The Merkle DAG extends FX graphs with content addressing, versioning, and persistence.

**Halide** [Ragan-Kelley et al. 2013]. Algorithm/schedule separation. Crucible's DAG/KernelCache separation follows the same principle at runtime level.

## 10.2 Automated Parallelism

**FlexFlow** [Jia et al. 2019]: simulator-guided MCMC search over parallelism configurations. **Alpa** [Zheng et al. 2022]: two-level optimization (ILP for inter-op pipeline, DP for intra-op tensor). Crucible formulates all dimensions (TP, DP, PP, EP, CP), communication algorithms, and memory management as a single SMT problem, solving jointly. Tradeoff: solver scalability vs decomposition approximation error.

**Megatron-LM** [Shoeybi et al. 2019, Narayanan et al. 2021] and **DeepSpeed** [Rasley et al. 2020] provide efficient manual-configuration parallelism. Crucible automates configuration selection over the same underlying mechanisms.

## 10.3 Formal Verification in Systems

**seL4** [Klein et al. 2009]: fully verified microkernel. **CompCert** [Leroy 2009]: verified C compiler. **CertiKOS** [Gu et al. 2016]: verified concurrent OS kernel. These verify complete system stacks. Crucible's scope is narrower --- runtime invariants (memory safety, protocol correctness, kernel bounds) rather than full functional correctness --- but targets ML runtimes where formal verification has not been applied.

**Dafny** [Leino 2010] and **F\*** [Swamy et al. 2016]: verified programming languages with integrated proof obligations. Crucible uses Z3 directly for domain-specific SMT formulas rather than verifying general programs. Less general but more targeted: specific invariants as specific formulas, proved as part of the build.

## 10.4 Fault-Tolerant and Persistent Training

**CheckFreq** [Mohan et al. 2021]: optimal checkpoint frequency via cost modeling. **Gemini** [Zhuang et al. 2023]: memory-efficient checkpointing. Both optimize within snapshot-based paradigm. Crucible's Cipher uses event sourcing: DAG chain (KB per step) + periodic snapshots. Enables recovery to arbitrary training steps.

**Varuna** [Athlur et al. 2022]: fault tolerance for pipeline-parallel spot instances. Crucible extends with configurable redundancy (alpha parameter), topology-aware placement, and Cipher integration for zero-loss recovery.

## 10.5 Content-Addressed Systems

**Git** [Torvalds 2005]: Merkle trees for version control. **IPFS** [Benet 2014]: content-addressed distributed filesystem. **Nix** [Dolstra et al. 2004]: content-addressed reproducible builds. Crucible applies the same principle to computation graphs: structural diff, deduplication, distributed sharing for compiled ML kernels.
# 11. Research Roadmap

Phase 1 (foundation) is complete. Subsequent phases produce independently evaluable artifacts.

**Phase 2: FX Core.** Z3 proof suites for all core invariants, consteval infrastructure, reflection checks, proof-carrying build. Eval: proof coverage, solver time scaling, structural check false positive rate.

**Phase 3: Longitude.** GPU profiling, network probing, kernel calibration, Z3 topology solver. Eval: calibration accuracy (predicted vs measured per shape/device), solver time at various cluster sizes, configuration quality vs expert tuning.

**Phase 4: Augur.** Digital twin, drift detection, bottleneck diagnosis, model intelligence. Eval: prediction accuracy, regression detection latency, diagnostic accuracy.

**Phase 5: Compiled Tiers 2--3.** ConductorTensorImpl, async kernel execution, CUDA Graph replay. Eval: per-op dispatch overhead (Tier 2 vs 1 vs eager), per-iteration overhead (Tier 3 vs 2), end-to-end throughput on standard benchmarks.

**Phase 6: Distribution.** Canopy (gossip, Raft, CRDTs), Keepers, full Cipher (three-tier persistence, configurable alpha redundancy). Eval: recovery time, redundancy overhead, heterogeneous scaling efficiency.

**Phase 7: Model-Aware Optimization.** Token adaptation, layer analysis, architecture mutation, training optimization. Integrated pipeline: Augur diagnoses, FX verifies, Keeper activates. Eval: time-to-accuracy improvement, quality preservation under automated mutation, comparison with expert manual optimization.

Each phase produces a focused evaluation against established baselines. We plan to make prototype, formalization, and evaluation artifacts publicly available.
# 12. Conclusion

Recording computation, representing it in a content-addressed graph, and compiling from that representation enables optimizations structurally impossible in eager execution: cross-model kernel reuse via semantic hashing, static memory planning from observed lifetimes, formal verification of runtime invariants, seamless compiled-to-eager fallback via graduated divergence detection, and event-sourced persistence enabling deterministic replay and hardware migration.

The Merkle DAG unifies compilation target, versioning mechanism, guard system, deployment artifact, and persistence format. This eliminates the training/inference discontinuity, provides built-in A/B testing (BranchNode arms with atomic swap), and enables the Vigil to survive hardware failure through the Cipher.

The prototype demonstrates feasibility: 3--5ns recording per operation on the foreground hot path, O(V+E) graph construction, content-addressed kernel caching, static memory planning, and graduated divergence recovery. The Lean 4 formalization provides machine-checked proofs of core algorithm correctness with zero unresolved obligations. Significant work remains across all subsequent phases.

Crucible is infrastructure, not intelligence. It observes, compiles, adapts, distributes, and persists --- mechanically, from measurements, through the Merkle DAG. The Vigil determines the quality ceiling; Crucible removes the infrastructure overhead that prevents the Vigil from reaching it.
