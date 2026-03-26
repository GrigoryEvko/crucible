# 3. Recording and Representation

This section describes how Crucible captures computation from the Vessel and builds its internal representation. The recording pipeline and graph construction are implemented and tested; the Merkle DAG structure is implemented with formal properties proved in Lean 4.

## 3.1 Vessel Interception

The Vessel is Crucible's adapter to the host framework. The current Vessel targets PyTorch's ATen dispatch mechanism --- a priority-ordered function pointer table indexed by dispatch keys. Crucible registers a dispatch key (`DispatchKey::Conductor`) at a priority above backend keys, allowing it to observe every operation before it reaches the compute backend. The Vessel abstraction is not PyTorch-specific; the same recording interface could adapt to other frameworks that expose a dispatch layer.

In recording mode, the Vessel interceptor executes six steps per operation: (1) snapshot input tensor metadata (shapes, strides, dtype, device, data pointer), (2) compute the operation's schema hash (from its registered name) and shape hash (from input dimensions), (3) execute the operation normally via redispatch to the backend, (4) snapshot output tensor metadata, (5) append metadata to the MetaLog, and (6) record the operation entry in the TraceRing. In compiled mode, the interceptor advances an operation index, checks a guard hash, and returns a pre-allocated shadow handle --- three steps with no execution, no allocation, and no redispatch.

The Vessel handles the full breadth of ATen operations, including operations with zero tensor arguments (profiler hooks, scope markers) and operations with TensorList inputs (concatenation, stacking). Scalar arguments are encoded inline (up to five int64 values per entry). Communication operations (all-reduce, all-gather) are intercepted identically to compute operations --- the same recording, timing, and optimization pipeline applies.

## 3.2 Trace Recording

Recording uses two parallel SPSC (single-producer, single-consumer) data structures communicating between the foreground thread (which runs Python and records Vessel operations) and a background thread (which builds graphs and compiles execution plans).

**TraceRing.** A ring buffer of 64-byte cache-line-aligned entries. Each entry contains the operation's schema hash, shape hash, scope hash (identifying the module), callsite hash (identifying the source location), tensor counts, scalar arguments, and a metadata index pointing into the MetaLog. The ring capacity is a power of two, enabling bitmask indexing (`index & MASK`) instead of modulo. Head and tail pointers are atomic with acquire/release ordering; no locks are used. The foreground thread writes entries at the head; the background thread reads from the tail.

**MetaLog.** A parallel ring buffer storing TensorMeta records containing sizes (up to 8 dimensions), strides, data pointer, dimension count, dtype, device type, and device index. The MetaLog is indexed by MetaIndex values written into TraceRing entries, providing the background thread with complete tensor metadata for graph construction.

Both structures are pre-allocated at initialization and never resized. The foreground thread's recording path involves no dynamic allocation, no system calls, and no contention. All graph and DAG memory is allocated from a bump-pointer arena on the background thread, providing allocation in approximately 2ns with zero fragmentation.

## 3.3 Graph Construction

The background thread drains the TraceRing and constructs a TraceGraph: a bidirectional CSR (Compressed Sparse Row) property graph where nodes represent operations and edges represent relationships.

**Data flow edges** are constructed by pointer tracking. An open-addressing hash table (PtrMap, stack-allocated, 8192 slots) maps tensor data pointers to the operation index that produced them. When an operation's input pointer matches a previously recorded output pointer, a DATA_FLOW edge is created from producer to consumer. This captures the actual dataflow of the computation without symbolic analysis.

**Alias edges** are constructed when multiple operations reference the same underlying storage (views, in-place operations, transposes). Same data pointer from different operations implies shared storage, recorded as an ALIAS edge.

Each graph node carries its hash values (schema, shape, scope, callsite), tensor metadata arrays, scalar arguments, and flags (gradient computation, inference mode). The graph is constructed in a single pass over the drained ring entries, with O(V + E) complexity via counting sort for CSR construction.

**Iteration detection.** The IterationDetector identifies repeating patterns in the operation stream using a signature of K = 5 consecutive schema hashes. When the same signature appears twice with consistent spacing, an iteration boundary is confirmed. This allows Crucible to identify the natural loop structure of training without programmer annotation.

## 3.4 The Merkle DAG

From the TraceGraph, the background thread constructs the Merkle DAG --- the representation of the Vigil's computation. The DAG contains three node types.

**RegionNode.** A compilable sequence of operations. Its content hash is computed from the schema hashes, input shapes, strides, data types, devices, and scalar values of all operations in the region. The content hash captures the semantics: two RegionNodes with identical content hashes perform identical computation. The Merkle hash extends the content hash with the hashes of child nodes, enabling O(1) structural equality testing for entire sub-trees.

**BranchNode.** Represents dynamic behavior --- points where the Vigil's computation may diverge (data-dependent control flow, dynamic shapes, architecture changes during training). A BranchNode contains a guard (the operation sequence that determines which branch to take) and multiple arms, each an independently compilable sub-DAG. Both arms are compiled; the guard selects which executes. BranchNodes are the mechanism for all forms of runtime adaptation: architecture mutation, attention head replacement, hyperparameter changes, and continuous learning with A/B verification.

**LoopNode.** Represents cyclic computation within the acyclic DAG framework. A LoopNode contains a body (the computation to repeat), feedback edges (connecting outputs to inputs across iterations), and a termination condition (repeat N times, or until a convergence criterion is met). The Merkle hash incorporates the body's content hash, a "loop" tag, the feedback signature, and the termination condition. LoopNodes enable compiled recurrence (RNNs without Python per timestep), convergence-based execution (fixed-point iterations), and cross-iteration optimization (pipelining iteration N+1's forward pass with iteration N's backward pass).

**Atomic activation.** New DAG structures (compiled regions, branch alternatives, updated memory plans) are prepared by the background thread and activated via a single atomic pointer swap at iteration boundaries. The foreground thread always executes a consistent, fully-compiled plan. This is the same mechanism used for all zero-downtime updates: compilation activation, branch swaps, memory plan updates, topology changes, and rollbacks.

## 3.5 Content Addressing and the Kernel Cache

The KernelCache maps (content_hash, device_capability) to compiled kernel artifacts. It is a lock-free open-addressing hash table: the background thread inserts entries via atomic compare-and-swap; the foreground thread reads entries without synchronization beyond acquire loads.

Content addressing provides several properties:

- **Cross-iteration reuse.** Iteration N+1 typically has the same content hashes as iteration N. All compiled kernels are cache hits.
- **Cross-run reuse.** The KernelCache persists as part of the Cipher. Restarting training on the same Vigil and hardware incurs zero compilation.
- **Cross-Vigil reuse.** Different Vigils that share sub-computations (e.g., the same attention mechanism with the same head dimension) produce the same content hashes for those sub-computations. Compiled kernels transfer automatically.
- **Cross-device coexistence.** The same content hash maps to different compiled kernels on different hardware. Multiple device-specific variants coexist in the cache, keyed by device capability.

The KernelCache grows monotonically and is write-once per (content_hash, device_capability) pair. It is a component of the Cipher and persists across Relay migrations --- when a Vigil migrates to new hardware, existing cache entries for previous devices remain valid for future use, and new entries are compiled for the current Relay.
