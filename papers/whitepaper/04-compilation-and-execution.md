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
