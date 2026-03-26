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
