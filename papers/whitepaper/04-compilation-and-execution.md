# 4. Compilation and Execution

This section describes how Crucible compiles the Merkle DAG into executable plans. Memory planning and Tier 1 replay are implemented; SMT kernel synthesis and shadow handle execution are described as designs.

## 4.1 Static Memory Planning

In eager execution, every operation allocates output memory through a runtime allocator (PyTorch's CUDACachingAllocator: freelist search, block splitting, coalescing, mutex acquisition). Crucible replaces this with a static plan computed from the dataflow graph.

From the Merkle DAG, the background thread extracts tensor lifetimes. Each tensor's birth is the index of its producing operation; its death is the index of the last operation that consumes it. Size is computed from shape and dtype, aligned to 512 bytes. Given these intervals, a sweep-line algorithm assigns offsets within a single contiguous memory pool:

1. Sort tensors by birth index.
2. Maintain a list of free intervals within the pool, initially one interval covering the full pool.
3. For each tensor in birth order: find the first free interval that fits, assign the offset, mark the interval as occupied until the tensor's death.
4. At each death point, return the interval to the free list.

The output is a MemoryPlan: a total pool size and, for each tensor, a fixed (operation_index, port, offset, size) tuple. At runtime, a single allocation obtains the pool. Every subsequent "allocation" is a pointer addition: `base_ptr + offset`. No mutex, no freelist search, no fragmentation, no contention.

**Correctness.** The sweep-line algorithm's non-overlap property --- no two simultaneously live tensors share memory --- is proved in Lean 4 for the general case and verified by Z3 for configurations up to 64 tensors with arbitrary lifetimes.

**Dynamic shapes.** When shapes change between iterations, the background thread computes a new MemoryPlan and activates it via atomic swap at the next iteration boundary. The foreground thread is never blocked.

**Activation checkpointing.** The memory plan provides exact per-tensor cost information. Crucible can automatically determine, for each forward activation needed in the backward pass, whether storing it (consuming memory) or recomputing it (consuming compute) is more efficient. This decision is per-tensor and adapts to available memory.

## 4.2 SMT-Based Kernel Synthesis

*Design.* Current autotuning approaches (Triton, AutoTVM/Ansor) search a configuration space by benchmarking candidate kernels. This is effective but time-consuming (hundreds to thousands of trials) and discovers local optima within the search strategy.

Crucible proposes formulating kernel configuration selection as a constraint optimization problem. For a given operation (e.g., GEMM), shape (M, N, K), and target device:

- **Axioms.** Hardware parameters are constants: SM count, shared memory per SM, registers per SM, warp size, memory bandwidth, compute throughput. These come from Longitude calibration measurements of the actual device, not from spec sheets.
- **Variables.** Kernel configuration parameters: tile dimensions (tile_M, tile_N, tile_K), pipeline stages, warp count, vectorization width.
- **Constraints.** Shared memory usage ≤ device limit. Register count ≤ 255. Tile dimensions are multiples of warp size. Total threads per block ≤ 1024. Occupancy ≥ minimum threshold.
- **Objective.** Minimize estimated execution time under a roofline cost model: max(compute_cycles / peak_compute, memory_bytes / peak_bandwidth).

A Z3 `optimize` query finds the configuration that minimizes the objective subject to all constraints. The solution is optimal within the analytical model.

**Qualification.** Optimality is with respect to the roofline cost model, which does not capture all hardware effects (L1/L2 cache behavior, instruction scheduling, warp divergence patterns, TLB pressure). Longitude calibration (Section 6) computes per-kernel-class correction factors that absorb these effects, bringing model predictions to within a few percent of measured performance. The formal guarantees --- no out-of-bounds access, no bank conflicts, coalesced memory access --- are model-independent and hold on actual hardware.

**Scope.** This approach applies to any kernel with a bounded, discrete configuration space: GEMM (tile/pipeline/warp parameters), convolution (tile/implicit_gemm), attention (tile/pipeline/TMA), normalization (vector_width/rows_per_block), reduction (block_dim/reduce_strategy), and collective communication (algorithm/channels/chunk_size).

## 4.3 Graduated Divergence Detection

When the Vigil's computation changes between recording and compiled execution, Crucible must detect the divergence and respond. Rather than a binary match/mismatch, Crucible uses four severity levels based on which hash component diverges:

1. **schema_hash mismatch** --- different operation. Hard diverge: immediately fall back to eager execution via the Vessel and re-record.
2. **shape_hash mismatch** --- same operation, different shapes. Hard diverge: compiled kernels and memory plan are invalid for the new shapes. Fall back, re-record, recompile.
3. **scope_hash mismatch** --- same operation, same shapes, different module context. Soft warning: the computation is identical; the call site moved (e.g., code refactoring). Compiled plan remains valid.
4. **callsite_hash mismatch** --- same operation, same shapes, same module, different source location. Softest warning: logged but no action.

**Pre-emptive preparation.** Before confirming that compilation is broken, Crucible prepares the eager fallback path. This avoids a latency spike on divergence: the transition from compiled to eager execution is seamless.

**Recovery.** After divergence, the system re-enters recording mode for the new behavior, builds a new graph and DAG, and compiles. If the divergence was temporary (e.g., a single unusual batch), the system may encounter the original behavior again and switch back to the previously compiled plan via BranchNode arm selection.

## 4.4 Shadow Handle Execution Model

*Design.* In the fully compiled execution mode (Tier 2), the Vessel interceptor does not execute operations or allocate memory. Instead, it returns *shadow handles*: tensor objects with correct metadata (shape, strides, dtype, device) whose storage points into the pre-planned memory pool. The GPU executes compiled kernels asynchronously on CUDA streams; Python holds shadow handles and inspects only metadata.

Shadow handles are full tensor objects (ConductorTensorImpl) --- they satisfy all metadata queries that Python or the framework might make. The only operations that force synchronization are those that inspect tensor *values*: `.item()`, `.cpu()`, `.numpy()`, `print()`, or data-dependent control flow. Everything else returns immediately.

For a model executing 1,000 operations with one `.item()` call for loss reporting: 999 operations return shadow handles; one synchronizes. Python's wall time is dominated by the single sync point.

## 4.5 Execution Tiers

Crucible implements compilation in three tiers of increasing optimization:

**Tier 1 (implemented).** Eager replay from the recorded trace with divergence detection. The compiled plan replays operations in recorded order, using the Vessel's normal dispatch for execution but the static memory plan for allocation. Guards check hash consistency at each operation. This tier validates the recording and divergence detection infrastructure without requiring custom kernel compilation.

**Tier 2 (designed).** Shadow handle execution. The foreground thread returns pre-allocated handles; the background thread has pre-scheduled kernel launches on CUDA streams. Python and GPU are fully decoupled. Target: approximately 2ns per Vessel dispatch (one pointer advance + one hash guard check).

**Tier 3 (designed).** CUDA Graph capture and replay. The entire compiled iteration is captured as a CUDA Graph and replayed in a single API call per iteration. Target: approximately 50ns per iteration, independent of operation count. Applicable when shapes are static across iterations.

Each tier is a strict superset of the previous tier's correctness infrastructure. Divergence detection and fallback work identically across all tiers.
