# 9. Implementation Status

## 9.1 What Is Built

31 C++26 headers in `include/crucible/`, 24 tests.

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

39 modules, 1,331 theorems, zero `sorry` in core infrastructure. Covers Arena (pairwise disjointness, alignment), MemoryPlan (sweep-line non-overlap), PoolAllocator, SPSC ring (FIFO ordering, batch drain), MetaLog, IterationDetector (detection latency bounds), TraceGraph (CSR consistency), Merkle DAG (collision probability, structural diff), Graph IR (DCE fixpoint, topological sort validity), scheduling (Graham's bound, Brent's theorem), roofline model (multi-level cache, wave quantization, correction factors), and fusion (chain optimality, occupancy). Built with Lean 4.28.0 + local Mathlib.

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
