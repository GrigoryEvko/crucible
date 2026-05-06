# Crucible — Runtime, Fleet, Frontend

*The runtime half of Crucible: mock-tensor dispatch capture, frontend-agnostic Vessel layer, Canopy fleet mesh, CNTP native transport, Cipher persistence, replay-deterministic training. Everything that isn't Forge (vendor-agnostic optimizer, see FORGE.md) or Mimic (per-vendor backend framework, see MIMIC.md).*

Crucible is three things. **Forge** compiles tensor graphs. **Mimic** realizes compiled graphs on silicon. **The runtime** — this document — orchestrates compilation, drives distributed execution, manages persistence, guarantees replay determinism, and presents a clean frontend surface to user code. The 17-layer overview in CLAUDE.md is the mental model; this document is the design reference for the concrete systems.

Written in the voice of CLAUDE.md: direct, opinionated, dense. Read alongside FORGE.md and MIMIC.md; cross-references are explicit.

---

## Contents

1. Thesis and scope
2. Layering — how Crucible, Forge, Mimic fit together
3. Hollow Vessel — mock-tensor dispatch capture, research kernel primitives
4. Frontend-agnostic design — PyTorch, JAX, native Python, native C++, native Rust
5. CNTP — Crucible Native Transport Protocol
6. Zero-userspace-tax comms in steady state
7. Canopy — the fleet mesh
8. k8s-Canopy operator + SLURM launch
9. Cipher — content-addressed persistence
10. Replay determinism — the load-bearing invariant
11. Data loading, inference sessions, dynamic shapes
12. Distribution at runtime — 5D parallelism, live reshard, pipeline scheduling, collective failure
13. Augur — continuous monitoring and drift
14. Keeper daemon lifecycle
15. Hardware heterogeneity handling
16. Realtime performance — bounded latency under production load
17. Observability and debugging
18. Security, multi-tenancy, and network topology requirements
19. What ships in Crucible runtime vs Forge vs Mimic
20. Build plan — runtime milestones
21. Open questions deferred
22. Glossary

---

## 1. Thesis and scope

Six sentences:

1. **The runtime presents one API to the user** (`cr.Trainer(model, recipe).fit(epochs)` or equivalent) and mediates everything below it — frontend capture, compilation, distribution, execution, persistence. User code never invokes Forge or Mimic directly.
2. **Frontend-agnostic by construction.** PyTorch is one of several frontends, not the canonical one. JAX, Jittor, native Python, native C++, native Rust are peers. Each frontend is a <2K-LoC adapter that produces IR001; everything below is shared.
3. **The dispatch layer captures mock tensors, not real execution.** PyTorch's backend never runs; Crucible returns `CrucibleTensorImpl` shadows immediately on every op. The user's Python "runs" synchronously producing graph structure. Execution happens when Crucible decides, via compiled kernels.
4. **The fleet has no master.** Canopy mesh with gossip (SWIM) for membership, Raft-scoped consensus for critical decisions. Dynamic join/leave. Any Keeper can orchestrate a compile; no designated driver node. k8s operator exists (`CrucibleCluster` CRD) but is a thin launch substrate, not a coordinator.
5. **All networking is zero-syscall in steady state.** RDMA verbs for bulk data, eBPF + XDP + AF_XDP for control plane, shared-memory/NVSHMEM intra-node. CNTP composes these; CPU cost of Crucible's own networking is ~100ns per RPC in the hot path.
6. **Replay determinism is the invariant that justifies every other design decision.** Philox counter-RNG + content-addressed memory plan + bit-exact kernels + canonical reduction topology means training state at step T is fully determined by `(weights_T, optimizer_T, cursor_T, seed)`. Checkpoint that tuple; recover to any step by replay. No hidden state anywhere.

Scope boundaries:

| Category | In scope | Out of scope |
|---|---|---|
| Frontends | PyTorch, JAX, Jittor, native Python/C++/Rust | Tensorflow (dead), MXNet (dead), custom DSLs not in the CKernel taxonomy |
| Distribution | Multi-node training, multi-node inference, elastic membership, mixed-vendor fleet | Federated learning across trust boundaries (that's L16, separate), cross-datacenter latency-critical workloads (best-effort only) |
| Persistence | Cipher three-tier (hot RAM / warm NVMe / cold S3-compatible), content-addressed | Database semantics (SQL, ACID transactions beyond what Raft gives us) |
| Networking | CNTP (RDMA, NVLink, io_uring TCP, eBPF control plane) | Vendor-proprietary proprietary interconnect APIs (NCCL, libnccom, hcoll, UCX) |
| Launch | k8s via CrucibleCluster CRD, SLURM via srun, bare-metal via systemd | AWS ParallelCluster, GCP hypercomputer, Azure integrations — all are thin adapters over the bare-metal launch |
| Scheduling | Fleet-internal (Canopy picks partitions) | Cluster-external (k8s Scheduler / SLURM scheduler) — we fit into whatever they give us |

Everything out of scope is either (a) solved by a neighbor (Forge/Mimic handle compilation; k8s/SLURM handle node allocation), or (b) an explicit non-goal.

---

## 2. Layering — how Crucible, Forge, Mimic fit together

```
┌─────────────────────────────────────────────────────────────┐
│  User code (framework-native or Crucible-native)             │
│  ── PyTorch / JAX / Jittor / raw Python / raw C++ / Rust ── │
└───────────────────────┬──────────────────────────────────────┘
                        │ dispatches ops producing mock tensors
                        ▼
┌─────────────────────────────────────────────────────────────┐
│  Vessel adapter (~2K LoC per frontend)                       │
│  - intercepts framework dispatch                             │
│  - translates to CKernelId + TensorMeta                      │
│  - returns CrucibleTensorImpl mock tensors                   │
└───────────────────────┬──────────────────────────────────────┘
                        │ IR001 op stream via TraceRing SPSC
                        ▼
┌─────────────────────────────────────────────────────────────┐
│  Crucible runtime core                                       │
│  - L4 Dispatch  (TraceRing, MetaLog, mock-tensor handles)   │
│  - L6 TraceGraph                                             │
│  - L7 Merkle DAG + RegionNodes + KernelCache                │
│  - L3 PoolAllocator                                          │
│  - L14 Cipher (hot/warm/cold tiers)                          │
│  - Keeper daemon + Canopy mesh                               │
└───────────────────────┬──────────────────────────────────────┘
       ▲                │ fused regions → compile request
       │ compiled plan  ▼
┌─────────────────────────────────────────────────────────────┐
│  Forge (FORGE.md) — vendor-agnostic compiler                 │
│  - IR001 → IR002 lowering                                    │
│  - Pattern match to kernel catalog                           │
│  - Pin NumericalRecipe                                       │
│  - Phase A through L                                         │
└───────────────────────┬──────────────────────────────────────┘
                        │ per-kernel compile calls
                        ▼
┌─────────────────────────────────────────────────────────────┐
│  Mimic (MIMIC.md) — per-vendor backend framework            │
│  - IR002 → IR003* lowering                                   │
│  - Native ISA emission                                       │
│  - Three-tier simulator + MAP-Elites                         │
│  - Per-vendor runtime library (our libcuda / libnrt / …)    │
│  - Per-vendor collective library (our NCCL / libnccom / …)  │
└───────────────────────┬──────────────────────────────────────┘
                        │ compiled vendor binaries + handles
                        ▼
┌─────────────────────────────────────────────────────────────┐
│  Accelerator silicon (via kernel driver ioctls only)         │
└─────────────────────────────────────────────────────────────┘
                        ▲
┌───────────────────────┼──────────────────────────────────────┐
│  CNTP (this doc, §5-§6) — Crucible Native Transport Protocol │
│  - Transport (RDMA / NVLink / io_uring TCP)                  │
│  - Gossip (SWIM)                                             │
│  - Consensus (Raft scoped)                                   │
│  - Collectives (at protocol level; vendor impl in Mimic)     │
│  - NetworkOffload plane (SHARP / ICI / XGMI / SwarmX)        │
│  - eBPF + XDP + AF_XDP for control plane                     │
└─────────────────────────────────────────────────────────────┘
```

Who owns what:

- **Frontends**: user-visible ops surface. Thin.
- **Vessel**: frontend → IR001 translation. Thin.
- **Crucible runtime core**: IR001 → trace → Merkle DAG → compile request. Owns Cipher, Canopy, KernelCache.
- **Forge**: compiler, vendor-agnostic. Owns IR002.
- **Mimic**: per-vendor everything below IR002. Owns IR003*, simulator, binary emitter, runtime library, collective library, calibration.
- **CNTP**: the wire protocol. Owns the bytes on the network.

User code sees only the frontend surface. Crucible runtime is where the magic happens — but it's all indirection, not algorithmic; the actual algorithms live in Forge and Mimic.

---

## 3. Hollow Vessel — mock-tensor dispatch capture

The user's Python code runs, but no actual computation happens during capture. Every tensor op is intercepted; Crucible returns a mock tensor (`CrucibleTensorImpl`) immediately; the user's loop body produces only graph structure.

### 3.1 Why this matters

The legacy "RECORD mode runs PyTorch's backend for the first iteration" path exists in CLAUDE.md's L4 description. **That's the old design.** The new design: PyTorch's backend never runs. Zero. Iteration 1 uses Crucible-compiled kernels just like iteration 1000. Consequences:

- **No vendor-library warmup.** Iteration 1 cannot touch cuBLAS, cuDNN, rocBLAS, MIOpen, anything. Aligns with the no-vendor-libs invariant.
- **Numerics consistent from step 0.** The recipe pinning applies to every step including the first, not just "after compilation kicks in."
- **Bit-exact CI is verifiable from step 0.** Replay-determinism tests don't need a warmup-ignoring clause.
- **Installation is smaller.** We don't need the vendor library backends even as a fallback; we have our own reference eager path (CPU or reference-tier kernels from the Genesis pack).

The cost: iteration 1 pays compile latency instead of a fast vendor-library warmup. For common shapes this is ~0ms (Genesis pack hit); for novel shapes it's 100-500ms (background compile while reference-eager runs that one iteration). Amortizes across thousands of iterations.

### 3.2 Mock tensor semantics

A `CrucibleTensorImpl` is a full tensor handle from the frontend's perspective:

```cpp
class CrucibleTensorImpl : public TensorImpl {
    // Metadata visible to the frontend
    ShapeHash           shape_hash;
    ScalarType          dtype;
    DeviceType          device_type;
    int8_t              device_idx;
    Layout              layout;
    int64_t             storage_offset;      // for views
    const int64_t*      sizes;                // arena
    const int64_t*      strides;              // arena
    bool                requires_grad;
    // ... all the fields a real TensorImpl has

    // Crucible-internal
    NodeId              producer_node;        // which IR001 op produced this
    SlotId              slot_id;              // where its data WILL live
    const Expr**        symbolic_shape;       // if any dim is symbolic
    mutable void*       materialized_storage; // nullptr until sync point forces realization
    RegionId            enclosing_region;     // for guard checks
};
```

From PyTorch's view: indistinguishable from a real tensor. Has `.shape`, `.dtype`, `.device`, `.requires_grad`, `.grad`, `.storage()` (raises NotImplemented before materialization), `.numel()`, `.ndim()`, all of it. Autograd sees a leaf or non-leaf, registers itself in the autograd tape, records grad_fn.

From Crucible's view: a graph node. The op that produced it is in TraceRing. The data doesn't exist until someone forces it.

### 3.3 Capture path

Per op, the frontend dispatches to `DispatchKey::Crucible`:

```
1. advance op_idx                                     ~1ns
2. build CrucibleTensorImpl (arena-alloc, fill metadata) ~2ns
3. record op + input CrucibleTensorImpl ids in TraceRing ~2ns
4. return to framework
```

~5-10ns total. No device memory allocated. No kernel launched. No vendor library touched.

The frontend sees a tensor. Continues execution. Builds up its own graph representation (PyTorch's autograd tape, JAX's traced jaxpr, whatever). On reaching a sync point, that framework-side representation is consistent — every `grad_fn` points at a real chain — but none of it has been executed.

### 3.4 Sync points

Execution is forced by:

| Trigger | How |
|---|---|
| `tensor.item()` | Compile-up-to-here, execute, return Python scalar |
| `tensor.numpy()`, `.cpu()`, `.tolist()` | Same; copy materialized storage to host |
| `print(tensor)`, `repr(tensor)` | Same (tensor printing reads values) |
| `bool(tensor)`, `if t > 0:` | Same; raises `CrucibleCaptureError` if tensor is not scalar |
| `trainer.step(x, y)` return | Explicit sync point; compile full step, execute, return loss scalar |
| `cr.materialize(t)` | Explicit materialize of one tensor |
| `cr.collect(list_of_tensors)` | Batch materialize |
| Python garbage collection of a mock tensor with consumers still pending | Defers; mock tensor holds a shared ref to its region until consumers resolve |

Data-dependent control flow (`if x.sum() > 0: branch_a() else: branch_b()`) is illegal in pure-capture mode. The user gets a `CrucibleCaptureError` pointing at the line. Options:

- Rewrite as `cr.cond(x.sum() > 0, branch_a, branch_b)` — explicit branch captured as a BranchNode in the Merkle DAG.
- Rewrite as `cr.where(x.sum() > 0, branch_a_result, branch_b_result)` — both branches computed, ternary selection (wasteful but always legal).
- Wrap the data-dependent region in `with cr.eager_mode():` — that sub-region runs eagerly (slow but flexible), the outer region stays captured.

For beginners: the eager fallback is the escape valve. For production training: write the loop the way the capture wants.

### 3.5 Compile-on-demand flow

When a sync point hits:

```
1. Flush accumulated TraceRing → TraceGraph → RegionNodes
2. For each RegionNode touching the sync point's data dependency:
   3. content_hash(region) + target_chip_caps_hash → cache key
   4. L3 cache lookup (compiled bytes)
      HIT   → skip to 7
      MISS  → L2 lookup (IR003* snapshot per-vendor-family)
          HIT → skip to 6
          MISS → L1 lookup (IR002 snapshot, cross-vendor)
              HIT → skip to 5
              MISS → Forge runs Phase A-E, produces IR002, populates L1
   5. Forge Phases F-G on IR002, calls Mimic::compile_kernel per kernel
      Mimic produces IR003*, populates L2
   6. Mimic emits binary bytes, populates L3
   7. Mimic's runtime::load_kernel reads bytes, DMAs to device memory
8. ExecutionPlan assembled (Phase I-J)
9. Runtime executes: launch graph, or individually submit kernels
10. Materialize sync-point tensors (DMA device → host if needed)
11. Return Python values to user code
```

Cache hit path: step 3, 4, 7, 8, 9, 10 only. ~1-10μs for an already-compiled step.

Cold compile path: everything. 10-500ms depending on novelty. Mitigated by Genesis Kernel Pack pre-seeding for common shapes.

### 3.6 Autograd without a vendor tape

PyTorch's autograd tape is built during forward execution — each op records its `grad_fn` with closure over input tensors. We need the same graph structure but without PyTorch executing.

Two implementation paths:

**Path A — leverage PyTorch's autograd infrastructure unchanged.** PyTorch's autograd doesn't need real tensor storage; it only needs metadata (shape, stride, dtype, requires_grad) to construct `grad_fn`s. Our mock `CrucibleTensorImpl` satisfies all those queries. Autograd tape builds correctly. When `.backward()` is called, Crucible intercepts — which is just another `DispatchKey::Crucible` dispatch. Autograd walks the tape, dispatches backward ops through `DispatchKey::Crucible`, Crucible captures those into the same TraceRing as forward ops. Result: one IR001 graph containing forward + backward, ready for Forge.

**Path B — Crucible-native reverse-mode differentiation.** Synthesize backward directly from forward IR001 by walking the Merkle DAG in reverse, applying per-op adjoint rules. Zero framework dependency. Works for native Python / C++ / Rust frontends. Matches JAX's approach.

Both paths coexist. PyTorch-Vessel uses Path A (zero effort, leverages existing PyTorch autograd). Native frontends use Path B (clean, first-party).

### 3.7 Data loaders as first-class graph ops

DataLoaders are a problem in the pure-capture model. They're Python-side iterators producing new host tensors each iteration; capturing them would mean materializing one batch per capture, defeating the "no execution" guarantee.

Resolution: **DataLoader becomes a `CKernelId::IO_PREFETCH` node** in IR001. The node is a Python callback (captured by `cr.dataloader(fn, shards, batch_size, shuffle_seed)`) that:

- Has output shape/dtype declared at compile time
- Produces a host-memory batch when executed
- DMAs that batch into a pre-planned device slot (part of Phase G MEMPLAN)
- Advances a `shard_cursor` state that's part of the Cipher checkpoint

The callback runs on a dedicated CPU-class Keeper pod (see §11) or a CPU thread of the compute-class Relay. Backpressure: the DataNode reads ahead by a bounded queue depth; if consumers are slow, it blocks; if producers are slow, compute waits on the node. All captured in the ExecutionPlan as a regular compute dependency.

Result: user writes

```python
for step, (x, y) in enumerate(s.dataloader("s3://pile/*.zst", batch_size=32, seed=42)):
    loss = trainer.step(x, y)
```

The `s.dataloader(...)` returns an infinite iterator that yields `(mock_tensor_batch_x, mock_tensor_batch_y)` pairs. Each iteration produces new CrucibleTensors; the captured graph has a DataNode providing them. Execution time: DataLoader runs in parallel with compute, gated by the captured ordering.

### 3.8 Research kernel primitives — decorators and escape valves

The substrate captures arbitrary control flow via the Merkle DAG's `LoopNode` and `BranchNode`, but research code needs a concise front-end grammar. Crucible ships a small set of Python decorators and primitives whose semantics map directly to IR002 kernel templates plus user-supplied `ComputeBody` fragments. The compiler specification lives in FORGE.md §18.7; this section defines the user-facing surface.

**Two orthogonal concerns.** Every templated kernel is a pair of decisions: *structural* (tile sizes, pipeline depth, warp specialization, memory layout, reduction topology) and *content* (what arithmetic runs inside the tile loop). Mimic's per-vendor backends own structural optimization; Crucible lets the user supply content as IR001 `ComputeBody` fragments. At IR002→IR003* lowering, bodies inline directly into the tile loop, fuse with peephole rewrites, and participate in register allocation alongside structural code. User gets FlashAttention-class perf on arbitrary variants; the user never writes SASS.

**Templated-kernel decorators.** Each targets one `KernelKind`; the decorator inspects the Python function, captures its body as `ComputeBody` fragments, binds them to the appropriate extension-point attribute fields:

```python
# Attention with custom score modification and causal mask
@cr.kernel.attention
def alibi_attention(q, k, v):
    def score_mod(score, b, h, q_idx, kv_idx):
        return score - 0.1 * cr.abs(q_idx - kv_idx)       # ALiBi positional bias

    def mask_mod(b, h, q_idx, kv_idx):
        return cr.abs(q_idx - kv_idx) < 512                # sliding window

    return cr.sdpa(q, k, v, score_mod=score_mod, mask_mod=mask_mod)

# Normalization with custom statistic and transform
@cr.kernel.norm
def power_norm(x, p=4):
    def stat_compute(x_tile):
        return cr.mean(cr.abs(x_tile) ** p) ** (1.0 / p)

    def normalize(x, stat):
        return x / (stat + 1e-6)

    return cr.norm(x, stat_fn=stat_compute, normalize_fn=normalize)

# Optimizer with custom state shape and update rule
@cr.kernel.optimizer(state_shape=lambda p: {"momentum": p.shape})
def lion(params, grads, state, lr, beta1, beta2):
    def update_body(p, g, s, lr, b1, b2):
        m_new = b2 * s.momentum + (1 - b2) * g
        update = cr.sign(b1 * s.momentum + (1 - b1) * g)
        return p - lr * update, {"momentum": m_new}
    return update_body

# Parallel prefix scan with custom associative operation
@cr.kernel.scan
def cumulative_max(x, axis=-1):
    def assoc_op(a, b):
        return cr.maximum(a, b)
    return cr.scan(x, op=assoc_op, axis=axis, identity=-cr.inf)
```

The decorator set covers the twelve IR002 kernel families that admit extension points: `cr.kernel.attention`, `cr.kernel.norm`, `cr.kernel.reduce`, `cr.kernel.scan`, `cr.kernel.pointwise`, `cr.kernel.embedding`, `cr.kernel.gemm` (prologue/epilogue only), `cr.kernel.conv`, `cr.kernel.ssm`, `cr.kernel.rng`, `cr.kernel.moe_route`, `cr.kernel.optimizer`. Each enforces its template's tile-shape constraints; bodies that violate them (e.g., a `score_mod` that introduces a cross-tile dependency) fail at capture with a diagnostic.

**Control-flow primitives.** Value-dependent control flow captures cleanly via four primitives that map to `LoopNode` and `BranchNode`:

```python
# Fixed-length loop over the same body (looped / universal transformer)
final_state, _ = cr.scan(step_fn, init=h0, length=N)        # compiles as LoopNode with REPEAT(N)

# Bounded while-loop with value-dependent termination
final = cr.while_loop(cond_fn, body_fn, init=state, max_iters=32)  # LoopNode with UNTIL(predicate)

# Conditional branching on tensor value
result = cr.cond(pred, true_fn, false_fn, operand=x)        # BranchNode with two compiled arms

# Sparse routing for MoE / mixture-of-depths
routes = cr.top_k(gate_logits, k=2)
dispatched = cr.scatter_sparse(tokens, routes)              # MOE_ROUTE kernel
per_expert = [experts[i](dispatched[i]) for i in range(n_experts)]
result = cr.gather_sparse(per_expert, routes)
```

Patterns Crucible compiles cleanly without graph breaks: Universal Transformers, DEQ fixed-points, diffusion denoising loops, Mixture of Depths per-token early exit, speculative decoding with accept/reject, chain-of-thought with tree expansion, neural MCTS, iterative reasoning. Each is a composition of the primitives above plus templated kernels. The research engineer writes Python; the compiler emits persistent-CTA kernels with internal loop bodies, bucketed shape specializations, and content-addressed caches.

**Three escape valves for patterns that don't fit a templated kernel.**

1. **`cr.kernel.compound`** — for user-defined fused op chains that cross kernel-family boundaries. Crucible captures the sequence as typed `KernelNode`s; Forge's Phase D fuse pass folds compatible boundaries; result is a single compound kernel. Typical ceiling: 60-70% MFU on well-chosen chains.

   ```python
   @cr.kernel.compound
   def gated_ffn_block(x, w1, w2, gate_w):
       y = cr.gemm(x, w1)
       g = cr.sigmoid(cr.gemm(x, gate_w))
       y = y * g
       y = cr.gemm(y, w2)
       return cr.layernorm(y + x)
   ```

2. **`cr.kernel.custom`** with structural hints — for novel algorithms (selective scan, block-sparse attention, ring reductions) that need Mimic tile-shape and memory-layout guidance but not full code. Hints steer MAP-Elites; result is a compiled kernel at 40-60% MFU.

   ```python
   @cr.kernel.custom(
       kind="recurrence",
       hints={
           "sequential_over": "time_dim",
           "parallel_over": ["batch", "channel"],
           "state_shape": (batch, channel, state_dim),
           "prefer_persistent_kernel": True,
       },
   )
   def selective_scan(u, delta, A, B, C):
       ...
   ```

3. **`cr.raw_kernel`** — full escape to hand-written ISA. User supplies PTX/SASS/MFMA/MXU/NeuronCore bytes; Crucible validates signature, inserts into the ExecutionPlan as an opaque leaf. No Crucible optimization, no replay-determinism guarantee beyond what the user enforces. Reserved for benchmark validation and ISA-level research.

   ```python
   @cr.raw_kernel(chip="nv_sm_100a", ext=".sass")
   def hand_tuned_gemm():
       return """WGMMA.MMA_ASYNC.SYNC.ALIGNED.M64N128K16 D, ...
                ..."""
   ```

**Cache behavior.** Every body fragment is content-addressed via its IR001 `ComputeBody` hash, interned in the ExprPool. Two users who write identical `score_mod` implementations (e.g., ALiBi with slope 0.1) produce byte-identical body hashes, the same `KernelContentHash`, and share the same L3 cache entry. Federation via L1 (IR002 snapshot) works for research variants because the extended hash remains vendor-neutral. Tweaking a constant (ALiBi slope 0.1 → 0.08) produces a new body hash; first iteration re-compiles (~100-300ms with MAP-Elites warm-started from the nearest body-family archive; see MIMIC.md §19); subsequent iterations hit cache.

**Autograd for research primitives.** Forward bodies auto-derive a reverse-mode backward via the IR001 adjoint rules per micro-op. User can override with `backward_body=` for numerically-delicate cases (straight-through estimators, quantization-aware training, custom gradient tricks). Gradient checkpointing policy is per-body via `checkpoint=` (`NEVER`/`STORE`/`RECOMPUTE`/`AUTO`); the default `AUTO` consults Forge Phase G's cost model.

### 3.9 Dynamic on-GPU scheduler — escape valve for irregular control flow

Most research patterns compile cleanly into `LoopNode` + `BranchNode` + extension-point bodies (§3.8). The ~5% that don't — fully data-dependent recursion, unbounded iteration with runtime-determined task count, producer-consumer graphs with unpredictable arrival order — benefit from an **on-GPU dynamic scheduler**, a pattern validated by the Event Tensor paper (Jin et al., 2026) for MoE and irregular workloads.

#### 3.9.1 When static compilation is insufficient

Patterns we compile statically via LoopNode/BranchNode:

- Fixed-N loops over identical body (Universal Transformer, DEQ)
- Bounded while-loops with convergence predicate (Ponder, diffusion)
- Per-step early exit (Mixture of Depths)
- Speculative decoding with accept/reject branching
- Iterative reasoning with max-depth cap

Patterns that don't compile cleanly:

- **Task counts determined by runtime data** — per-expert MoE tile count depends on actual `topk` routing
- **Unbounded work queue** — arbitrary recursion, tree search with uncapped expansion
- **Producer-consumer streams with unpredictable arrival** — speculative tree where accept at node K unblocks subtree expansion

For these, we emit a **tiny scheduler CTA** embedded in the Plan that runs a push/pop queue on global memory, atomically distributing ready tasks to worker CTAs.

#### 3.9.2 The scheduler kernel

```cpp
__global__ void on_gpu_scheduler(GlobalTaskQueue* q, EventTensor* events) {
    while (!q->done) {
        Task* t = q->try_pop();
        if (!t) { __nanosleep(100); continue; }
        switch (t->kind) {
          case EXPERT_TILE:  dispatch_expert_tile(t); break;
          case TREE_EXPAND:  dispatch_tree_node(t);   break;
          case SPEC_VERIFY:  dispatch_verify(t);      break;
          // ...
        }
        // Completion triggers dependent tasks
        events[t->event_id].notify();
        for (Task* dep : events[t->event_id].waiters()) {
            if (dep->all_deps_ready()) q->push(dep);
        }
    }
}
```

One CTA of ~128 threads; atomic push/pop on a shared global-memory queue; notify/wait on the Plan's EVENT_TENSOR PatchPoint (FORGE.md §18.8). Runs on a dedicated green context (§14.9).

#### 3.9.3 User-facing API

```python
@cr.kernel.custom(scheduler="dynamic", event_tensor_shape=[n_experts, max_tokens_per_expert])
def moe_layer_with_unpredictable_routing(tokens, weights, routing_fn):
    # body expresses dynamic task graph
    ...
```

When `scheduler="dynamic"`, Forge Phase J emits the scheduler CTA as a persistent kernel running alongside worker CTAs; both are in the same ExecutionPlan's pushbuffer. The EVENT_TENSOR PatchPoint is populated by the routing kernel at runtime.

#### 3.9.4 Cost vs static

| Dimension | Static (LoopNode/BranchNode) | Dynamic scheduler |
|---|---|---|
| Per-task overhead | ~200 ns (counter decrement + jump) | ~500 ns - 1 μs (atomic ops + notify/wait) |
| Load balancing | compile-time | runtime, adaptive |
| Determinism | bit-exact under BITEXACT | input-deterministic only (atomic-order variations tolerated) |
| Supported patterns | Known-at-compile-time control flow | Fully data-dependent |
| CI validation | bit-exact replay | input-deterministic replay + atomic-ordering sanity |

~2-5× per-task overhead is the cost of flexibility. Default is static; dynamic is opt-in via decorator.

#### 3.9.5 Determinism under dynamic scheduling

Under `BITEXACT_TC/STRICT` recipes, the dynamic scheduler's atomic queue uses deterministic push ordering:

```
queue_push(task):
    slot = atomicAdd(queue.write_ptr, 1) mod queue_capacity
    queue.slots[slot] = task
```

The `atomicAdd` return value gives a canonical queue position; same input task set → same slot assignments. Worker CTAs pop in canonical order via a second atomic. Result: input-deterministic replay, though not byte-deterministic across hardware atomic-ordering variations.

For strict byte-determinism requirements, dynamic scheduling is unavailable; fall back to `cr.eager_mode()` with `force_cpu=True`.

#### 3.9.6 When not to use dynamic

Dynamic is strictly an escape valve. If a workload compiles cleanly into LoopNode/BranchNode, keep it static — determinism + lower overhead + simpler debugging. Dynamic scheduler is for genuinely irregular patterns that would otherwise require Python-level dispatch (defeating the mock-tensor-capture model).

Prefer combined approach: static outer plan + dynamic sub-plan for one unpredictable section, embedded via `ChainEdge`. Dynamic section completes → chain edge signals → static plan resumes.

---

## 4. Frontend-agnostic design

### 4.1 The Vessel adapter pattern

Each frontend has a ~2K-LoC adapter that:

1. **Registers with the frontend's dispatch layer** to intercept ops
2. **Translates op call → CKernelId + TensorMeta** (looks up schema hash, extracts inputs)
3. **Returns a frontend-compatible mock tensor** (`CrucibleTensorImpl` wearing the frontend's `TensorImpl` interface)
4. **Handles sync points** (forces materialize when needed)

Everything else — Forge, Mimic, CNTP, Cipher, Canopy — is shared across frontends.

### 4.2 PyTorch Vessel

```
crucible/vessel/pytorch/
├── Interceptor.cpp       — DispatchKey::Crucible registration
├── TensorMeta.cpp        — ATen Tensor → CrucibleTensorImpl
├── SchemaRegistry.cpp    — aten::mm → CKernelId::GEMM_MM, etc.
├── MockImpl.cpp          — CrucibleTensorImpl subclass of c10::TensorImpl
├── Sync.cpp              — .item() / .cpu() / .numpy() handling
├── Autograd.cpp          — hook into PyTorch's autograd (Path A)
├── Init.cpp              — Python binding + Crucible bootstrap
```

~2K LoC. The most mature adapter because PyTorch's dispatch has been doing this for a decade.

### 4.3 JAX Vessel

JAX traces through custom tensor types (`jax.core.Tracer`). We subclass `Tracer` with `CrucibleTracer`:

```
crucible/vessel/jax/
├── Tracer.cpp            — CrucibleTracer implementing abstract_eval
├── Primitives.cpp        — map jax primitives → CKernelId
├── Trace.cpp             — capture jaxpr via our tracer
├── AutodiffGlue.cpp      — integrate with jax.vjp / jax.jvp
├── PjrtAdapter.cpp       — satisfy PjRt client interface
├── Init.py               — `jax_neuronx`-style registration
```

~2-3K LoC. PjRt interface requires slightly more surface than PyTorch's dispatch.

### 4.4 Jittor / TensorFlow-lite / other

Similar template. Any frontend with a dispatch hook can be wrapped. Jittor has a clean graph IR already; the adapter is ~1.5K LoC. TF2 Eager is compatible but nobody's asking.

### 4.5 Native Python frontend

For users who want to skip frameworks entirely:

```python
import crucible as cr

with cr.Session(recipe="f16_f32accum_pairwise") as s:
    x = cr.tensor.randn([256, 1024], dtype=cr.f16)
    w = cr.tensor.randn([1024, 4096], dtype=cr.f16, requires_grad=True)
    y = cr.ops.matmul(x, w)
    z = cr.ops.relu(y)
    loss = cr.ops.mean(z)
    loss.backward()
    s.step(loss, weights=[w])
```

The native-Python frontend is thinner than PyTorch-Vessel because there's no framework dispatch to intercept — we own the ops directly. `cr.tensor.*` constructors return `CrucibleTensorImpl` directly. `cr.ops.*` produces new tensors by capturing. ~1K LoC of Python bindings + shared runtime below.

### 4.6 Native C++ frontend

Embedded, inference-only, or systems-oriented deployments skip Python entirely:

```cpp
#include <crucible/frontend/cpp.h>
using namespace crucible;

Session s({.recipe = "f16_f32accum_pairwise"});
auto x = s.randn<f16>({256, 1024});
auto w = s.parameter<f16>({1024, 4096});
auto y = s.matmul(x, w);
auto z = s.relu(y);
auto loss = s.mean(z);
s.backward(loss);
s.step(loss, {w});
auto loss_value = loss.item();  // materialize
```

~500 LoC of C++ header-only bindings over the shared runtime. Useful for: inference servers, embedded ML, CI/test harnesses for Crucible itself, benchmark suites.

### 4.7 Native Rust frontend

```rust
use crucible::prelude::*;

let s = Session::new(SessionConfig { recipe: "f16_f32accum_pairwise", ..default() });
let x = s.randn::<f16>(&[256, 1024]);
let mut w = s.parameter::<f16>(&[1024, 4096]);
let y = s.matmul(&x, &w);
let z = s.relu(&y);
let loss = s.mean(&z);
s.backward(&loss);
s.step(&loss, &mut [&mut w]);
```

~1K LoC of Rust bindings via bindgen over the C API. The Rust frontend is especially useful for: async inference servers (tokio), systems programming, WASM exports.

### 4.8 Frontend-agnosticism in practice

Every frontend drops into the same capture loop. The cost of adding a new frontend is dominated by the dispatch-hook surface, not by any Crucible-internal work. A motivated contributor can add a new frontend in a week.

The flip side: **Crucible is the runtime the user talks to, not the frontend.** When something goes wrong in production, the diagnosis tools are Crucible's (`cr.debug`, `crucible-top`, Cipher inspection). The frontend is an API, not a debugging surface.

---

## 5. CNTP — Crucible Native Transport Protocol

Five layers in ~32K LoC total. Replaces NCCL + RCCL + UCX + MPI + hcoll + libsharp for our use case, without any of their assumptions (no fixed world, no rank-as-identity, no rigid topology).

### 5.1 Layer 1 — Transport

Thin uniform API, three concrete implementations:

```cpp
namespace crucible::cntp::transport {

class Transport {
public:
    virtual ~Transport() = default;

    // Eager small send (<= MTU). ~1-5μs RDMA, ~100ns NVLink.
    [[nodiscard]] virtual Status send_eager(
        fx::Bg, PeerEndpoint*, std::span<const uint8_t>, MsgHeader) = 0;

    // Zero-copy RDMA write. Peer reads from pre-negotiated remote region.
    [[nodiscard]] virtual Future<void> write_rdma(
        fx::Bg, PeerEndpoint*, RemoteRegion, std::span<const uint8_t>) = 0;

    // Zero-copy RDMA read.
    [[nodiscard]] virtual Future<std::span<uint8_t>> read_rdma(
        fx::Bg, Arena&, PeerEndpoint*, RemoteRegion) = 0;

    // One-sided atomics (FETCH_ADD, COMPARE_AND_SWAP).
    [[nodiscard]] virtual Future<uint64_t> atomic_faa(
        fx::Bg, PeerEndpoint*, RemoteAddr, uint64_t delta) = 0;

    // Register memory region for zero-copy access.
    [[nodiscard]] virtual LocalRegion register_region(
        std::span<uint8_t>, RegionFlags) = 0;

    // Poll completions.
    [[nodiscard]] virtual std::span<CompletionEvent> poll_completions(
        fx::Bg, std::span<CompletionEvent> buf) = 0;
};

} // namespace
```

Three implementations:

- **`RdmaTransport`** — libibverbs. Works for both InfiniBand and RoCE v2. 1-5μs latency, 50-400 Gb/s. QP pools per peer, registered memory regions, GPUDirect-RDMA where NIC supports it. Primary data-plane.
- **`NvlinkTransport`** — intra-node only. `cuMemcpyPeerAsync`, optional NvShmem primitives. Full-mesh if NVLink switch present; ring otherwise. 100ns latency, 900 GB/s on H100 SXM5. Equivalent backends for AMD XGMI, TPU ICI.
- **`TcpTransport`** — io_uring with IORING_SETUP_SQPOLL. Syscall-free in steady state; kernel polls the submission queue. 20-40μs latency. Fallback for WAN, heterogeneous clusters, development.

Router picks transport per-message based on (src, dst, msg_size, priority, topology).

### 5.2 Layer 2 — Gossip (SWIM + Lifeguard)

Classical SWIM (Das, Gupta, Motivala 2002) with Lifeguard extensions (DeGhett et al. 2008):

- Every 1s, each Relay picks K=3 random peers from its membership view
- Sends a probe; peer responds with ACK
- Missed ACK after RTT + jitter → mark peer `suspect`
- Indirect probes via K=3 neighbors before declaring dead
- 3-way handshake before quorum accepts `confirmed-dead`
- State summary piggybacks on normal probes — Merkle hash of (peer list, epoch, owned shards)

Convergence: O(log N) rounds across N peers. Failure detection: ~5s default (tunable).

Message format: ~64 bytes per probe. Eager path via Layer 1. Runs entirely in XDP (see §6) for receive side, eliminating kernel stack.

### 5.3 Layer 3 — Consensus (Raft, scoped)

Raft for critical decisions only:

- **Topology commits** — fleet-wide DP/TP/PP partition changes must be atomic
- **Cipher promotion** — moving state from hot→warm→cold
- **Keeper leader election** — for single-writer roles like "this Keeper coordinates the next Forge recompile"
- **Recipe registry updates** — cluster-agrees on a new recipe being available

Raft does NOT run for:

- Per-step all-reduce (direct collective, latency-critical)
- Gradient updates (datapath)
- Metric aggregation (CRDT-friendly, eventual consistency fine)
- Kernel cache invalidations (lazy, content-addressed)

One Raft group per cluster. Leader election on bootstrap or leader failure. Standard Diego Ongaro implementation. ~5K LoC of clean C++26, ported/inspired by existing implementations. Uses Layer 1 transport for AppendEntries/Vote RPCs.

### 5.4 Layer 4 — Collectives (protocol)

Protocol-level collective operations. Per-vendor implementation lives in Mimic (§37 of MIMIC.md); CNTP provides the algorithm primitives.

```cpp
namespace crucible::cntp::coll {

[[nodiscard]] std::span<uint8_t> all_reduce_sum(
    fx::Bg bg, Arena& arena,
    ContentHash tensor_hash,
    std::span<const uint8_t> local_shard,
    ReduceGroup group,
    const NumericalRecipe* recipe);

[[nodiscard]] std::span<uint8_t> all_gather(
    fx::Bg bg, Arena& arena,
    ContentHash tensor_hash,
    std::span<const uint8_t> local_shard,
    ReduceGroup group);

// ... reduce_scatter, all_to_all, broadcast, send/recv p2p
}
```

Algorithm selection per-call, driven by:

- Meridian's current N×N latency/bandwidth matrix
- Message size
- `recipe.determinism`:
  - `BITEXACT` → pinned-order binary tree sorted by UUID. Deterministic regardless of peer arrival order. Slightly slower than ring.
  - `ORDERED` → ring or halving-doubling, ordered within group
  - `UNORDERED` → any topology, opportunistic
- NetworkOffload eligibility (§5.5)

Dynamic membership: on failure mid-collective, surviving peers re-route via the next round's topology. Consensus layer commits new membership at epoch boundary; mid-collective failures retry with bounded delay.

### 5.5 Layer 5 — NetworkOffload plane (optional)

Per MIMIC.md §38. In-network aggregation hardware (Mellanox SHARP, TPU ICI aggregation, AMD XGMI reductions, Cerebras SwarmX). Capability-queried, capability-tolerant, auto-fallback to Layer 4 software collectives.

From CNTP's perspective, `NetworkOffload` providers are opt-in plugins consulted at collective dispatch time. If a matching provider is available and eligible for the current call, CNTP routes through hardware offload. Otherwise, software path.

---

## 6. Zero-userspace-tax comms in steady state

The userspace tax comes from three places. Each gets killed separately.

### 6.1 Sources of tax

- **Syscall cost** — 100-300ns per syscall (mode switch, TLB flush, PCID updates, Spectre mitigations)
- **Buffer copy cost** — 1-10μs per MB for kernel `copy_to_user` / `copy_from_user`
- **Context switch cost** — 300ns-2μs per switch

### 6.2 How each layer avoids the tax

| Mechanism | Path | Eliminates |
|---|---|---|
| RDMA verbs | mmap'd NIC doorbell + pre-registered MR + CQ polling | All three for bulk data |
| XDP BPF | NIC driver hook, before IP stack | Syscalls + context switches for matched control-plane packets |
| AF_XDP sockets | UMEM shared between NIC and userspace | Buffer copies for XDP-redirected packets |
| io_uring + SQPOLL | Kernel polls SQ ring; userspace just writes SQEs | Syscalls for TCP fallback paths |
| Shared memory / NVSHMEM | Direct peer memory access | Everything; no kernel at all |
| NIC/switch offload (SHARP, ICI) | Reduction happens in fabric hardware | Host CPU doesn't see payload |

### 6.3 Composing them

```
                       userspace (Crucible Keeper)
 gossip        Raft       collective     
 worker        worker     worker
     │         │              │
     │ spin-polls        │ mmap'd NIC  
     │ UMEM rx ring     │ doorbell +
     │                   │  CQ ring
     │                       │
───│──────────────│──────────│─────────────── kernel/userspace
     │                       │
  XDP program           (NIC bypass —
  matches CNTP          direct hardware
  magic bytes,          access via mmap)
  updates BPF map,           │
  XDP_TX ack,                │
  or REDIRECT to             │
  userspace UMEM             │
     │                       │
───│───────────────────│─────────────────── driver/hardware
     │                       │
     └───── RDMA NIC ────────┘
                 │
                 ▼
           RoCE v2 / InfiniBand / NVLink
```

Every worker thread spins on acquire-loads in its mmap'd ring. NIC or XDP fills the ring. Kernel socket stack never runs for CNTP traffic.

### 6.4 Per-message cost breakdown

| Message | Path | Host latency | Syscalls |
|---|---|---|---|
| SWIM heartbeat | XDP in → update BPF map → XDP_TX ack | ~80ns (both sides) | 0 |
| Raft AppendEntries (empty) | NIC → XDP_REDIRECT → UMEM → userspace poll | ~200ns | 0 |
| Small RPC (<256B) | NIC → XDP_REDIRECT → UMEM | ~300ns | 0 |
| Bulk tensor send (100MB) | Pre-registered MR → NIC DMA | ~2ms wire-limited, ~50ns host | 0 |
| SHARP-offloaded all-reduce | post WR, wait CQE; switch does reduction | ~10μs fabric, ~100ns host | 0 |
| Rare TCP fallback send | io_uring SQPOLL SQE | ~2μs | 0 (SQPOLL) |

### 6.5 One-time setup

Paid at Keeper init, never after:

| Operation | Cost |
|---|---|
| `ibv_open_device` + `ibv_alloc_pd` per NIC | ~1ms |
| `ibv_reg_mr` for 80GB HBM pool | ~100μs |
| `ibv_create_qp` per peer | ~100μs × N peers (parallelizable) |
| `bpf_obj_load` + attach XDP | ~10ms |
| AF_XDP socket setup per ring | ~1ms |
| **Total init for 8-node cluster** | **~50-100ms** |

Amortizes across hours/days of training.

### 6.6 What stays in userspace, and why

eBPF is bounded (~1M instructions per program, must prove termination, limited stack, whitelisted helpers only). Not suitable for:

- Raft state machine (unbounded log, conditional logic depends on history)
- SWIM indirect probes (multi-round state machine)
- Collective algorithm selection (size + topology heuristics)
- Cryptographic signatures on Cipher entries

Rule: **fast path in eBPF, slow path in userspace**. Typical distribution: 95% of packets handled entirely in XDP, 5% redirected to userspace for complex decisions.

---

## 7. Canopy — the fleet mesh

Canopy is the union of all Keepers. Not a service, not a topology — an emergent mesh maintained by gossip + consensus. No master node, no registry, no coordinator. Any Relay can join or leave at any time; the mesh adapts.

### 7.1 Node identity

Every Relay has a **128-bit UUID**, stable across reincarnations, derived once at first boot from hardware attestation (TPM, vendor-specific ID, or fallback: random + persisted). Never re-assigned.

```cpp
struct SourceUuid {
    uint64_t hi;
    uint64_t lo;
    auto operator<=>(const SourceUuid&) const = default;
};
```

Rank (when used — e.g., for ordering in a ring collective) is a view computed from sorted UUIDs at the current epoch. Not an identity.

### 7.2 Membership lifecycle

```
Keeper starts up
     │
     ▼
Hardware probe (chip id, NIC, memory, recipes supported)
     │
     ▼
Discovery (multicast, seed peer list, k8s Downward API, or SLURM env)
     │
     ▼
Gossip join — exchange state with K=3 random peers
     │
     ▼
Raft membership commit (epoch += 1)
     │
     ▼
Fleet-intersection recompute (recipe registry × new member caps)
     │
     ▼
Available for work; Forge may dispatch compile requests
     │
     ▼
 (running — processes ops, participates in collectives)
     │
     ▼
Graceful shutdown OR unexpected death
     │
     ▼
Gossip propagates `confirmed-dead`; Raft commits new membership
     │
     ▼
In-flight collectives retry on new topology; compute reshards if needed
```

### 7.3 Heterogeneous hardware coexistence

A Canopy can contain Relays of different chip types (H100, MI300X, v5p, trn2, CPU-only) simultaneously. Each Relay advertises its chip + native recipe bitmap:

```json
{
  "uuid": "...",
  "chip": "nv_sm_100a",
  "ncs_visible": 8,
  "native_recipes": ["0x9E37...", "0x5B1C...", "..."],
  "transport_endpoints": ["rdma://...", "nvlink://..."],
  "epoch": 42
}
```

Raft maintains the fleet-wide `native_recipe_intersection` per ReduceGroup. When members differ:

- **STRICT policy**: Forge refuses to pick a recipe not in the intersection. Heterogeneous group may need to be split into vendor-homogeneous sub-groups.
- **ADAPT policy**: Forge picks the best-available recipe; emulation-on-weakest-member is tolerated, perf tax is accepted.

Default for training: STRICT (determinism first). Default for inference: ADAPT (availability first). Configurable per-workload.

### 7.4 Fleet reshard

When membership changes (node joins, node dies, cluster grows/shrinks), Raft commits new membership at epoch E+1. In-flight computation at step T:

1. Raft signals epoch bump to all Relays
2. Current step T completes on old membership (or is aborted if a dead node held data we needed)
3. At iteration boundary:
   - Recompute DP/TP/PP partitioning (Phase K of Forge, triggered again)
   - Redistribute shards per new partition
   - Cross-Relay handoffs that carry live Cipher hot-tier state use
     `EpochedDelegate<DelegatedSession<...>, K, E+1, generation>` so
     the sender cannot weaken the declared epoch/generation and stale
     recipients cannot accept the delegated endpoint.  FOUND-G70 (#739)
     consumes this type-level fence at the production Canopy
     collective/reshard call sites.
   - Continue from step T+1 on new topology

If a node died mid-step with data we needed, we roll back to the last Cipher checkpoint and replay (see §10). Bounded cost: N steps of recompute (N = checkpoint interval).

### 7.5 No master, no coordinator

Every architectural decision in Canopy follows from "no master":

- **No "rank 0 owns init"** — every Relay initializes independently, joins via gossip
- **No "rank 0 collects gradients"** — Raft-orchestrated reduction, canonical ordering by UUID
- **No "rank 0 handles checkpointing"** — Cipher is Raft-replicated; any Relay can write, any can read
- **No "rank 0 drives the compile"** — any Keeper can spawn Forge, cache results shared via Cipher
- **No "rank 0 coordinates dataloader"** — DataNode CPU pods are Relays in their own right; they gossip their cursor state like anyone else

The k8s operator in §8 has no "master pod". The SLURM launch in §8 does not rely on `SLURM_PROCID=0`. Training survives the death of any single Relay including what traditional systems would call the "chief" or "coordinator."

---

## 8. k8s-Canopy operator + SLURM launch

### 8.1 CrucibleCluster CRD

A single Kubernetes CustomResourceDefinition for deploying a Crucible training/inference workload.

```yaml
apiVersion: crucible.ai/v1
kind: CrucibleCluster
metadata:
  name: llama-70b-train
  namespace: ml
spec:
  model:
    config_uri: s3://cfg/llama-70b.json
    precision_recipe: "f16_f32accum_pairwise"
    checkpoint_interval_steps: 500
    checkpoint_uri: s3://ckpt/llama-70b/
  data:
    shards: ["s3://pile/shard-*.zst"]
    shuffle_seed: 42
  compute:
    gpu_replicas:
      min: 8
      max: 128
      type: h100-sxm5
      affinity: node-pool=gpu-h100
    cpu_replicas:
      min: 2
      max: 16
      affinity: node-pool=data-workers
  policy:
    fleet: ADAPT           # or STRICT
    elastic: true          # allow scaling during run
    reshard_on_failure: true
  network:
    rdma: required         # abort if no RoCE/IB on nodes
    ebpf_control_plane: true
status:
  phase: Training
  current_step: 12847
  last_checkpoint_step: 12500
  active_members: 64
  degraded_members: 2
  recipe_intersection_size: 14
```

### 8.2 Operator implementation

`crucible-operator` controller, ~8K LoC Go (standard k8s operator pattern). Translates a `CrucibleCluster` CR to:

- **DaemonSet** for Keeper pods, one per matching node (GPU DaemonSet + CPU DaemonSet)
- **ConfigMap** with model/data/policy config (spread via gossip, not Services)
- **PersistentVolumeClaim** if Cipher warm-tier is local (or no PVC if Cipher is S3-only)
- **No Deployment** — every Keeper is peer-level
- **No StatefulSet** — no ordering required; UUIDs are stable, not pod-indexes
- **No master pod** — not a deployment pattern we support

Peer discovery: **not via k8s Service**. Services introduce master semantics (ClusterIP, Endpoints). Instead:

- Multicast announcement on the pod network (works if pod network supports it), OR
- Seed-peer URLs in the ConfigMap that any pod can resolve (a bootstrap hint, not a coordinator)

Once one peer is reached, SWIM gossip takes over; membership converges in O(log N) rounds.

### 8.3 Scaling

```
kubectl scale crucible-cluster llama-70b-train --replicas=128
```

Operator requests more Keeper pods (up to max). New pods join Canopy via SWIM. Raft admits them at next membership commit. Reshard triggers at next iteration boundary.

Scale-down symmetric. `kubectl delete pod keeper-xyz` → gracefully quiesces (finishes current op, signals leave via gossip) → Raft removes → reshard if needed → training continues.

Pod eviction by k8s (node drain, resource pressure): Kubernetes sends SIGTERM to the pod; Keeper's signal handler:
1. Announces leave via gossip
2. Checkpoints any unique state to Cipher
3. Exits

k8s spawns replacement pod on a different node; replacement joins Canopy; reshard happens.

### 8.4 Horizontal autoscaler

`CrucibleCluster` supports `spec.compute.gpu_replicas.autoscale`:

```yaml
autoscale:
  min: 8
  max: 128
  metric: throughput  # tokens/sec / gpu
  target: 0.85        # 85% peak throughput
  cooldown_seconds: 300
```

Controller watches metrics from Augur (via a Prometheus scrape endpoint on each Keeper). Below-target throughput × stable for cooldown → scale up. Above-target + idle time → scale down.

### 8.5 SLURM integration

Simpler, launcher-only pattern:

```bash
#!/bin/bash
#SBATCH --nodes=64
#SBATCH --ntasks-per-node=1
#SBATCH --gpus-per-node=8
#SBATCH --time=24:00:00

srun --export=ALL \
    crucible-keeper \
    --config=/scratch/crucible/llama-70b-train.json \
    --seed-peers=$(scontrol show hostname $SLURM_NODELIST | head -1):7831
```

SLURM allocates N nodes. Each node starts one Keeper via `srun`. Keepers discover via the seed-peer hint (first node's hostname). Canopy forms, training proceeds. SLURM's `PMI_RANK` / `SLURM_PROCID` are **ignored** — rank in Crucible is a view, not identity.

Elasticity under SLURM: limited. SLURM typically allocates a fixed node set for a job's duration. If we want elastic, use multiple concatenated jobs (`sbatch --dependency=afterany:...`) or run in k8s instead.

### 8.6 Bare-metal launch

No scheduler, direct systemd unit:

```ini
# /etc/systemd/system/crucible-keeper.service
[Unit]
Description=Crucible Keeper daemon
After=network-online.target nvidia-modprobe.service

[Service]
ExecStart=/usr/local/bin/crucible-keeper --config=/etc/crucible/node.json
Restart=always
RestartSec=10
User=crucible
Group=crucible
# allow RDMA, eBPF, large locked memory
AmbientCapabilities=CAP_NET_ADMIN CAP_BPF CAP_IPC_LOCK
LimitMEMLOCK=infinity

[Install]
WantedBy=multi-user.target
```

Each node runs one daemon. Gossip discovers peers from the config's seed-peer list or multicast. Identical semantics to k8s / SLURM launch; just no orchestrator on top.

### 8.7 Mixed-scheduler clusters

One Canopy can span multiple scheduler domains. Example: GPU Relays on-prem under SLURM, CPU data-loader Relays in k8s on the same network. All join the same Canopy via RDMA/TCP. Raft/gossip is scheduler-agnostic. The only requirement: IP reachability between all Keepers.

---

## 9. Cipher — content-addressed persistence

Cipher is Crucible's event-sourced state store. Three tiers; all content-addressed; all replicated via Raft for durability commits.

### 9.1 Three tiers

| Tier | Storage | Latency | Durability |
|---|---|---|---|
| **Hot** | In-process RAM + RDMA-registered (peers can read) | ~100ns local, ~1μs remote | Survives process restart only if replicated to other Relays' hot tiers |
| **Warm** | Local NVMe, per-Relay | ~20μs read | Survives process restart; not machine failure |
| **Cold** | S3-compatible object store (S3, GCS, MinIO, Ceph) | ~50ms read | Survives cluster failure; federation-shareable |

Entries promote hot → warm → cold based on age + access patterns. A freshly-computed kernel cache entry lives in hot; accessed rarely, it warms; after 24h without use, it colds.

### 9.2 Content-addressed layout

Every Cipher entry keyed by content_hash. Layout:

```
/cipher/
├── hot/                    — in-process, not on disk
│   └── <hash-prefix>/<content_hash>.bin → mmap'd arena bytes
├── warm/                   — local NVMe
│   └── <hash-prefix>/<content_hash>.bin
│   └── <hash-prefix>/<content_hash>.meta.json
└── cold/                   — S3-compatible
    └── <bucket>/crucible/<hash-prefix>/<content_hash>
```

`hash-prefix` is the first 3 hex chars of the content_hash for directory sharding (4096 subdirs).

Metadata file accompanies each entry:

```json
{
  "content_hash": "0x9E3779B97F4A7C15...",
  "kind": "ExecutionPlan",
  "size_bytes": 142376,
  "created_at": "2026-04-17T12:34:56Z",
  "accessed_at": "2026-04-17T14:22:01Z",
  "promotion_count": 3,
  "dependencies": ["0x5B1C...", "0x7ABF..."],
  "tags": ["model=llama-70b", "recipe=f16_f32accum_pairwise", "chip=h100_sxm5"]
}
```

### 9.3 Entry kinds

Cipher stores:

- **CompiledKernel** (binary bytes + predicted cycles + insights) — L3 cache of KernelCache
- **IR003\* snapshot** (per-vendor) — L2 cache
- **IR002 snapshot** (vendor-neutral) — L1 cache; federation-shareable at the cold tier
- **MAP-Elites archive** per kernel-family + chip — warm-start for future compiles
- **ExecutionPlan** — full compiled training-step plan
- **TrainingCheckpoint** — `(weights, optimizer_state, data_cursor, seed, step_idx, epoch)`
- **CalibrationData** — per-chip TOML/JSON calibration TargetCaps extensions
- **RecipeRegistry snapshot** — point-in-time view of the global recipe catalog
- **Residuals** — Augur's measurement-vs-prediction dataset
- **Merkle DAG snapshots** — versioned computation graph for bisect/blame

### 9.4 TrainingCheckpoint format

The load-bearing persistence artifact for §10 replay determinism.

```json
{
  "version": 1,
  "step_idx": 12500,
  "epoch": 3,
  "model_content_hash": "0x9E37...",
  "recipe_registry_epoch": 42,
  "seed": 8675309,
  "data": {
    "shard_cursor": [
      {"shard_uri": "s3://pile/shard-0042.zst", "byte_offset": 1834827648},
      {"shard_uri": "s3://pile/shard-0043.zst", "byte_offset": 0}
    ]
  },
  "fleet_uuids_at_checkpoint": ["uuid1", "uuid2", "..."],
  "weights_blob": "0xABCD...",          // hash → binary entry in Cipher
  "optimizer_state_blob": "0xEF01...",  // hash → binary entry in Cipher
  "metadata": {
    "time_to_checkpoint_ms": 483,
    "byte_size_total": 150293847568
  }
}
```

Weights and optimizer state are separate Cipher entries (they're large; JSON wrapper is ~1KB, binary blobs can be 100GB+). The checkpoint manifest is content-addressed; the blobs are content-addressed; nothing is inconsistent. Incremental checkpointing via deduplication — if only 1% of weights changed since last checkpoint, only 1% of blob chunks need new Cipher entries (content-addressing handles this for free).

### 9.5 Cross-run federation

L1 IR002 snapshots and recipe registry snapshots are **federation-shareable**. Two Crucible installations running the same model produce identical IR002 content hashes; each can read the other's L1 cache from a shared cold-tier bucket. Meta's compilation of Llama-70B in Dublin enriches a researcher's cold-tier cache in Tokyo for the same model.

Privacy guarantees:

- **L1 entries contain no weights** — just kernel templates, recipes, tile specs. Safe to share.
- **L2 entries contain no secrets** — vendor-specific IR003* snapshots, no trained parameters.
- **L3 entries (compiled binaries)** — safe to share but chip-specific; useful only to same-chip sites.
- **TrainingCheckpoint entries** are private by default; federation opt-in per-namespace.

### 9.6 Raft replication

Committed Cipher writes go through Raft to guarantee durability across at least majority of replicas. For hot tier, this means at least one other Keeper has the entry in its hot tier before the write is acknowledged. For warm tier, at least one other Keeper has flushed to its NVMe.

Cold tier writes are to the object store directly; S3/GCS provides durability. Cipher metadata points to the cold-tier object, is then Raft-replicated (metadata is tiny).

Read path: any Keeper can read any entry. Locality matters — if peer A has the entry hot, peer B reads over RDMA in ~1μs instead of from warm NVMe in ~20μs. Gossip maintains per-entry "where is it hot" hints so readers aim at the right peer.

### 9.7 GC and retention

Entries accrue over a training run. Retention policy:

- **Hot**: LRU eviction at memory pressure. Bounded by `crucible.cipher.hot_bytes_max` (default 16GB per Keeper).
- **Warm**: LRU + age. Entries older than 30d are demoted to cold.
- **Cold**: retained indefinitely unless explicit cleanup. S3 lifecycle policy if you want it.

TrainingCheckpoints are never auto-collected — explicit user action required. KernelCache entries can be auto-collected because they're regeneratable.

---

## 10. Replay determinism — the load-bearing invariant

**Training state at step T is a pure function of `(weights_T, optimizer_T, cursor_T, seed)`. No hidden state anywhere.** This section specifies why, and how we verify it.

### 10.1 Sources of hidden state in conventional training

Why most ML stacks can't replay deterministically:

1. **RNG state**: torch.cuda.manual_seed sets a per-device state that advances with each op in implementation-dependent order. Recovering it requires replaying the exact same op sequence.
2. **CUDACachingAllocator**: allocation addresses depend on malloc/free history. Non-determinism in scheduling → non-determinism in addresses → non-determinism in bank conflicts / cache hits → timing-dependent behavior (at bit level with cudnn.benchmark).
3. **cuDNN algorithm selection**: picks the fastest kernel dynamically based on first-run timing. Different choice → different math → non-deterministic result.
4. **NCCL ring order**: at init, NCCL builds a ring based on topology detection. Different detection → different ring → different reduction order → different bits.
5. **Vendor library version drift**: cuBLAS 12.2 and 12.3 can produce different FMA orderings for the same op.
6. **Async scheduling**: in-flight kernels completing in different orders accumulate into different final values via atomic adds.

Every one of these is hidden state. Lose it, lose replayability.

### 10.2 How Crucible eliminates each

| Source | Our answer |
|---|---|
| RNG state | Philox4x32 counter-based. Key derives from `(seed, step_idx, op_idx, thread_idx)`. Pure function. Zero state. |
| Allocator addresses | L3 static memory plan. Every slot's address is `pool_base + offset`. Offsets are content-addressed. Same graph → same plan → same addresses. |
| Kernel selection | Forge + Mimic pin exactly one kernel per (content_hash, chip_caps) tuple. No dynamic selection. |
| Collective order | CNTP pinned-order binary tree sorted by UUID. Deterministic per epoch. |
| Library version | No vendor libraries. We version our own Mimic per chip; Cipher keys include our version. |
| Async ordering | Content-addressed chain tokens in IR002 impose explicit ordering. No atomics in reductions under BITEXACT recipes. |

### 10.3 Philox in detail

Philox4x32 (Salmon et al. 2011) is a counter-based PRNG:

- Input: `(key: 2×uint32, counter: 4×uint32)`
- Output: 128 bits of pseudo-random
- Stateless between calls — same input → same output
- Throughput: ~3 cycles per 128 bits on modern CPUs; essentially free

Per-op RNG usage in Crucible:

```
For each op that needs randomness (dropout, random_normal, bernoulli, ...):
  key     = (seed_lo, seed_hi)                    // training-run constant
  counter = (step_idx, op_idx, tile_idx, lane_idx)  // deterministic from position

  rand_bits = philox(key, counter)
```

Same `(key, counter)` always produces the same `rand_bits`, regardless of:

- Which chip is running this op
- Which thread got this work
- When this op executed
- Whether we're training for the first time or replaying after a crash

This is the keystone. Without it, replay is impossible.

### 10.4 Static memory plan in detail

From Forge's Phase G (see FORGE.md §12), every tensor has a `SlotId` with `(offset, size, alignment)` in a per-device pool. The plan is derived from the IR001 graph's lifetime analysis — deterministic given the graph.

```
plan.compute_offsets(graph):
  sorted_slots = sort(slots, by = slot.birth_op_idx)
  for s in sorted_slots:
    s.offset = first-fit bump into the pool below interval-occupancy graph
    (deterministic tiebreak: slot_id ascending)

result: same graph → same offsets, every run
```

Every runtime load of a checkpoint reconstructs the same plan, allocates the same pool, puts the same tensor at the same address. Layout is stable across machines, across reincarnations, across vendor backends.

### 10.5 Bit-exact kernel emission — four determinism tiers

The `NumericalRecipe::determinism` field (FORGE.md §19.1) admits four levels, each with a distinct cross-vendor guarantee and perf cost. The levels are not abstractions; every recipe in the registry declares exactly one level, and CI (§10.8) enforces it.

#### 10.5.1 Where the silicon diverges

Three sources of non-determinism at the tensor-core MMA level:

1. **Internal summation topology.** A WGMMA m64n128k16 on Hopper computes `Σᵢ aᵢ·bᵢ + c` as a dot product over K. The partial products are summed in a carry-save adder tree whose shape NVIDIA documents only partially. AMD's MFMA 32×32×8 uses a different tree. TPU's MXU, a third. Even with round-to-nearest-even throughout, order-of-summation differences can yield ≤1 ULP divergence at mixed-precision boundaries.
2. **FTZ / denormal policy.** NVIDIA sm_90+ tensor cores default `FTZ = off`; scalar FP defaults `FTZ = on`. AMD CDNA3 has its own convention; TPU varies by generation. If one backend flushes denormals and another preserves them, outputs diverge on any input that produces denormals.
3. **Widening discipline.** FP16 × FP16 → FP32 accumulate: some silicon widens both operands to FP32 before MAC (two rounding events); others do the multiply in the extended 22-bit format and add to FP32 accumulator with a single round. The two produce different bits at corner cases.

(1) is the hardest and the reason `BITEXACT_STRICT` exists as a separate tier. (2) and (3) are tractable via software correction during IR003* realization (FTZ pinning, explicit FP32 widening before MMA).

#### 10.5.2 The four tiers

| Level | Perf (vs UNORDERED) | Cross-vendor guarantee | Typical use |
|---|---|---|---|
| `UNORDERED` | 1.00× | no guarantee (≤100 ULP in practice) | inference where reproducibility is not required |
| `ORDERED` | 0.95-0.98× | ≤4 ULP, reduction topology pinned | training with mixed-vendor fleet, relaxed reproducibility |
| `BITEXACT_TC` | 0.92-0.96× | ≤1 ULP; 0 ULP on K≤8 MMA fragments | training with mixed-vendor fleet, tight reproducibility |
| `BITEXACT_STRICT` | 0.02-0.10× | 0 ULP byte-identical on any silicon | reference validation, regression CI, legal / compliance reproducibility |

**`UNORDERED`.** The backend picks any algorithm, any reduction order, any tile shape. Fastest. Used for inference where each query is independent and cross-run divergence is acceptable. Not used for training.

**`ORDERED`.** Reduction topology pinned (pairwise tree by canonical index ordering); tile shapes free to vary per chip; cross-vendor ULP bound declared per-recipe in the registry. Tolerance typically 2-4 ULP for FP16, 4-8 ULP for FP8. Default for mixed-precision training on heterogeneous fleets. Perf tax ~3-5% vs UNORDERED.

**`BITEXACT_TC`.** The discipline that makes mixed-vendor training practically reproducible without sacrificing tensor-core throughput. Decomposes large-K reductions into K≤8 tensor-core fragments (small enough that the summation tree collapses to unambiguous pairwise order on every vendor) plus a pinned outer scalar reduction chain. Each fragment is bit-exact cross-vendor by construction (tree shape is unambiguous); the outer chain is our-emitted scalar FMA (bit-exact by canonical order). Perf tax ~5-8% vs UNORDERED. Only FP32 and select mixed-precision recipes with K≤8 tensor-core realization qualify; recipes declare `tc_shape_constraint` in the registry (FORGE.md §20.1).

**`BITEXACT_STRICT`.** No tensor cores. All reductions as scalar FMA chains with a single canonical tree shape. Bit-identical on any silicon, including cross-architecture (NV ↔ AM ↔ TPU ↔ TRN ↔ CPU). 10-50× slower than UNORDERED. Reserved for correctness validation (the CPU reference oracle runs in this mode), regression CI (a reference run to compare every backend against), and legal/compliance use cases where byte-identical reproducibility is load-bearing.

#### 10.5.3 Per-vendor realization

Every backend's `realize_recipe` (MIMIC.md §40) honors the tier:

- Pin FTZ explicitly via `setreg .denormal_mode` (NV), `s_setreg MODE` (AM), or equivalent per backend. A recipe with `BITEXACT_TC` or tighter forces FTZ=off across all backends.
- For `BITEXACT_TC`: prefer small-K tensor-core shapes. NV: WGMMA `m64n128k8` over `m64n128k64`. AM: MFMA `32×32×8` over `32×32×16`. TPU: MXU with K=8 accumulator resets. The outer reduction is our scalar FMA chain in pinned pairwise order.
- For `BITEXACT_STRICT`: disable tensor cores entirely; emit scalar FMA in pinned pairwise order.

The CPU reference always implements `BITEXACT_STRICT`; it is the oracle every other backend is compared against in the cross-vendor CI matrix (§10.8, MIMIC.md §41).

#### 10.5.4 FP32 vs FP16 guarantees

- **FP32** (no widening, no denormal flushes on any supported silicon). `BITEXACT_TC` and `BITEXACT_STRICT` are both achievable and byte-identical to each other for pure-FP32 kernels. Perf tax for TC vs STRICT on FP32 is negligible because FP32 tensor-core shapes on modern silicon already use K=8 fragments.
- **FP16 mixed-precision with FP32 accumulate**. `BITEXACT_TC` achievable with the K≤8 discipline. `BITEXACT_STRICT` achievable but at 20-50× slowdown (emulate every MAC as scalar FMA). `ORDERED` is the pragmatic default for production training.
- **FP8 MX and FP4 MX**. Cross-vendor divergence from block-scale application is ≤8 ULP and not closeable by software correction. These recipes never declare `BITEXACT_*`; the highest available tier is `ORDERED` with per-recipe ULP tolerance in the registry.

#### 10.5.5 What it buys

A user can declare `cr.Trainer(..., determinism="BITEXACT_TC")` on a heterogeneous fleet (H100 + MI300X + trn2 + v5p), train 10,000 steps, and get weights within ≤1 ULP of what a pure H100 run would produce. Checkpoint divergence across the fleet is bounded; gradient aggregation via canonical-tree reduction composes ULP bounds linearly; `BITEXACT_STRICT` re-runs on CPU provide the oracle for arbitration if divergence ever exceeds tolerance.

### 10.6 Canonical reduction topology

All-reduce over a DP group of N members:

```
Sort members by UUID (canonical)
Build a binary tree: level 0 is pairs (m0,m1), (m2,m3), ...; level 1 is (pair0_result, pair1_result), ...; etc.

For each tree level:
  Partner exchange via RDMA write
  Sum locally
  Proceed to next level
```

Any peer arrival order produces the same tree, because the tree is derived from UUID-sort, not from arrival. Floating-point sums within a tree node are deterministic because operand order is fixed (left-child first). Across the tree, summation is ordered (left subtree sum + right subtree sum, then combined).

Under BITEXACT recipe, this is bit-deterministic. Under ORDERED, slightly faster algorithms (ring, HD) are used; still ordered within group, slight ULP differences cross-topology tolerated.

### 10.7 Checkpoint and recovery procedure

**Checkpointing** (every N steps, N configurable, default 500):

1. Raft elects a coordinator (round-robin across Keepers, not a fixed master)
2. Coordinator broadcasts "begin checkpoint at step T"
3. Every Keeper flushes its weight/optimizer shards to Cipher warm-tier
4. Cipher writes are Raft-replicated (at least majority)
5. DataNode Keepers checkpoint their cursor state
6. Coordinator writes the checkpoint manifest (see §9.4) to Cipher
7. Raft commits the manifest; acknowledgment returns to coordinator; training continues

Time to checkpoint: dominated by weight serialization (linear in weight size). On 8×H100 with 70B params at BF16 = 140GB total weights, spread across 8 shards = 17.5GB each, written to local NVMe at 3GB/s = ~6s per Keeper, parallel = ~6s total wall clock. With 500-step checkpoint interval at 1s/step = ~1% overhead.

**Recovery** (after a node dies):

1. Raft detects death (via SWIM → `confirmed-dead`), commits new membership at epoch E+1
2. All surviving Keepers query Cipher for the most recent checkpoint manifest
3. Find manifest at step T0 (≤ N steps before failure)
4. Reconstruct memory plan from `model_content_hash`
5. Load weights + optimizer state from Cipher (prefer hot tier from surviving peers, fall back to warm/cold)
6. Rewind data cursors to checkpoint positions
7. Replay steps T0 → T_crash deterministically:
   - Same Philox seed + step_idx → same RNG
   - Same data cursor → same batches
   - Same kernels from L3 cache → same gradients
   - Same reduction topology → same sums
8. Arrive at `weights_T_crash` bit-identical to what the dead run would have computed
9. Continue training at T_crash + 1

Cost bound: ≤ N steps of recompute. With N = 500 and 1s/step = ~500s of wasted compute per failure. Aggressive N = 100 = ~100s. Both far better than "lose 1 step + pray the RNG state rebuilds" (the conventional outcome).

### 10.8 Replay Determinism CI

Mandatory test suite from day 1 of implementation. Runs on every merged PR. If any test goes red, hidden state has been introduced — investigate immediately.

```
TEST: bit_exact_single_backend_replay
  1. Start Keeper on backend B with seed S
  2. Train 1000 steps, snapshot weights every 100 steps
  3. Kill Keeper after step 500
  4. Start fresh Keeper, load step-0 checkpoint
  5. Replay steps 1..1000
  6. Assert: byte_equal(run1.weights[i], run2.weights[i]) for i ∈ {100, 200, ..., 1000}
  7. Must pass for every BITEXACT recipe × every supported backend B

TEST: bit_exact_cross_backend
  1. Start Keeper on backend A (e.g., NV) with seed S, model M, recipe R (BITEXACT)
  2. Train 100 steps
  3. Start Keeper on backend B (e.g., AM) with same seed S, model M, recipe R
  4. Train 100 steps using same data shards
  5. Assert: byte_equal(nv.weights[i], am.weights[i]) for i ∈ {10, 20, ..., 100}
  6. Must pass for BITEXACT recipes; ULP-bounded for ORDERED

TEST: fleet_reshard_replay
  1. Start 8 Keepers, train 300 steps
  2. Kill Keepers {2, 5, 7} at step 250 (simulating failures)
  3. Raft commits 5-Keeper membership
  4. Replay from last checkpoint (T0 = 250 / N = 200)
  5. Train to step 400
  6. Start a second run with just 5 Keepers from step 0
  7. Assert: byte_equal(run1.weights[400], run2.weights[400])
  8. Must pass; validates that reshard doesn't break determinism

TEST: checkpoint_format_stability
  1. Write a TrainingCheckpoint at step T
  2. Read it back in the same process
  3. Assert: structural equality + byte equality of weight blobs
  4. Read it back in a fresh process (no in-memory state)
  5. Assert: same equality
  6. Read it back on a different backend (NV → AM)
  7. Assert: weights are loadable, step/epoch/cursor are preserved
```

These tests exercise the entire replay invariant — Philox, memory plan, kernel emission, collective topology, checkpoint format. If they pass, Crucible's core promise holds. If they fail, something has leaked non-determinism and the design is broken.

Budget: ~15-20K LoC of test harness + runner + oracles. Each test run takes ~10 minutes on a small cluster. CI pipeline: pre-merge gate (one backend), release gate (full matrix).

### 10.9 The value proposition, restated

Replay determinism isn't a nice-to-have. It's the property that:

- Makes mixed-vendor fleet training believable
- Makes debugging tractable (reproduce any step from a checkpoint)
- Makes scientific ML reproducible across compute environments
- Makes catastrophe recovery cheap (replay N steps, not restart from scratch)
- Makes regression tests possible (run the same training twice, compare)
- Makes continuous learning auditable (every weight change traceable)

Without it, Crucible would be another ML framework. With it, Crucible is a different category of tool.

---

## 11. Data loading as a first-class graph op

The DataLoader is not a Python iterator decoration; it's a `CKernelId::IO_PREFETCH` node in IR001 with typed output, captured lifecycle, checkpointed cursor state.

### 11.1 User-facing API

```python
# PyTorch-Vessel style
loader = cr.dataloader(
    path="s3://pile/shard-*.zst",
    batch_size=32,
    shuffle_seed=42,
    decompress="zstd",
    parse="msgpack",
    transform=lambda x: normalize(x),
    prefetch_depth=4,
)

for step, (x, y) in enumerate(loader):
    loss = trainer.step(x, y)  # captured, compiled, executed
```

`loader` is a normal Python iterator from the user's perspective. Internally:

- `cr.dataloader(...)` registers a DataNode in IR001
- Each iteration advances the DataNode's cursor
- The DataNode produces a `(x, y)` tuple of CrucibleTensorImpl mocks
- At sync point (step execution), the DataNode is materialized: a CPU worker reads the shard, parses, transforms, DMAs the result into the device slot

### 11.2 Execution model

DataNodes run on **CPU-class Keeper pods**. A training cluster has:

- N GPU Relays for compute (e.g., 64 × H100 pods)
- M CPU Relays for data loading (e.g., 8 × c5.24xlarge pods with NVMe storage)

CPU Relays subscribe to DataNode work via Canopy gossip. Each CPU Relay owns a subset of shards (assigned at epoch boundary based on UUID-sort). When a compute Relay needs a batch, the CNTP data-plane delivers it via RDMA:

```
GPU Relay's step T needs batch
     │
     ▼
Compute Relay's DataNode queue has 4 pre-fetched batches (prefetch_depth=4)
     │
     ▼
Oldest batch's bytes are already in device memory (was DMA'd while step T-4 was running)
     │
     ▼
Step T executes, consuming that batch
     │
     ▼
CPU Relay processes next batch in parallel, DMAs to device on completion
```

Backpressure: if CPU Relay can't keep up, compute starves. If compute is slow, CPU Relay's prefetch queue fills and it blocks. Both ends have bounded memory (configurable via `prefetch_depth`).

### 11.3 Cursor checkpointing

The DataNode's state is `(shard_uri, byte_offset)` tuples per CPU Relay, plus the shuffle seed + shuffle epoch.

Cursor is Raft-replicated and included in TrainingCheckpoint (§9.4). On recovery:

1. Cursors reload from the checkpoint manifest
2. CPU Relays resume from the exact byte offset in their assigned shards
3. Shuffle is deterministic (Philox-seeded) so the sequence of batches after recovery matches what the dead run would have produced

Combined with §10 replay determinism: after recovery, we get byte-identical batches, byte-identical gradients, byte-identical weights. Full determinism, end to end.

### 11.4 Shard assignment

Shards are content-addressed too. A shard's assignment to a CPU Relay is determined by `hash(shard_uri) % num_cpu_relays` with consistent hashing so membership changes redistribute only the affected shards. On fleet reshard:

- New CPU Relay joins: Raft commits, consistent hashing reassigns ~1/N shards
- CPU Relay leaves: its shards redistribute to surviving peers
- All re-assignments happen at iteration boundary; cursors migrate with shards

### 11.5 What DataLoader is NOT

- **Not PyTorch's DataLoader** — we don't spawn Python worker processes, don't rely on `fork()`, don't use `torch.utils.data.DataLoader`
- **Not WebDataset / Litdata** — though we can load those formats, the orchestration is ours
- **Not bound to any framework** — native Python / C++ / Rust frontends use the same DataNode abstraction

Parsing formats (CSV, Parquet, MsgPack, Arrow, custom) are plugins; each is a ~200-LoC `Parser` that knows how to yield `(bytes, metadata)` from a shard byte stream. Users can register custom parsers.

### 11.6 Inference sessions and paged attention

Variable-context LLM inference imposes a distinct execution pattern from training: model weights are fixed, the KV cache is state, and each request grows from initial prefill to many decode steps while the fleet serves other requests concurrently. Crucible exposes this via a first-class session object.

**`cr.InferenceSession`.** Explicit handle carrying fixed model weights, per-session KV cache, current token position, sampling state. Methods: `session.prefill(tokens) → batch_logits`, `session.generate(max_new, temperature, top_p) → token_stream`, `session.extend(tokens)`, `session.fork() → session'` (branching for beam search or speculative decoding), `session.close()` (releases pool pages).

**Prefill vs decode.** Prefill is compute-bound; decode is memory-bandwidth-bound. Crucible compiles them as distinct ExecutionPlans:

- **Prefill plan**: `PAGED_ATTN` kernel (or `RAGGED_ATTN` for variable-length batches) with Q,K,V compute in parallel. Tile shapes MAP-Elites-tuned for `seq_len ∈ [one_bucket]`, batch size bucketed separately. Typical 50-500ms for 1-10K new tokens.
- **Decode plan**: single-Q attention against the full KV cache via `PAGED_ATTN` with a page-table read. Typical 5-20ms per token on memory-bandwidth-bound hardware, dropping to 1-5ms on batch sizes >4.

The session transparently routes to the prefill or decode plan based on the number of tokens supplied per call.

**Paged KV cache.** The `PAGED_ATTN` kernel reads K,V through a page table: logical token position → physical page. Pages are 16 tokens × N heads × head_dim × dtype ≈ 64-256KB each. A `PagedKVCache` runtime object per session owns a pool of pages, grows on demand, frees on `close()`. Pages may be distributed across multiple GPUs or swapped to host/NVMe when the session exceeds single-device HBM. Representative capacity: 200K-token context on Llama-70B at FP16 ≈ 262GB of KV cache; paged across 4 H100s + host staging.

The pool allocator for KV pages is a Crucible-wide service. Cross-session page sharing (identical system prompts, prefix caching) uses content-addressing on page contents; two sessions with identical prefill content share physical pages via reference counting.

**Continuous batching.** The Keeper maintains a pool of active sessions. At each decode step it assembles a batch from ready sessions (those whose last token has been processed and next token is requested), composes a `batch_size × cache_depth` dispatch, executes, distributes results back to sessions. Batch composition drifts per step; a `BranchNode` on `(batch_size_bucket, cache_depth_bucket)` dispatches to the appropriate cached ExecutionPlan. Typical cache: 24 compiled plans (4 batch buckets × 6 cache buckets), ~12MB of L3 entries, covers >99% of serving traffic.

**Variable-length per batch.** When a decode batch contains sessions with differing KV cache lengths, the kernel uses `cu_seqlens` metadata (cumulative cache lengths) and walks each session's page table independently within the same kernel launch. One `PAGED_ATTN` kernel per (head_dim, dtype, causal) triple covers any cache-length distribution; no per-length recompile.

**Speculative decoding.** A small draft model emits N candidate tokens; the target model verifies in parallel with batch_size=N. Acceptance computed in the same kernel via `accept_reject` extension point. On reject, the session rewinds to the accepted prefix; on accept, the session advances by the accepted-prefix length plus one bonus token. Draft and target are separate `InferenceSession`s sharing the pool allocator.

### 11.7 Dynamic shapes in practice

Shape variation is a first-class concern, not a fallback. Three mechanisms compose: bucketed specialization (cache many concrete kernels), parametric kernels (one compile covers a range with small runtime tax), and online bucket learning (observe hit/miss patterns, sub-specialize hot narrow ranges).

**Bucketed specialization.** A symbolic dimension with range `[lo, hi]` in `SymbolTable` partitions into log-spaced buckets at Phase F.4:

```
seq_len ∈ [1, 524288]  →  buckets:
  [1, 64], [65, 512], [513, 2048], [2049, 8192],
  [8193, 32768], [32769, 131072], [131073, 524288]
```

Each bucket has its own compiled kernel with concrete tile dims tuned for the midpoint of the range. A GEMM with `(M ∈ 8 buckets, N ∈ 8 buckets, K fixed)` produces ≤64 specialized kernels, ~300KB each, ~20MB total in L3. Runtime dispatch: `bucket_id = log_floor(current_shape)`; lookup is O(1).

**Parametric kernels.** For the long tail of novel buckets (or when the user declares a kernel `parametric=True`), Forge emits a kernel that takes shape as a runtime argument:

```cpp
__kernel gemm_parametric(
    fp16* A, fp16* B, fp32* D,
    i32 M_runtime, i32 N_runtime, i32 K_runtime,       // runtime shape
    i32 tile_m, i32 tile_n, i32 tile_k,                 // runtime tile choice
    ...);
```

Per-op overhead: ~1-3% from shape-dependent arithmetic and bounds checks in the outer loop. Used for the 1% of shapes that don't hit any bucket, while the background thread compiles a fresh bucket entry.

**Three-case performance model.** Table summarizing the steady-state cost:

| Shape pattern | Behavior | Perf tax |
|---|---|---|
| Concrete static shape | Direct L3 hit | 0% |
| Symbolic dim within pre-seeded bucket | Specialized-kernel L3 hit | 0% |
| Symbolic dim in a novel bucket | Parametric fallback + background compile | 1-3% for ~200ms, then 0% |
| Symbolic dim outside declared bounds | `cr.ShapeOutOfRangeError` | N/A — user declared bounds too tight |

99% of production workloads hit case 1 or case 2. The parametric fallback eliminates Inductor-style graph breaks; there is no "recompile stall" in the hot path.

**Online bucket learning.** Augur tracks per-bucket hit counts plus the distribution of actual shape values within each bucket. If an existing bucket (e.g., `[8193, 32768]`, tuned for ~16K) consistently sees traffic in a narrow sub-range (e.g., always 8200-8400), Augur triggers sub-specialization: compile a new bucket `[8193, 8400]` in the background, insert into the dispatch table, measure hit rate. Over hours, the bucket set evolves to match the workload's actual shape distribution. Sub-specialization is write-once — the parent bucket stays, the sub-bucket simply preempts it when its range matches.

**Memory planning with bounds.** Phase G uses `hi` of each symbolic dim's range as the slot size upper bound. Wastes memory proportional to `hi / actual`, but eliminates Inductor's "couldn't plan because couldn't bound" failure mode. For fully-dynamic slots with no declared bounds, the user must supply `cr.Session.max_batch_size`, `max_seq_len`, `max_context_length` at session construction; omission is a compile-time error.

**Checkpoint-replay determinism with bucket drift.** If the online bucket set changes mid-training (sub-specialization occurs at step 10K), replay from an earlier step (step 8K) uses the *historical* bucket set for steps 8001-10000, not the current one. The bucket set at each step is part of the ExecutionPlan and is persisted in Cipher; replay loads the historical plan. Same graph + same seed + same plan → same results.

### 11.8 Variable-length batching within training

Training sequences have heterogeneous lengths (512 to 8192 in the same batch). Four strategies; the `RAGGED_ATTN` KernelKind with THD layout is Crucible's default.

| Strategy | FLOPs efficiency | Complexity |
|---|---|---|
| Pad to max length | 40-60% (waste on short sequences) | simplest, single kernel |
| Length bucketing | 70-85% | sort + split sub-batches, per-bucket compile |
| Packing (concat with attention mask) | 90-95% | mask construction, explicit cross-sequence boundaries |
| Ragged / THD layout with `cu_seqlens` | 95-99% | one kernel, metadata tensor, no padding |

The `cr.dataloader` with `variable_length=True` produces THD layout directly. `cu_seqlens[i]` gives the cumulative position of sequence `i`'s start. The `RAGGED_ATTN` kernel iterates per-sequence attention without padding. One compiled kernel per `(head_dim, dtype, causal)` triple; no per-sequence-distribution recompile.

### 11.9 ExecutionPlan — the execution primitive

The ExecutionPlan from Forge Phase J (FORGE.md §15, §18.8) is not a capture-mode afterthought; it is the primary object the runtime submits to the device. User code never invokes `cuLaunchKernel` or its per-vendor equivalent. Every training step, every inference decode, every data-advance boundary is a Plan — or a chain of Plans — loaded from Cipher, submitted via one doorbell write, replayed byte-deterministically until invalidated by a guard failure, a shape bucket miss, or an explicit `plan.patch()` that renames it by content hash.

This section specifies four properties that together distinguish Crucible's Plan from CUDA Graphs and from every megakernel framework published to date: patch-point mutability, semaphore-driven chaining, GPU-side control flow via pushbuffer jumps, and replay determinism by construction.

#### 11.9.1 Plan struct

Authoritative definition lives in FORGE.md §15 and §18.8; this is the runtime view.

```cpp
struct ExecutionPlan {
    // Byte-level state (per-vendor command stream)
    MemoryPlan                       memory;
    std::span<const uint8_t>         pushbuffer_bytes;       // pre-composed per-vendor
    ContentHash                      pushbuffer_hash;
    std::span<const uint8_t>         gpfifo_entries;          // NV/AM only
    std::span<const uint8_t>         constbank_arena;         // c[0] arena, slot-addressable

    // Compiled kernels
    std::span<const CompiledKernel*> kernels;

    // Launch orchestration
    std::span<const LaunchRec>       launches;
    std::span<const BarrierRec>      barriers;

    // Patches + guards
    std::span<const PatchPoint>      patch_points;
    std::span<const Guard>           guards;

    // Shape bucket key
    uint32_t                         bucket_id;

    // Chain edges
    std::span<const ChainEdge>       chain_in;
    std::span<const ChainEdge>       chain_out;

    // Hash identity + caps binding
    ContentHash                      plan_hash;
    const TargetCaps*                caps;

    // Replay metadata
    RecipeHash                       recipe_hash;
    uint32_t                         compile_version;
    uint64_t                         replay_seed;
};
```

Each field is arena-allocated. `pushbuffer_bytes` is typically 1-100 KiB per Plan (one transformer step). `gpfifo_entries` is 8 B per launch record. The Plan itself is a Cipher entry (L3 tier) content-addressed by

```
plan_hash = merkle(pushbuffer_hash, memory.hash, kernels.hashes, barriers.hashes,
                    patch_points.hashes, guards.hashes, chain_edges.hashes,
                    recipe_hash, compile_version)
```

Submission is one function call:

```cpp
namespace crucible::runtime {
    Future<StepResult> submit_plan(PlanId id, std::span<const PatchValue> patches = {});
}
```

`submit_plan` is O(n_patches + 1 doorbell write) — typically sub-μs CPU cost. See §14.7 for the full latency budget.

#### 11.9.2 PatchPoint taxonomy

Eight kinds cover every runtime-mutable value in a Plan. Detailed per-kind semantics + emission discipline live in FORGE.md §18.8; summary here for the runtime view.

| Kind | Width | Runtime use |
|---|---|---|
| `SCALAR` | 1-8 B | Hyperparameters (`lr`, `beta1`, `dropout_p`), per-op constants in c[0] |
| `SLOT_PTR` | 8 B | Absolute VRAM pointer for a memory-plan slot; patched once at plan load via `runtime::resolve_plan(pool_base)` |
| `SHAPE_DIM` | 4 B | Runtime shape dim for a parametric kernel (FORGE.md §F.4) |
| `RNG_COUNTER` | 8 B | Philox counter base (`seed + step_idx * 2^32`) |
| `CONDITIONAL` | 4 B | BranchNode selection predicate, patched by a producer kernel or host |
| `COUNTER_BUMP` | 4 B | LoopNode iteration counter |
| `SEMAPHORE_VALUE` | 4-8 B | ChainEdge threshold; patched each step to advance the epoch |
| `EVENT_TENSOR` | N × (4 or 8) B | Multi-dim counter array for data-dependent dependencies (Event Tensor pattern; MoE routing, speculative acceptance) |

Runtime patching is a typed API:

```cpp
plan.patch("learning_rate", 1e-4f);                  // SCALAR f32
plan.patch("seq_len", 4096);                         // SHAPE_DIM u32
plan.patch("seed", step_index);                      // RNG_COUNTER u64
plan.patch("expert_routing", routing_tensor_view);   // EVENT_TENSOR
```

Implementation: locate PatchPoint by name in the plan's hash-indexed directory; write `width_bytes` at `pushbuffer_offset` (or for EVENT_TENSOR at strided offsets). Under SFENCE fence on the pushbuffer region, the write takes effect on the next doorbell submission. No recomposition, no recompile. Width mismatch is a runtime error (`CruciblePatchError`).

**Patched plan identity**. A patched Plan is a new content-addressed entity:

```
plan_hash_patched = hash(plan_hash_base, patch_manifest.hash)
patch_manifest.hash = hash((pp.hash, pp_value_bytes) for pp in patches)
```

Cipher stores both the pre-patch and post-patch plans; re-submitting `(base_plan, patches)` hits cache at ~30ns lookup. For training loops where only `RNG_COUNTER` and `SEMAPHORE_VALUE` change per step, the effective cache miss rate is zero after first submission; the same compiled pushbuffer is reused every step with different scalar bytes at patch offsets.

#### 11.9.3 ChainEdge — semaphore-driven plan sequencing

Plans compose without host round-trips via pinned-memory semaphores. A training step's optimizer-update Plan waits for the backward Plan's completion; a data-advance Plan signals new batches are ready for the next forward.

```cpp
struct ChainEdge {
    SemaphoreId  sem;                     // handle into per-Keeper semaphore pool
    uint64_t     expected_value;          // WAIT_IN: acquire when sem >= expected
    uint64_t     signal_value;            // SIGNAL_OUT: release sem = signal_value
    ChainKind    kind;                    // WAIT_IN / SIGNAL_OUT / WAIT_THEN_SIGNAL
};
```

**Semaphore pool**. Allocated at Keeper init (§14.6) from pinned sysmem mapped via BAR1 (NV) or equivalent. Pool size is `2 × max_chain_depth × num_concurrent_plans`; typically 64-256 entries per Keeper, ~2-8 KB pinned memory. Each slot is 8 B.

```cpp
struct SemaphorePool {
    std::span<uint64_t>         slots;               // pinned sysmem, 8B each
    std::span<SemaphoreMeta>    metadata;             // owner tag, refcount, last_committed_value
    BitVector                   free_mask;            // lock-free CAS alloc
};
```

Pool metadata is Raft-committed; Cipher persists last-committed value per slot so chain continuity survives process restart.

**Lifecycle phases**:

- **Compile time**: Forge Phase J (§J.5) acquires semaphores via `SemaphorePool::acquire(plan_hash)`; SemaphoreIds embed into the Plan's chain_in/chain_out spans; Phase J emits vendor-specific acquire/release pushbuffer instructions at the correct byte offsets.
- **Steady state**: Per-semaphore value advances monotonically (epoch counter). 2^64 overflow = ~500K years at 1μs/step; not a practical concern.
- **Reclamation**: When all Plans referencing a semaphore are evicted from Cipher, `SemaphorePool::release` returns the slot. Ref-counting is Raft-committed so a semaphore cannot be reclaimed while any replica still references it.
- **Recovery**: On mid-chain failure, Raft commits new membership; surviving Keepers restore semaphore values from the last Cipher checkpoint; replay continues from `sem_step = T_0` per §10.7.

**Wait semantics**. `expected_value` uses acquire-or-greater (`ACQ_CIRC_GEQ` on NV, `WAIT_MEM_ZERO` on AMD, `WAIT_FOR_EVENT` on TPU); late-delivered signals are never missed. Multi-wait (Plan waits on multiple sems) chains ChainEdge entries with implicit AND; OR semantics require explicit `COUNTER_BUMP` or `CONDITIONAL` PatchPoints.

**Canonical training-step chain**:

```
Plan 1 forward         chain_in=[]                  chain_out=[sem_step=N]
Plan 2 backward        chain_in=[sem_step>=N]       chain_out=[sem_grad=N]
Plan 3 allreduce       chain_in=[sem_grad>=N]       chain_out=[sem_reduced=N]
Plan 4 optimizer       chain_in=[sem_reduced>=N]    chain_out=[sem_weight=N+1]
Plan 5 dataload        chain_in=[]                  chain_out=[sem_data=N+1]
Plan 6 forward_N+1     chain_in=[sem_weight>=N+1,
                                 sem_data>=N+1]      chain_out=[sem_step=N+1]
```

All six plans submit concurrently at epoch start. Device sequences via semaphores; host CPU observes only the terminal `sem_step` advance. Per-step progression: ~0 CPU time.

#### 11.9.4 GPU-side control flow via pushbuffer jumps

`LoopNode` and `BranchNode` in the Merkle DAG (CLAUDE.md L7) compile to **device-executed control flow**: the GPU's host engine walks the pushbuffer, evaluates predicates in GPU-visible memory, and jumps within the pushbuffer without host involvement.

**LoopNode compiled shape (NV Hopper+)**:

```
pushbuffer (conceptual):
    [pre_loop setup]
  loop_top:
    [per-iteration kernel launches]
    [micro-kernel: decrement counter, evaluate predicate, write condition reg]
    [GPFIFO conditional jump: if cond BRA loop_top else fallthrough]
  loop_end:
    [post_loop]
```

The jump is a GPFIFO entry with SYNC=0 pointing back at a prior pushbuffer offset. Hopper's host engine supports bounded GPFIFO jumps natively. The counter micro-kernel is ~8 SASS instructions (~200ns per iteration); predicate read is one cache-line load from pinned memory.

**BranchNode compiled shape**:

```
pushbuffer (conceptual):
    [pre_branch]
    [predicate read: LOAD c[0][branch_predicate] → R0]
    [conditional skip to arm_b or fall through to arm_a]
  arm_a:
    [kernel launches for true branch]
    [unconditional jump to end]
  arm_b:
    [kernel launches for false branch]
  end:
    [post_branch]
```

Both arms are compiled and present in pushbuffer; predicate selects which to skip. Memory plan accounts for both arms' peak slot usage. Each arm is independently content-addressed, so BranchNode arms are cache-shareable across Plans with identical arms.

**Per-iteration cost**: ~200-500ns for the counter/predicate micro-kernel plus ~100ns GPFIFO jump. A looped transformer layer (LoopNode REPEAT(N=32)) over a persistent kernel runs at ~2% overhead vs unrolled; for N≥8 LoopNode wins on icache footprint and compile time.

**Interaction with BITEXACT**: loop counters and branch predicates are deterministic given the Plan's `replay_seed`. Under `BITEXACT_TC/STRICT`, LoopNode iteration count is bit-identical across replays.

#### 11.9.5 Replay determinism as plan invariant

A Plan's execution at step T is a pure function of:
- `plan_hash` (structural identity)
- Current `PatchPoint` values (concrete runtime state)
- Input tensor bytes (via SlotRef resolution against `memory.pool_base + slot.offset`)
- `replay_seed` (Philox counter input)
- `recipe_hash` (numeric contract)

Under `determinism ≥ BITEXACT_TC` recipes, two submissions of the same `(plan, patches, inputs, seed)` tuple produce byte-identical device writes. The CI invariant `bit_exact_replay` (§10.8) subsumes this: if a Plan violates it, either hidden state has leaked or a PatchPoint has been missed. Debug via `crucible plan diff` (§16.6).

**Guard-failure path**. If a Plan's guards fail (schema hash mismatch, shape out of bucket), the runtime raises `CruciblePlanDivergence` carrying the divergence point. The Keeper falls back to reference-eager for the current iteration and background-compiles a new Plan for the divergent state. Both old and new Plans remain in Cipher, addressable by their plan_hashes.

**Plan invalidation**. A Plan becomes invalid when:
- Its L3 kernel bytes are evicted or recompiled due to calibration drift (§13.3)
- Its memory_plan references a slot whose type changed
- Its recipe_hash falls out of the fleet-intersection (§7.3)
- Its `compile_version` predates the current Forge+Mimic version

Invalidation marks the Plan dead in Cipher (content-addressed — no in-place mutation); the next user-visible submission triggers a fresh compile.

#### 11.9.6 Plan versioning and federation

Plans are federation-safe at Cipher L1 but per-chip at L2/L3:

| Cache level | Plan artifact | Cross-vendor shareable? |
|---|---|---|
| L1 | IR002 snapshot pre-Phase J | **yes** — no hardware bits |
| L2 | IR003* snapshot per-vendor | same vendor family only |
| L3 | Compiled pushbuffer bytes | exact chip family + suffix |

Two installations running the same model produce byte-identical L1 plans; federation goldmine per §9.5. Chip-specific recompilation to pushbuffer is background work amortized against the first submission on that chip.

`compile_version` is the tuple `(forge_version, mimic_vendor_version, recipe_registry_epoch)`. Any component bump invalidates L2/L3 but L1 survives. Plan hash carries the tuple; Cipher checkpoint at §9.4 pins versions across runs.

---

## 12. Distribution at runtime — live reshard, elastic membership

Forge's Phase K computes a static 5D partition at compile time (FORGE.md §25). At runtime, that partition is an input to execution, but Canopy can also live-reshard when membership changes. Here's the coordination.

### 12.1 Partition lifecycle

```
Fleet boots                                  initial partition committed
        │                                     at epoch 0
        ▼
Training begins, step 0 starts
        │
        ▼
Step executes; gradient collectives per partition
        │
        ▼
Every M steps, Augur reviews topology measurements
        │
        ▼
If measurements changed significantly → recompute partition
        │                                     (Phase K runs on updated Meridian data)
        │
        ▼
If new partition differs: Raft commits at iteration boundary
        │
        ▼
Next step uses new partition; old state redistributed via RDMA
```

### 12.2 Membership changes

**Node joins (up-scale)**:

1. New Keeper completes startup, advertises chip caps + native recipes
2. SWIM propagates its existence; Raft votes on admission
3. Meridian probes latency/bandwidth to new peer
4. At next iteration boundary:
   - Phase K re-runs on expanded membership
   - If partition structure changed, weight shards redistribute (e.g., TP=4 → TP=8 means each GPU sends half its weights to a new peer)
   - If no structural change, new peer becomes a spare; next death absorbs it

**Node leaves (graceful shutdown)**:

1. Keeper announces `leaving` via gossip
2. Finishes current step if one is in progress
3. Transfers unique state (hot-tier Cipher entries that don't exist on warm/cold) to a surviving peer
4. Raft commits new membership; any live endpoint handoff is typed as an
   `EpochedDelegate` at the committed epoch/generation, not as an unversioned
   `Delegate`
5. Reshard if needed

**Node dies (unexpected)**:

1. SWIM declares `confirmed-dead` (~5s detection)
2. Raft commits new membership
3. Roll back to last checkpoint, reshard to surviving members, replay (§10.7)

### 12.3 Mixed-vendor coordination

Mixed NV + AM + TPU + TRN fleet. Partition respects vendor boundaries by default: TP groups only span same-vendor peers (to avoid cross-vendor RDMA penalty), DP groups can span vendors (gradient all-reduce across vendors works fine under BITEXACT recipe).

Configurable per-workload:

```yaml
partition_policy:
  tp_scope: same_vendor    # only form TP within a vendor
  pp_scope: same_vendor    # same
  dp_scope: any            # DP can mix vendors
  ep_scope: any            # MoE experts can mix
  cp_scope: same_chip      # context parallel within same chip for low latency
```

### 12.4 Live reshard cost

Dominant cost: weight redistribution. On partition change from TP=8 to TP=4 across 8 GPUs:

- Each GPU that's gaining TP rank receives ~2× its previous weight share from departing peers
- Transfer happens via RDMA at wire speed
- 70B model shards at BF16 ≈ 17.5GB per old shard × 4 transfers × N GPUs
- On 400 Gb/s RoCE = ~350ms per 17.5GB transfer, parallel = ~350ms total

So 350ms of downtime for a large live reshard of a 70B model. Negligible against training duration. Comparable to PyTorch's NCCL world-reinit which takes minutes — except ours happens automatically, Raft-committed, without user intervention.

### 12.5 What runtime reshard does NOT handle

- **Model architecture change mid-run** — that's a recompile, not a reshard. Forge re-runs Phase A-L on the new graph; may invalidate L2/L3 cache.
- **Precision change mid-run** — recipe change requires recompile.
- **Cross-vendor recipe intersection empty** — fleet can't form a consistent partition; Keeper escalates with a `policy_violation` event to Augur; user decides (STRICT refuses, ADAPT degrades).

### 12.6 Pipeline scheduling — 1F1B, interleaved, zero-bubble

Pipeline parallelism splits layers across devices; micro-batches flow through. The scheduling pattern determines how much of the pipeline bubble (idle time at start/end) can be eliminated. Crucible emits the schedule as part of the ExecutionPlan; per-stage Keepers execute their assigned micro-batches in pinned order.

**Three patterns, all compiled.** Forge Phase I.LaunchOrder emits one of the three based on a `partition.pipeline_schedule` attribute:

- **GPipe 1F1B (one forward, one backward)**: each stage processes one forward micro-batch, then one backward, alternating. Bubble overhead ≈ `(num_stages - 1) / num_microbatches`. For 16 stages × 64 micro-batches: ~23% bubble.
- **Interleaved 1F1B (Megatron)**: each stage owns multiple virtual stages; micro-batches cycle through virtual stages before advancing to the next physical stage. Bubble drops to ~12% for the same configuration.
- **Zero-bubble 1F1B (DeepSeek V3 scheduling, Qi et al. 2024)**: backward split into "activation gradient" (B) and "weight gradient" (W) halves, with W scheduled to fill forward bubbles. Bubble drops to <2% on well-sized configurations.

**What it costs to implement.** The compile-time work is Phase I.LaunchOrder emitting the per-stage micro-batch sequence. The runtime work is per-stage Keepers iterating their assigned sequence with pinned order — a captured launch sequence, replayed each epoch. No runtime scheduling decisions. Each stage's assigned sequence is content-addressed; identical model + partition + schedule → same compiled plan.

**Memory coordination.** Zero-bubble requires holding more activations in flight (the B half runs later than the full backward would). Phase G.MEMPLAN's `activation_memory_bound` attribute receives a `schedule_aware=True` flag that inflates the activation-storage reservation by the schedule's in-flight factor (typically 1.2-1.5×). If the budget can't be met, Phase K demotes to interleaved 1F1B (less bubble reduction but lower memory) and marks the ExecutionPlan accordingly.

**Backward-pass splitting.** The schedule's B/W split requires Forge Phase I.2 to split backward kernels into "inputs gradient" and "weights gradient" sub-kernels with distinct dispatches. Achievable for all IR002 kernel kinds via the backward-body extension point (§3.8); weight-gradient computation typically has no data dependency on subsequent forward micro-batches, which is what enables scheduling it later.

### 12.7 Bucketed async all-reduce

Aggregate gradient traffic is bandwidth-bound; hiding it behind backward computation is the single largest lever for DP-heavy workloads. FSDP's approach — bucket gradients into 25-50MB units, fire all-reduce per bucket as soon as its gradients are produced, overlap with subsequent backward layers — is the default pattern Crucible emits.

**Implementation.** Phase K.InsertCollectives emits `CollectiveAttrs` not as one all-reduce per step but as one per bucket. Each bucket's collective depends on its contributing gradients being produced but not on later buckets. Phase I schedules them asynchronously on a dedicated "collective stream" that overlaps with the compute stream:

```
Backward layer N:  [compute gradients] ──┐
                                          └─→ Bucket K ready
                                                  │
Backward layer N-1: [compute gradients] ──┐       ├─ all_reduce(K) fires async
                                          └─→ ... │
                                                  ▼
                                          Bucket K completes, written back
```

Typical hidden fraction: 70-90% of all-reduce latency on well-sized buckets. Bucket size is a recipe parameter (default 25MB), auto-tuned by Augur per model: smaller for latency-bound small-message collectives, larger for bandwidth-bound.

**Bucket-level content addressing.** Each bucket's `CollectiveAttrs` is content-hashed by `(bucket_size, reduce_op, dtype, member_set, recipe.determinism)`. Two models with identical bucket schedules share compiled collective kernels via L2.

**Concurrent-collective support per vendor.** CNTP and Mimic `comm::` must support multiple in-flight collectives on independent peers. NV: MSCCL-style multi-ring with separate streams. AM: XGMI supports multiple concurrent reductions. TPU: ICI supports multiple concurrent aggregations with care on torus ordering. Trainium: NeuronLink supports multiple NCC groups. Backend-specific details are in MIMIC.md §37.

### 12.8 Expert parallelism runtime

MoE layers route each token to K experts. With N experts distributed across G GPUs (EP degree G), routing requires an all-to-all per expert dispatch and a reverse all-to-all per expert combine. Naïve implementation bottlenecks on the all-to-all; careful implementation approaches 60% MFU.

**The routing flow.**

1. Gate logits → top-K routing decisions per token (`MOE_ROUTE` kernel)
2. Permute tokens by destination expert's owner GPU (sort-by-key)
3. All-to-all: send each GPU's tokens to their experts' owners
4. Grouped GEMM: per-expert compute on the received batch (Megablocks / Marlin pattern)
5. Reverse all-to-all: send results back to the token's owner GPU
6. Reverse permute
7. Combine K expert outputs per token

**Hot-expert handling.** Per-expert token count is imbalanced; some experts get 2-5× more tokens than others. Two mitigations:

- **Capacity factor.** A `capacity_factor` (default 1.25) bounds per-expert tokens per batch. Tokens exceeding capacity are dropped with zero gradient; the gate is penalized via an auxiliary loss. Declared in the routing recipe.
- **Expert migration.** Augur tracks per-expert assignment rates over 1000-step windows. If expert hotness diverges >2× from uniform, Canopy proposes a remap (rotate hot experts to GPUs with surplus capacity) at the next epoch boundary. Raft commits; permutation tables update.

**Grouped GEMM realization.** Per-vendor backends realize the grouped GEMM as their native equivalent: NV uses Marlin-style grouped launch (one kernel launch, N tile dispatches, per-expert weight pointers), AM uses MFMA grouped dispatch, TPU uses batched MXU, Trainium uses per-expert NeuronCore assignment.

### 12.9 Sequence / context parallelism — ring attention

Long-context training (>16K tokens) saturates tensor parallelism before it saturates the GPU. Sequence parallelism splits the sequence dimension; context parallelism extends this across nodes. Ring attention (Liu et al. 2023) is the canonical pattern.

**The ring.** Each GPU holds a slice of Q; K and V rotate around a ring through all GPUs. At each rotation step, each GPU computes `softmax(Q_local @ K_visiting^T) V_visiting` and accumulates into its output (with online softmax rescaling). N-way parallelism in O(N) rotation steps; per-step communication is `shard_bytes`, compute is `shard_bytes × Q_local`. Balanced if `compute_per_step ≈ comm_per_step`.

**Implementation.** `ATTENTION` KernelKind with `algorithm=RING` attribute. Phase K emits a rotating `CollectiveAttrs` (effectively K/V send to next peer, recv from prev peer) interleaved with per-rotation attention kernels. Double-buffered K/V slots in the memory plan so communication and computation overlap. Striped attention (a variant that interleaves Q and K/V rotations) is a different `algorithm` variant.

**Correctness under BITEXACT.** Ring attention's online-softmax accumulation is sensitive to rotation order. Under `BITEXACT_TC` recipes, the ring order is pinned by canonical UUID sort; under `BITEXACT_STRICT`, the reduction is serialized to a pinned tree (slower but byte-identical). Declared per-recipe.

### 12.10 In-flight collective failure semantics

A node dying mid-collective is a load-bearing failure mode. The replay-determinism guarantee requires that failure rolls back cleanly to the last checkpoint; the collective layer's job is to detect failure promptly and signal rollback.

**Per-collective timeout.** Every `comm::all_reduce`, `comm::all_to_all`, `comm::reduce_scatter`, etc. takes a `timeout_ms` (default 5000). If the completion future doesn't resolve within the timeout, the caller:

1. Calls `canopy::signal_suspected_dead(peer)` for each unresponsive peer
2. Waits on Raft's membership-commit future (bounded by Raft timeout, ~2s)
3. On new membership commit: the collective is aborted; calling code raises `CollectiveAbortedError`
4. The running ExecutionPlan is invalidated; replay kicks in from the last Cipher checkpoint

**Raft integration.** The collective library does not commit membership changes itself; it signals suspicion via Canopy gossip. Raft's leader (whichever Keeper holds the current term) collects suspicion signals, cross-validates with heartbeats, commits the new membership at epoch E+1. Every Keeper observes the epoch bump and aborts in-flight collectives tied to the old epoch.

**Mid-step recovery cost.** Failure at step T with last checkpoint at T₀ = T - N: rollback cost ≈ N steps of recompute. With N=500 and 1s/step, ~500s. With aggressive N=100, ~100s. Better than "single-node failure kills the job, requeue from scratch."

**Partial-result retention.** A collective that completed 7/10 rings before a failure: the partial results are discarded. Retrying with a degraded topology (9 peers instead of 10) would produce bit-different results under BITEXACT recipes (different tree shape). The design choice is correctness over marginal perf — always roll back to checkpoint, never salvage partial collective output.

### 12.11 Gradient norm and clipping

Global gradient norm for clipping requires `sqrt(Σ ‖g_i‖²)` across all parameters. The naïve implementation (per-GPU local-square-sum + all-reduce + sqrt + broadcast of `scale = 1.0 / max(1.0, norm / threshold)`) introduces two determinism concerns:

1. **Summation order across parameters.** Under BITEXACT recipes, the per-parameter square-sums must be reduced in canonical order. The recipe pins the reduction as pairwise tree sorted by parameter-ID hash.
2. **Broadcast path determinism.** The scale scalar is computed on one peer and broadcast; under a non-deterministic broadcast path (different tree shape per run), floating-point `1.0 / max(...)` rounding could diverge if the computed norm itself has any ULP slack. Solution: canonical scalar reduction + broadcast from UUID-sorted root.

Both are handled by a dedicated `GlobalNormRecipe` component within the NumericalRecipe; Phase K.InsertCollectives emits a bespoke `ALL_REDUCE_SCALAR` + `BROADCAST` pair with pinned order.

### 12.12 Distributed checkpoint with chunk-level deduplication

Per §9.4, a 70B-model checkpoint writes ~17.5GB per Keeper. Write amplification at fleet scale (1024 nodes × 17.5GB = 18TB per checkpoint) is substantial. Chunk-level content-addressing reduces this ~10-100× because most weights change slowly across checkpoints.

**Algorithm.** Each Keeper's weight shard is split into 64KB chunks; each chunk's SHA-256 content hash becomes its Cipher key. Checkpoint write:

1. Each Keeper computes content hashes for all its chunks
2. Keeper queries Cipher: which of its chunks already exist?
3. Keeper writes only missing chunks; writes a *manifest* (list of `(chunk_index, chunk_hash)`) to Cipher
4. Raft commits the manifest as the canonical checkpoint entry

Recovery reads the manifest; fetches chunks (from hot tier on surviving peers where possible, fallback to cold tier). Across N=500-step checkpoint intervals, typical chunk reuse is 95-99% for most weights, reducing per-checkpoint write amplification to ~1/20.

**Cross-Keeper chunk sharing.** If Keeper A and Keeper B hold overlapping shards (RAID α > 0), they compute identical chunks for the overlap. Content-addressing naturally deduplicates at the Cipher level; only one write per unique chunk regardless of which Keeper wrote it first.

### 12.13 DataLoader shard migration on membership change

When a CPU Relay leaves (graceful or abrupt), its assigned shards redistribute via consistent hashing. Active reads on the departing shard require a handoff:

1. Departing Relay announces `leaving`; stops accepting new prefetch requests
2. In-flight batches complete; decoded batches in the prefetch queue are drained to consumers
3. Cursor state (`shard_uri, byte_offset, shuffle_epoch`) for each active shard is handed off to the assigned-successor Relay via CNTP send
4. Successor resumes from the exact byte offset
5. Departing Relay exits

**Abrupt failure.** Cursor state is lost; recovery rolls back to the last Cipher checkpoint (§11.3). The departing Relay's assigned shards redistribute; new owners load cursors from checkpoint; data order from checkpoint onward is deterministic.

### 12.14 EMA / SWA weight averaging across the fleet

Training frequently maintains an exponential moving average (EMA) or stochastic weight averaging (SWA) copy of weights for better inference quality. Crucible exposes:

```python
trainer = cr.Trainer(model, ema_decay=0.9999, swa_start=None)
```

Per step, EMA weights update as `ema_w = β · ema_w + (1-β) · w`. Placement:

- **Replicated**: every DP replica holds a full EMA copy. Simple, redundant, high memory.
- **Sharded** (default): EMA shards mirror weight shards (same FSDP-style partition). Updated locally per step; no cross-replica communication for EMA updates.

Checkpointing: `TrainingCheckpoint.ema_weights_blob` is persisted alongside the primary weights (§9.4). Reconstruction on recovery: load both, continue from checkpoint.

Cross-fleet EMA averaging (some papers' federated-average pattern): triggered via explicit `trainer.sync_ema(interval_steps=100)`; emits a `CollectiveAttrs` for EMA all-reduce at the specified interval.

---

## 13. Augur — continuous monitoring and drift

Augur is the observer subsystem. Runs continuously in the background. Its two responsibilities: **measure** (what's happening on the hardware) and **decide** (when to trigger recompile / recalibration / reshard).

### 13.1 Sampling

Every ~1% of kernel launches, Augur attaches a `mimic::probe_counters` call. The sampled kernel executes; on completion, Augur:

1. Reads the hardware counters (cycles, stalls, memory traffic, occupancy, per-pipe utilization)
2. Loads the prediction stored in the kernel's `CompiledKernel.predicted_cycles`
3. Computes residual per signal
4. Appends to the regression dataset (per-kernel rolling window, kept in hot Cipher for fast query)

Overhead: ~10ms per sampled launch (counter read + residual compute). At 1% sampling and 100 launches/sec = ~10ms/sec = 1% of wall time. Negligible.

### 13.2 Drift detection

For each kernel × chip, Augur maintains a running P95 of residuals per signal. Drift triggers when:

- P95 cycle residual > 10% for 100+ consecutive samples → recalibrate Mimic's timing model for this chip
- P95 memory-BW residual > 15% → recalibrate memory subsystem parameters
- P95 stall-attribution residual > 20% → investigate scheduler / pipeline model

Recalibration runs during cluster-idle periods. Updates per-chip TargetCaps JSON; invalidates affected L3 cache entries. Next kernel use triggers recompile.

### 13.3 Regression detection

Beyond drift-from-prediction, Augur watches for drift-from-past. Same kernel + same shape + same chip → cycles should be stable within tolerance. If the same kernel suddenly runs 20% slower than its 7-day rolling median:

- Thermal throttling (compare to `nvml` / vendor-equivalent thermal counter)
- Clock-domain degradation (frequency counter dropped)
- Memory health (ECC correctable-error rate increased)
- Firmware change (check vendor driver version)

Each regression has a known cause taxonomy. Augur logs, tags, and emits an event via Canopy gossip. If the event correlates with a Keeper's health signals being poor, Canopy may mark the Keeper degraded; Raft may decide to reshard.

### 13.4 Model intelligence (L9-L11 integration)

Beyond kernel-level monitoring, Augur also tracks training-level signals per Crucible CLAUDE.md's L9-L11:

- **Hessian spectrum** (Lanczos every N steps) — identifies saddle points, sharpness transitions
- **Per-layer gradient SNR** — drives automatic per-layer LR adaptation
- **Effective rank** (randomized SVD) — drives adaptive bottleneck insertion
- **CKA layer similarity** — drives redundant-layer pruning

These are expensive (~seconds per sampling), so cadence is coarser — every 1000 steps. Results feed into L10 Model Evolution for architecture mutation proposals.

### 13.5 Recommendations engine

Augur doesn't decide unilaterally. It generates **recommendations** ranked by `expected_speedup × confidence`. Each tagged:

- `auto-hot`: safe, apply without user review (e.g., invalidate stale L3 cache entry)
- `auto-cold`: apply during idle/maintenance window (e.g., recalibrate Mimic timing)
- `manual`: requires user review (e.g., "consider pruning layer 47, CKA=0.99 with layer 46")

Recommendations persist in Cipher; user can review via `crucible recommendations list`.

### 13.6 Online collective-algorithm learning

Beyond kernel-level drift, Augur samples per-collective timings and maintains a benchmark table indexed by `(op, algorithm, peer_set, msg_size)`. Every ~100 steps, Augur compares observed vs predicted collective time per bucket:

```
record (op=ALLREDUCE, algorithm=RING, peers=8, msg_size=1MB,
        actual_latency=µs, actual_bw=GB/s)

compare to prediction from the benchmark table
if systematic deviation > 10% for 100+ samples:
    update benchmark table
    flag partition for Phase K re-solve
    if Z3 proposes different algorithm: reshard at next iteration boundary
```

This closes the topology-adaptation loop (§12.7, FORGE.md §25.6): the partition solver's cost model stays accurate as network conditions shift. A link degradation that would cost 30% of steady-state throughput triggers a re-solve within minutes, not at the next scheduled recompile.

### 13.7 MFU tracking

Augur reports cluster MFU (`achieved_TFLOPs / peak_TFLOPs`) per iteration, broken down by loss source:

```
Baseline target (peak): 989 TFLOPs BF16 per H100
Achieved:                 658 TFLOPs (66.5% MFU)

Breakdown of the 33.5% loss:
  Lost to comm (not overlapped):         12%  → bucket async all-reduce tuning
  Lost to pipeline bubble:                 2%  → zero-bubble PP active
  Lost to memory BW (attention):          10%  → FP8 mixed precision opportunity
  Lost to small-kernel launch overhead:    3%  → persistent-kernel fusion expansion
  Lost to suboptimal kernels:              4%  → MAP-Elites generation increment
  Lost to suboptimal tile shapes:          2%  → shape-bucket sub-specialization
```

The breakdown drives concrete recommendations: "expand persistent fusion to layer 34" is a specific, actionable proposal with expected MFU gain, not a generic "your kernel is slow."

---

## 14. Keeper daemon lifecycle

The Keeper is the per-Relay daemon. One process per physical (or virtual) node. Hosts everything: dispatch, trace, compile, comm, Cipher tier, data loading (if CPU Relay), or compute execution (if GPU Relay).

### 14.1 Startup sequence

```
1. systemd/k8s/SLURM starts crucible-keeper binary
2. Load node config (chip detection, NIC detection, paths)
3. Initialize per-vendor runtime (Mimic rt::open_device)
4. Allocate Cipher hot-tier RAM arena
5. mmap Cipher warm-tier NVMe (if configured)
6. Initialize CNTP transport (RDMA QPs, AF_XDP sockets, BPF programs)
7. Register RDMA memory regions for all our tensors' pools
8. Compute stable UUID (from TPM, or persist-first-boot random)
9. Start SWIM gossip (join via seed peers or multicast)
10. Receive initial membership state from gossip
11. Join Raft (may be voter or learner depending on cluster size)
12. Advertise native_recipes bitmap, chip caps
13. Meridian probes latency/BW to new peers (may take seconds)
14. Keeper is READY
```

~500ms-2s total, dominated by RDMA setup and Meridian probing.

### 14.2 Idle state

When no work is queued:

- Gossip + Raft heartbeats continue (all in eBPF fast path)
- CPU consumption: <1% of one core (polling a few atomic counters)
- GPU is idle (no kernel launched)
- Cipher tier: LRU eviction continues in background thread
- Augur: samples counters on any rare stray work

### 14.3 Working state

Compute Keeper running training:

- Foreground: receive dispatch calls from frontend, return mock tensors
- Background thread 1: drain TraceRing, build TraceGraph, request Forge compiles
- Background thread 2: Mimic compile worker pool (hardware_concurrency threads)
- Background thread 3: execute compiled kernels via Mimic runtime
- Background thread 4: CNTP completion poller (spins on RDMA CQs + AF_XDP rings)
- Background thread 5: Cipher tier manager (promote / evict)
- Background thread 6: Augur sampler

All threads are shared-nothing except via SPSC rings and acquire/release atomics. Zero locks.

### 14.4 Shutdown

Graceful (SIGTERM):

1. Announce `leaving` via gossip
2. Finish current step if one is in-flight
3. Flush any dirty hot-tier Cipher entries to warm tier
4. Transfer unique hot-tier state to peers (Raft-committed)
5. Close RDMA QPs, unload BPF programs
6. Exit

~1-2s graceful. If interrupted (SIGKILL), the recovery path in §10.7 handles it.

### 14.5 Self-updating

Crucible binaries update via Cipher cold-tier distribution. New binary is content-addressed (`hash(binary_bytes)`); Canopy gossips the hash; Keepers that don't have it pull from cold tier; Raft coordinates rolling update:

1. Half the Keepers update simultaneously (controlled by Raft — never entire cluster at once)
2. Updated Keepers graceful-shutdown, reload new binary, re-join Canopy
3. Raft verifies the new Keepers are healthy
4. Other half updates
5. Training resumes on new binary

Updates during training are possible (with minor step-loss from graceful shutdown). More commonly: update windows during idle periods.

### 14.6 Health monitoring

Keeper health signals emitted to Canopy:

- CPU utilization, memory usage, thermal
- GPU utilization, HBM occupancy, ECC errors
- NIC link quality, RDMA completion rates
- Cipher tier sizes, eviction rates
- Per-op stall counts (from Augur)

Aggregated by Canopy; available via Prometheus scrape endpoint (`:9090/metrics`). Watched by the k8s-operator for autoscaling decisions.

### 14.7 Per-Keeper latency expectations

The numbers in this section are **modeled cost decompositions** for a warm Keeper on a Hopper SXM5 host (Sapphire Rapids or Grace + PCIe Gen5 x16 + InfiniBand NDR 400) — i.e. step-by-step accounting of where time should go based on the underlying hardware and protocol costs. They are NOT promises Crucible delivers a particular nanosecond figure on your hardware. The bench suite reports actual measurements; values marked with ✽ are design targets that require CI-validation against real silicon before being declared achieved. Augur drift detection (§13.3) uses the modeled values as a baseline against which actual measurements are compared, not as a contract on delivery.

#### 14.7.1 Host-side dispatch

**Cold path** — first submission of a novel plan:

| Step | Cost | Basis |
|---|---|---|
| Plan lookup by `plan_hash` | ~30 ns | Swiss-table hash lookup |
| PatchPoint writes (N scalars) | ~10 ns × N | Write-combined store to pushbuffer WC mapping |
| SFENCE | ~15 ns | Flush WC buffer |
| GPPut advance | ~10 ns | 4 B MMIO write to BAR1 |
| SFENCE | ~15 ns | |
| Doorbell write | ~40 ns | 4 B MMIO write to HOPPER_USERMODE_A or BAR0+0x2200 |
| **Total CPU critical path** | **~120-200 ns** ✽ | For plan with ≤ 5 patches |

**Warm path** — plan resubmit with identical patches:

| Step | Cost |
|---|---|
| PatchPoint writes | 0 (unchanged) |
| GPPut advance + doorbell | ~80 ns |
| **Total CPU critical path** | **~80-100 ns** ✽ |

#### 14.7.2 GPU-side dispatch

Doorbell retire → first byte of kernel output:

| Step | Cost |
|---|---|
| PCIe posted-write fly time | ~100-200 ns |
| GPFIFO entry fetch | ~30 ns |
| Pushbuffer fetch + SEND_PCAS | ~200 ns |
| SKED-to-SM dispatch (QMD schedule, grid launch) | ~100-200 ns |
| **Total wire → first-SM-cycle** | **~400-650 ns** ✽ |

#### 14.7.3 Completion detection

Kernel end → CPU-observed:

| Step | Cost |
|---|---|
| Kernel's terminal `SEM_RELEASE` | ~50 ns SASS execution |
| PCIe fly back to pinned sysmem | ~200-400 ns |
| CPU cacheline snoop | ~100-300 ns |
| Spin-poll predicate match | ~10 ns tight loop |
| **Total kernel-end → CPU-observed** | **~400-800 ns** ✽ |

**Full round-trip for a single-kernel plan** (dispatch + kernel execution + completion) = ~1.0-1.5μs for a ~200 ns compute kernel, PCIe-fly-time-dominated. For persistent kernels (LoopNode bodies with internal iteration), dispatch cost amortizes to zero — per-internal-iteration cost is just the counter-bump micro-kernel (~200 ns) + predicate read.

#### 14.7.4 Collective latency

**Intra-node, NVLink P2P, 8 GPUs**:

| Collective | Target | NCCL baseline |
|---|---|---|
| All-reduce 8 B, 8-way ring | ~3-5μs (6 hops × 400ns + 2 SM steps × 300ns) | ~25μs |
| All-reduce 1 KB | ~5-8μs | ~25μs |
| All-reduce 1 MB (BW-bound) | ~25-40μs | ~40μs |
| Broadcast 8 B | ~2μs | ~15μs |

**Cross-node, IB NDR 400**:

| Collective | Target | NCCL baseline |
|---|---|---|
| RTT, 64 B, single RDMA write | ~700-900 ns (IB hw floor + CNTP ~100ns) | n/a (NCCL abstracts) |
| All-reduce 8 B, 64-way ring | ~8-12μs | ~40μs |
| All-reduce 1 MB, 64-way | ~80-120μs (BW) | ~100μs |

Where NCCL numbers come from: our re-baseline of publicly reported numbers plus aikitoria's benchmark of 8×H100 with `NCCL_P2P_LEVEL=SYS` (prior turn's NCCL context).

#### 14.7.5 Initialization budget

| Phase | Target | PyTorch+NCCL baseline |
|---|---|---|
| Keeper startup (RDMA QPs + BAR map + Cipher) | ~500-2000 ms | ncclCommInitRank: 500-1500 ms per rank |
| Canopy join (SWIM + Raft) | ~200-500 ms | n/a (NCCL is fixed-world) |
| CNTP peer handshake (pre-warmed QPs) | ~10-50 ms at 8 nodes | ~1-3 s at 64 nodes |
| Meridian calibration probes | ~5-15 s one-time | n/a |
| Plan compile (cold, novel shape) | ~100-300 ms | torch.compile+NCCL: 1-5 s |
| Plan compile (L1/L2 cache hit) | ~1-10 μs | n/a |
| **Total cold fleet init, 8 H100 nodes** | **~3-5 s** ✽ | ~30-60 s |

#### 14.7.6 Failure recovery

| Event | Cost |
|---|---|
| SWIM `confirmed-dead` detection | ~5 s (1s probe + indirect confirmation) |
| Raft membership commit | ~50-100 ms |
| Cipher checkpoint load (warm NVMe) | ~50-200 ms |
| Plan reshard compute | ~200-500 ms |
| Replay from checkpoint (N=500 steps × 1 s) | ~500 s |
| **Recovery excluding replay** | **~5-7 s** ✽ |
| FLR + GSP re-upload (if needed) | +~1.5-2.5 s (§17.8) |

#### 14.7.7 Steady-state Keeper CPU footprint

On a running training Keeper:

| Thread | Cost |
|---|---|
| Dispatch loop (spin on ack semaphore) | ~1-5% of one core |
| Gossip + Raft fast path (XDP) | <0.5% of one core |
| Cipher tier management | <0.5% of one core |
| Augur sampling at 1% rate | <1% of one core |
| **Total Keeper overhead per node** | **~3-7% of one core** ✽ |

These numbers are the numerical contract. Augur runtime monitoring (§13.3) validates against measurements; sustained drift >10% triggers recalibration or Plan invalidation. The ✽-marked targets require CI-validation against real silicon (Phase 1 of the build plan, §19) before being declared achieved.

### 14.8 NV-specific Keeper init sequence

Concrete instantiation of §14.1 for NVIDIA hybrid backend (nvidia.ko for GSP boot + userspace for hot path, per MIMIC.md §36.4.2). Eleven discrete steps: 1-5 once per host reboot (driven by nvidia.ko + `nvidia-persistenced`), 6-11 once per Keeper process start.

**Host-boot-time (steps 1-5)**:

1. **Load kernel module with module params** — `modprobe nvidia NVreg_RegistryDwords="RMForceStaticBar1=0x1;RMPcieP2PType=0x1;RmWatchDogTimeOut=0;RmRcWatchdog=0" NVreg_DynamicPowerManagement=0x00 NVreg_EnableResizableBar=1 NVreg_EnableMSI=0`. See MIMIC.md §36.6 for the full matrix. Cost: ~500 ms - 1 s.
2. **GSP firmware upload + boot** — nvidia.ko uploads signed GSP-FMC + GSP-RM blobs, programs WPR2, FSP secure-boot handshake. Cost: ~1-1.5 s first time; ~200-400 ms from cached hugepage sysmem on subsequent FLRs.
3. **BAR1 resize** — `nv_resize_pcie_bars()` enlarges BAR1 to cover full VRAM (requires `EnableResizableBar=1` + `pci=realloc`). Cost: ~50 ms; skipped on cached host config.
4. **Static BAR1 mapping** — `kbusEnableStaticBar1Mapping_TU102` installs a 2MB-aligned huge-page identity map in GMMU covering full VRAM. Now `BAR1_VA == FB_physical_offset`. Cost: ~10-30 ms.
5. **`nvidia-persistenced` started** — daemon holds a sentinel fd open across Keeper lifetime so GSP doesn't reboot on last-fd-close. Cost: ~5 ms; one-time per host.

**Keeper-process-time (steps 6-11)**:

6. **Open `/dev/nvidiactl` + `/dev/nvidia0`**. Cost: ~2-5 ms.
7. **RM client / device / subdevice / vaspace** — `NV_ESC_RM_ALLOC` for `NV01_ROOT`, `NV01_DEVICE_0`, `NV20_SUBDEVICE_0`, `FERMI_VASPACE_A`. Four ioctls; includes one GSP RPC for vaspace. Cost: ~10-20 ms.
8. **VRAM pool allocation (one call)** — `NV_ESC_RM_VID_HEAP_CONTROL` with `NVOS32_FUNCTION_ALLOC_SIZE`, flags `NVOS32_ATTR_LOCATION_VIDMEM | _PHYSICALITY_CONTIGUOUS | ALLOC_FLAGS_FIXED_ADDRESS_ALLOCATE`, size = `clientFbSize - reserved`, offset = 0. Returns `hMemory`. Cost: ~50-150 ms (GSP RPC + PMA walk). **This is the ONE VRAM allocation for the entire Keeper lifetime** — all subsequent tensor "allocation" is pointer bump into the pool.
9. **Map VRAM + BAR1** — `NV_ESC_RM_MAP_MEMORY` on hMemory with `NVOS33_FLAGS_MAPPING_DIRECT | CACHING_TYPE_WRITECOMBINED`, then `mmap(fd, size)` with returned cookie → `IS_FB_OFFSET` path in `nvidia_mmap_helper`. Userspace gets a WC pointer covering the full pool. Cost: ~5-10 ms.
10. **Channel + USERMODE + compute object** — allocate TSG (`KERNEL_CHANNEL_GROUP_A`), channel (`HOPPER_CHANNEL_GPFIFO_A=0xC86F` or `BLACKWELL_CHANNEL_GPFIFO_A`), compute object (`HOPPER_COMPUTE_A=0xCBC0` or `BLACKWELL_COMPUTE_A=0xCDC0`), `HOPPER_USERMODE_A` with `bBar1Mapping=1` (doorbell mapped into userspace). Three GSP RPCs: `ALLOC_CHANNEL_DMA`, `NVA06F_CTRL_CMD_BIND(engineType=COMPUTE0)`, `NVA06F_CTRL_CMD_GPFIFO_SCHEDULE(enable=1)`. Cost: ~30-60 ms.
11. **Pin clocks + register MR** — `NV2080_CTRL_CMD_PERF_BOOST(DURATION_INFINITE, CMD_SET)` pins max sustained clock (one GSP RPC). `ibv_alloc_pd + ibv_reg_mr` on the BAR1-covered pool (one MR, persistent for Keeper lifetime). Semaphore pool allocation from pinned sysmem (64-256 slots × 8 B). Cost: ~20-50 ms.

**Total Keeper startup post-host-reboot: ~150-400 ms**, dominated by step 8 (VRAM alloc) and step 10 (channel setup). After step 11, the Keeper enters steady state: zero ioctls, zero GSP RPCs on the hot path. Every kernel dispatch is pushbuffer + doorbell (§14.7); every collective is an embedded pushbuffer entry (MIMIC.md §37.5); every patch is ~10 ns MMIO (§11.9.2).

**Blackwell** (sm_100a+) uses identical sequence with different class handles. **Mixed consumer/datacenter Blackwell** (MIMIC.md §42.6) also works — BAR1 P2P is enabled by default per `PDB_PROP_KBUS_SUPPORT_BAR1_P2P_BY_DEFAULT` in 595.58.03+ for GB20x, and enabled via patch for pre-Blackwell consumer.

Non-NV vendors follow analogous sequences documented in MIMIC.md §36.4 (AMD AM-style ~50-100 ms; TPU ~200-500 ms; Trainium ~100-200 ms; CPU ~5 ms).

### 14.9 Green contexts for role-split SM allocation

DeepSeek-V3's pattern — dedicating specific SM subsets to communication vs compute — maps to Hopper+'s **Green Contexts** feature (hardware-level SM partitioning). Crucible uses green contexts to avoid NCCL's "grab all SMs for the collective, then compute has to wait" serialization.

#### 14.9.1 Background: the DeepSeek pattern

DeepSeek-V3 uses warp-specialized kernels with dedicated SM roles:

- ~20 SMs pinned to MoE dispatch (token-to-expert all-to-all)
- ~20 SMs pinned to MoE combine (expert-to-token all-to-all)
- remaining SMs (~90-100 on H100) run compute (MMAs, attention, norm)

Hand-tuned in their private CUDA kernels using `cooperative_groups` + warp specialization. With green contexts, it becomes a runtime configuration, not a kernel-compile choice.

#### 14.9.2 Green contexts — hardware primitive

Green Contexts (`cuCtxCreate_v3` with partition descriptor, Hopper+) let userspace carve up SMs at hardware-partition level:

```
ctx_compute  = cuCtxCreate_v3(partition = [SM 0..89])        // 90 SMs
ctx_dispatch = cuCtxCreate_v3(partition = [SM 90..109])      // 20 SMs
ctx_combine  = cuCtxCreate_v3(partition = [SM 110..131])     // 22 SMs
```

Each context owns its SMs exclusively; kernels launched into one context cannot preempt another's SMs. Hardware scheduler respects partitioning natively.

#### 14.9.3 Crucible's green-context usage

Keeper init creates N green contexts at §14.8 step 10 (channel + USERMODE setup expanded with per-role channels):

- **compute context** — majority of SMs; runs compute kernels from ExecutionPlan
- **dispatch context** — ~10-20 SMs; runs CNTP collective dispatch kernels (ring_send, all-to-all first-stage)
- **combine context** — ~10-20 SMs; runs CNTP collective combine kernels (ring_recv_reduce, all-to-all second-stage)
- **scheduler context** (optional) — 1 SM; runs on-GPU dynamic scheduler CTA when enabled (§3.9)

Forge Phase I.StreamAssign tags each kernel with its role; Phase J emits the pushbuffer entry to the appropriate context's channel. Device-side scheduling is autonomous; no host coordination beyond initial plan submission.

#### 14.9.4 SM partition policy per workload

Auto-configured by Forge Phase K, user-overridable:

| Workload | compute SMs | dispatch | combine | scheduler |
|---|---|---|---|---|
| Dense transformer (no MoE) | 128/132 | 2 | 2 | 0 |
| MoE with EP=8 | 90/132 | 20 | 22 | 0 |
| MoE with dynamic scheduler | 88/132 | 20 | 22 | 2 |
| DP-heavy with bucketed all-reduce | 120/132 | 6 | 6 | 0 |
| Context-parallel (ring attention) | 100/132 | 16 | 16 | 0 |

Measured on H100 (132 SM). Tuned online by Augur (§13.7) via MFU feedback: if collective utilization < compute utilization, shift SMs from compute to comm; vice versa reversed. Bounds: compute ≥ 50%; comm ≥ 5%.

#### 14.9.5 Combined with warp specialization + setmaxnreg

Within each green context, kernels further specialize:

- **Producer warps** — issue TMA loads; release registers via `setmaxnreg.dec.sync.aligned.u32 40`
- **Consumer warps** — grab released registers via `setmaxnreg.inc.sync.aligned.u32 240`; run MMA

Full hierarchy:

```
GPU silicon (132 SM)
├── compute context (90 SM)
│   └── persistent GEMM kernel
│       ├── producer warps (32)   ← TMA loads, low reg pressure
│       └── consumer warps (96)   ← MMA, high reg pressure, setmaxnreg grabbed
├── dispatch context (20 SM)
│   └── persistent ring_send_recv kernel
└── combine context (22 SM)
    └── persistent ring_recv_reduce kernel
```

See MIMIC.md §15.5 for setmaxnreg PTX emission detail.

#### 14.9.6 Green context lifecycle

Green contexts are created at Keeper init, persisted across plan submissions, destroyed at Keeper shutdown. Each has its own GPFIFO channel + USERMODE doorbell. Plan submissions target specific channels via per-role stream IDs.

If workload characteristics change materially (MoE count changes, DP/TP ratio shifts), Augur may recommend re-provisioning; Keeper tears down and recreates green contexts at next iteration boundary (~10-20 ms, one-time per re-provisioning event).

#### 14.9.7 Cross-vendor support

Green contexts are NV Hopper+ specific. Equivalents:

- **AMD**: XCD (Accelerator Complex Die) partitioning on MI300X — similar but coarser (4 XCDs, 38 CUs each)
- **TPU**: native TensorCore subsystem isolation; scalar processor assigns MXU/VPU/VMEM per dispatch
- **Trainium**: NeuronCore subsystem assignment (TensorEngine / VectorEngine / ScalarEngine / DMA / Sync pre-partitioned in hardware)
- **CPU**: thread-affinity + cgroup cpu-set (userspace partitioning)

The role-split pattern is portable; realization is per-vendor (MIMIC.md §40.8).

---

## 15. Hardware heterogeneity handling

### 15.1 Per-chip cap detection

At Keeper startup, the per-vendor Mimic runtime probes the local chip:

```cpp
auto* chip_info = mimic::<vendor>::detect_local_chip();
// returns: ChipId, SM/CU count, HBM size, driver version, firmware version, ...
auto* target_caps = mimic::<vendor>::load_caps(chip_info);
// returns: TargetCaps<Vendor> (vendor-specific) + abstract TargetCaps
```

Abstract caps are gossiped fleet-wide. Vendor-specific caps stay local.

### 15.2 Fleet recipe intersection

Canopy maintains a fleet-wide set of (chip_id, native_recipe_bitmap) tuples, per Raft epoch. Recipe picker at Forge Phase E takes the intersection.

### 15.3 Per-chip kernel cache segmentation

L3 cache is keyed on (content_hash_ir003, chip_caps_sig). Different chips don't share L3 entries. L2 can span same-family chips (e.g., H100 + H200 share IR003NV for many recipes). L1 is cross-vendor.

### 15.4 Chip failover

If a chip fails (ECC uncorrectable, thermal panic, driver crash), its Keeper:

1. Logs failure details to Cipher
2. Marks itself degraded via gossip
3. Raft evicts it from active work assignments
4. Remaining Keepers reshard
5. Training replays from last checkpoint if step-in-progress was affected

The failed Keeper's hardware may auto-recover (after reboot) or need manual intervention; either way, Crucible continues on reduced capacity.

---

## 16. Realtime performance — bounded latency under production load

The Vigil's dispatch hot path targets ~2ns (COMPILED) to ~20ns (RECORD) per op. A production Relay runs that path alongside the kubelet, the NIC's RDMA ISR, the CUDA driver threads, the khugepaged daemon, neighbour tenants in adjacent cgroups, and the Keeper's own Canopy gossip. Any µs-scale stall is 500–10,000× over the per-op budget. Crucible is not "fast"; Crucible is **bounded**. Throughput can be pipelined around jitter. Per-op determinism cannot — every op runs on the critical path of the ML step.

This section is the design reference for how each Crucible layer cooperates to hold its latency budget under cluster noise. The mechanisms land in the Keeper at startup (§14), exposed via a unified `crucible::rt::Policy` surface. The bench harness (`bench/bench_harness.h`) invokes the same `Policy` — so a passed bench on a dev laptop in `Policy::production()` mode is empirical proof the prod Keeper holds the budget.

### 16.1 Thread taxonomy — HOT, WARM, COLD

Every Crucible thread belongs to exactly one class. The class dictates scheduler policy, memory residency, and affinity:

**HOT** — one thread per compute chip. Runs the dispatch interception, compiled-replay, TraceRing producer. Never syscalls in the steady state (io_uring fire-and-forget for any I/O). Policy:

- `SCHED_DEADLINE(runtime=500µs, deadline=1ms, period=1ms)` — the kernel's CBS admits us with a formal budget. Preempts below-priority work but cannot starve the system indefinitely (unlike SCHED_FIFO).
- Pinned to one P-core selected per §16.2. Ideally an `isolcpus=` core.
- All touched pages `mlock2(MLOCK_ONFAULT)`. Never swapped out.
- NUMA-local to the GPU it serves (`/sys/class/drm/cardN/device/numa_node`).
- SMT sibling optionally offlined (or `PR_SCHED_CORE` to ensure only trusted code runs on the sibling).
- Watchdog: a COLD thread monitors Augur's deadline-miss counter; after N breaches in M seconds, the HOT thread is downgraded to SCHED_FIFO, then SCHED_OTHER, with a diagnostic at each step.

**WARM** — one or more threads per Keeper: background compile (Forge Phase D/E), TraceGraph build, memory planner, Meridian recalibration, TraceRing drainer. Policy:

- `SCHED_OTHER`, nice 0.
- Pinned to a set of cores on the same NUMA node as the HOT thread — **never the HOT core itself**.
- Memory not locked; can page normally under pressure.
- Free to syscall. Block on compile jobs fine.
- Runs the atomic pointer swap that brings newly-compiled code into service at the next iteration boundary (§7.x, §10).

**COLD** — Canopy gossip, Cipher persistence writer, Augur sampler, health monitor, self-update orchestration. Policy:

- `SCHED_OTHER`, nice +10.
- Affinity to any core, preferring a different NUMA node from HOT (stay out of the way).
- Memory not locked.
- Uses standard kernel networking (not RDMA/io_uring) — the traffic is 1 Hz control plane, not worth the complexity.

No thread ever transitions between classes at runtime. The critical operation at iteration boundary is the release-store pointer swap inside the DAG (L7) — the existing HOT thread picks up the new code path on the next dispatch, no handoff needed.

### 16.2 Core selection — never hardcoded

Decision order at Keeper startup. Library: hwloc, or 300 lines of sysfs parsing:

1. **cgroup cpuset**: parse `/proc/self/status:Cpus_allowed_list`. The orchestrator (K8s cpuset cgroup, Slurm cpuset plugin) has already restricted us; this set is authoritative.
2. **Intersect with isolcpus**: `/sys/devices/system/cpu/isolated` ∩ (1). These are the quarantined cores — prefer them.
3. **Topology rank** the remaining candidates:
   - Intel hybrid: `/sys/devices/system/cpu/cpuN/topology/core_type` — prefer `Core` (P-core) over `Atom` (E-core).
   - GPU locality (compute thread): `/sys/class/drm/cardN/device/numa_node` == core's node.
   - NIC locality (comm thread): `/sys/class/net/ibN/device/numa_node` == core's node.
   - SMT: exclude thread siblings of already-selected HOT cores (`/sys/devices/system/cpu/cpuN/topology/thread_siblings`).
4. Fall back to (1) with a logged warning — "running in cohabited mode; expect p99 tail +Nµs".
5. If (1) has fewer cores than Crucible needs (HOT per chip + WARM + COLD), reject startup with a configuration error. Don't silently run degraded.

On the bench harness the same logic runs via `bench::detail::first_isolated_cpu` plus topology-aware additions. Hardcoding "pin to CPU 10" is a bug, not a configuration — the orchestrator chooses the core set, Crucible chooses within it.

### 16.3 Memory residency

Hot state is a fixed, enumerable set of regions allocated at Meridian calibration:

- KernelCache metadata + compiled-kernel code sections (§7.x L7)
- MemoryPlan pool backing (§3.x L3, per-chip, per-NUMA-node)
- TraceRing and MetaLog SPSC buffers (§4.x L4)
- PoolAllocator slot arrays (§3.x)
- Cipher hot-tier mmap'd log buffer (§9.x L14)
- Graph IR arena (§6.x L6)

For each: Keeper issues `mlock2(addr, len, MLOCK_ONFAULT)` at startup. Pages lock on first access and stay resident until munlock. Never `mlockall(MCL_CURRENT)` — too aggressive for a multi-GB runtime; cold state should page normally.

For MemoryPlan pools (multi-GB, mostly-static, sequential-access patterns): additionally `madvise(MADV_HUGEPAGE)` after mmap. On Linux 6.1+, follow with `madvise(MADV_COLLAPSE)` during Meridian to synchronously build 2MB hugepages — khugepaged would otherwise collapse them unpredictably during training, stealing 10–100ms.

Globally: `prctl(PR_SET_THP_DISABLE, 1)` opts the process out of khugepaged entirely. All hugepage usage now comes through explicit `MADV_HUGEPAGE` / `MAP_HUGETLB` paths — no background daemon can touch our mappings.

Prefault pass: during the final Meridian stage, Keeper walks every hot region touching one byte per 4KB (or per 2MB for hugepage regions). Populates page tables before the first dispatch.

Regions NOT locked: Python interpreter state, libstdc++ internals, Keeper config, stack memory for WARM/COLD threads. If any of those faults, the faulting thread is not HOT and the hot path is unaffected.

### 16.4 Layer-by-layer enablement

The 17-layer model maps onto specific realtime mechanisms. This table is the "how to enable realtime at layer N" index:

| Layer | Realtime mechanism | Lives in |
|-------|--------------------|----------|
| L0 F\*X | Hot-path code proven allocation-free via static analysis; proof certificate in Cipher | `fx/src/verify/hot_path.fx` |
| L1 Hardware | Topology discovery (hwloc), frequency lock, C-state disable, IRQ steering detection | `src/rt/Topology.cpp`, `src/rt/FrequencyLock.cpp` |
| L2 Kernels | Kernels pre-compiled at Meridian; JIT only on COLD thread; kernel code mlocked | `src/rt/KernelResidency.cpp` (new) |
| L3 Memory | `mlock2(MLOCK_ONFAULT)` + `MADV_HUGEPAGE` on PoolAllocator, MemoryPlan; prefault walk | `src/rt/MemoryHardening.cpp` (new), hooks into `include/crucible/PoolAllocator.h` |
| L4 Operations | Dispatch thread on SCHED_DEADLINE, pinned, RSEQ for TraceRing head | `src/rt/DispatchThread.cpp` (new) |
| L5 Tensors | ConductorTensorImpl storage backed by L3 pools — no heap alloc in tensor lifecycle | existing `include/crucible/Tensor.h`, verified by L0 |
| L6 Graphs | TraceGraph build on WARM thread; atomic pointer swap to publish | existing `include/crucible/Graph.h`, §7 |
| L7 Merkle DAG | Release-store / acquire-load at swap site; never a lock on hot read path | existing `include/crucible/MerkleDag.h` |
| L8–L12 Intelligence | All analyses on WARM; modifications published via atomic swap at iteration boundary | existing; no new realtime code |
| L13 Distribution | NIC-local NUMA for comm thread; GPUDirect RDMA for bulk; UCX not ibverbs-direct | `src/rt/CommThread.cpp` (new), §5 CNTP |
| L14 Lifecycle | Cipher hot-tier via io_uring with SQPOLL + registered buffers; fire-and-forget from HOT | `src/cipher/HotWriter.cpp`, §9 |
| L15 Meridian+Augur | Meridian prefaults + registers all hot regions; Augur monitors from COLD thread at 1 Hz | `src/meridian/Prefault.cpp`, `src/augur/Monitor.cpp` |
| L16 Ecosystem | Cross-run persistence of Policy profile in Cipher; reload at next startup | existing Cipher path |

The file layout is illustrative — finalise during build-plan milestone §20.

### 16.5 The `Policy` surface

Single type in a new namespace, consumed by both the Keeper and the bench harness:

```cpp
namespace crucible::rt {

struct Policy {
    // HOT thread
    bool          hot_thread_enabled   = true;
    SchedClass    hot_sched            = SchedClass::Deadline;
    uint64_t      hot_runtime_ns       = 500'000;
    uint64_t      hot_deadline_ns      = 1'000'000;
    uint64_t      hot_period_ns        = 1'000'000;
    CoreSelector  hot_core             = CoreSelector::auto_isolcpu_or_topology();

    // Memory
    bool          mlock_hot_regions    = true;
    bool          thp_hint_pools       = true;   // MADV_HUGEPAGE on MemoryPlan
    bool          thp_collapse_now     = true;   // MADV_COLLAPSE during Meridian
    bool          disable_thp_global   = true;   // PR_SET_THP_DISABLE
    bool          prefault_hot_regions = true;

    // Frequency / C-states (require CAP_SYS_ADMIN or writable sysfs)
    bool          lock_frequency       = true;
    bool          disable_c_states     = true;
    SmtPolicy     smt                  = SmtPolicy::keep;  // or offline_sibling

    // I/O
    bool          io_uring_sqpoll      = true;
    bool          rdma_for_comm        = true;   // UCX over ibverbs

    // Watchdog
    uint64_t      hot_deadline_miss_budget = 10;
    uint64_t      watchdog_window_sec      = 60;

    // Fallbacks
    Policy        on_capability_missing = Policy::dev_quiet();

    // Profiles
    [[nodiscard]] static Policy production() noexcept;  // all on
    [[nodiscard]] static Policy dev_quiet()  noexcept;  // pinned + mlock; SCHED_OTHER
    [[nodiscard]] static Policy none()       noexcept;  // fully opt-out
};

[[nodiscard]] std::expected<AppliedPolicy, ApplyError>
    apply(const Policy&) noexcept;

// AppliedPolicy is an RAII handle: destruction restores prior state.

} // namespace crucible::rt
```

Keeper startup (§14.1) invokes `crucible::rt::apply(Policy::production())` as the final pre-fleet-join step. The bench harness has `bench::Run::hardening(Policy)` that applies the same `Policy` for the duration of the measurement.

### 16.6 Capabilities needed

The runtime needs specific Linux capabilities to enable each mechanism. Keeper is shipped with file capabilities via `setcap` (from the operator's deployment tooling); dev laptops use `bench-caps` CMake target. Matrix:

| Mechanism | Required capability | Fallback if missing |
|-----------|---------------------|---------------------|
| `mlock2` | `CAP_IPC_LOCK` | degrade to no locking; log warning |
| `sched_setattr(SCHED_DEADLINE)` | `CAP_SYS_NICE` | degrade to SCHED_OTHER; log warning |
| `sched_setaffinity` | (user on self) | always available |
| `prctl(PR_SET_THP_DISABLE)` | (user on self) | always available |
| `madvise(MADV_HUGEPAGE)` | (user on self) | always available |
| `madvise(MADV_COLLAPSE)` | (user on self, kernel ≥ 6.1) | skip; let khugepaged do it |
| `io_uring_setup(SQPOLL)` | `CAP_SYS_NICE` (for pinning SQPOLL thread) | drop SQPOLL; still fast enough |
| Write `scaling_*_freq` | `CAP_SYS_ADMIN` OR write-access via udev rule | read current and log |
| Write `cpuidle/stateN/disable` | `CAP_SYS_ADMIN` OR udev rule | C-states stay active |
| Write `/proc/irq/*/smp_affinity` | `CAP_SYS_ADMIN` | operator configuration step |
| `PR_SCHED_CORE` | (user on self, kernel ≥ 5.14) | no core-scheduling |
| `perf_event_open` (HW counters) | `CAP_PERFMON` OR `kernel.perf_event_paranoid ≤ 2` | HW counters unavailable in Augur |
| BPF tracepoint attach | `CAP_BPF + CAP_PERFMON + CAP_DAC_READ_SEARCH` | sense hub unavailable (still run) |

Every mechanism degrades to a logged warning, not a hard failure. The deploy-health report (§16.8) summarizes which mechanisms are active, which fell back, and the expected latency impact of each fallback.

### 16.7 I/O — kernel bypass everywhere

- **Inter-node bulk data**: RDMA verbs via UCX (CNTP §5). Zero-copy, zero kernel involvement in the data path. GPUDirect RDMA for GPU-to-NIC.
- **Intra-node GPU-GPU**: NVSHMEM (NVIDIA) / ROCSHMEM (AMD). Device-addressable shared memory.
- **Cipher log writes**: io_uring with `IORING_SETUP_SQPOLL` + `IORING_REGISTER_BUFFERS` + `IORING_REGISTER_FILES`. HOT submits SQEs with pre-registered buffers; a kernel SQPOLL thread drains without syscall. The HOT thread never waits — CQE processing is COLD's job.
- **Control plane** (gossip, Raft heartbeat, health): regular kernel TCP. COLD thread, 1 Hz traffic — io_uring gains are not worth the complexity here.

### 16.8 Deploy-health report

At Keeper startup, after applying the Policy, Keeper emits a machine-readable report describing the active hardening:

```
crucible-keeper v0.X.Y — realtime deploy-health
============================================
cpuset.cpus_allowed: [4-11, 16-23]  (cpuset cgroup)
isolcpus:            [4-7]           (from /proc/cmdline)
nohz_full:           [4-7]           ← OPTIMAL
rcu_nocbs:           [4-7]           ← OPTIMAL
hot_thread.core:     4 (P-core, NUMA 0, GPU-local)  ← OPTIMAL
hot_thread.sched:    SCHED_DEADLINE(500µs/1ms/1ms)
hot_thread.smt_sibling_cpu: 36 (offlined)
mlock_hot_regions:   OK (4.2 GB locked)
thp_collapse_now:    OK (pool regions now 2MB hugepages)
frequency_lock:      OK (locked to 5600 MHz on CPU 4)
c_state_disable:     OK (CPU 4 states 1-6 disabled)
irq_steering:        IRQ 128 (NIC mlx5) -> CPU 0  ← OPTIMAL
                     IRQ 196 (NVMe) -> CPU 8     ← OPTIMAL
                     (0 IRQs on hot cores)
bpf_sense_hub:       attached, 59 tracepoints  ← OPTIMAL

DEPLOY STATUS: OPTIMAL
```

On a suboptimal deploy, the report is explicit about what's missing and the expected p99 impact:

```
DEPLOY STATUS: DEGRADED
  isolcpus: NOT SET — expect p99 tail +2-5µs from softirq on hot cores
  frequency_lock: DENIED (no CAP_SYS_ADMIN) — expect p99 tail +1-3µs from frequency transitions
  IRQ 196 (NVMe) -> CPU 4 — HOT cpu is taking device IRQs; expect occasional 10µs-class stalls
```

The operator (and Augur, programmatically) uses this report to drive remediation. The report is written to Cipher at every Keeper startup — historical deploy-quality is queryable across checkpoints.

### 16.9 Validation — bench harness as production probe

The realtime hardening is not proven by argument; it is proven by measurement. The bench harness (`bench/bench_harness.h`) applies the same `Policy` via `bench::Run::hardening(policy)`. The eBPF sense hub (`bench/bpf/sense_hub.bpf.c`, §17) attaches 59 kernel tracepoints and reports every counter that fired during the measurement window.

Development workflow:

```cpp
// Dispatch hot path, measured under production hardening
auto r = bench::Run("vigil.compiled_dispatch")
    .hardening(crucible::rt::Policy::production())
    .samples(1'000'000)
    .measure([&]{ vigil.dispatch_compiled(op); });

r.print_text(stdout);
// Expected output, if hardening works:
//   vigil.compiled_dispatch  p50=2.1  p90=2.3  p99=3.1  p99.9=4.8  max=12.4  cyc=4.9  cpu4
//     └─ clean
```

If `└─ clean` appears, the hot path held its budget with zero kernel interference — ship it. If anything non-zero shows (`preempt`, `pgfault`, `softirq`, `migrate`), the `senses:` line names the interference and the fix lands in `Policy`, not in the code path. The harness is the oracle; the hardening is its subject.

Per-layer regression benches run the same way:

- `bench_memory_plan` exercises L3; if `pgfault` appears, Meridian's prefault is incomplete.
- `bench_trace_ring` exercises L4; if `preempt` appears, SCHED_DEADLINE admission failed.
- `bench_compile_swap` exercises L7; if `softirq` appears on the HOT core during the swap, IRQ steering is incorrect.

Regression-testing Crucible at the ML-workload level runs a short (~500 step) training loop with `crucible::rt::Policy::production()` and asserts every per-step latency is within `5 × baseline_p50`. A breach implicates either the hardening or the workload — the sense hub localises which.

### 16.10 Dev-mode defaults and laptop workflow

`Policy::production()` on a dev laptop is hostile: SCHED_DEADLINE threads can wedge a laptop if the benched code spins; frequency-lock burns battery; mlock can fail under constrained RAM. Dev defaults (via `Policy::dev_quiet()`):

- HOT thread: `SCHED_OTHER`, pinned but not deadline-scheduled
- `mlock2` only the smallest hot regions (TraceRing, KernelCache metadata)
- No frequency lock, no C-state changes, no SMT fiddling
- THP stays at kernel default (usually `madvise`-driven)
- Watchdog disabled (laptop won't wedge)
- io_uring still preferred (no permission needed)
- Sense hub attached if `bench-caps` has been run

Laptop benches show numbers 2-5× noisier than production, but relative comparisons (A vs B) still hold via Mann-Whitney U. The `Policy::production()` mode is reserved for CI runs on dedicated bench nodes with proper caps and isolcpus.

### 16.11 Operator prerequisites — what's the deploy contract

For `Policy::production()` to actually achieve OPTIMAL, the operator must configure the node outside Crucible's reach:

- Boot cmdline: `isolcpus=<hot_cores> nohz_full=<hot_cores> rcu_nocbs=<hot_cores> intel_pstate=disable processor.max_cstate=1`
- IRQ affinity: pin every device IRQ away from `hot_cores` via udev rules or `/etc/tuned/*`
- cgroup cpuset: Keeper pod's cpuset includes `<hot_cores>` exclusively; no noisy neighbours share
- CPU frequency: operator sets `/sys/devices/system/cpu/cpuN/cpufreq/scaling_governor=performance` on hot cores (or grants Keeper write access via udev rule allowing CAP_SYS_ADMIN-equivalent)
- Hugepage reserve: `vm.nr_hugepages` configured to cover MemoryPlan pool size per Keeper
- Kernel config: `CONFIG_PREEMPT_RT=y` for true realtime, or `CONFIG_PREEMPT=y` for soft realtime
- Capabilities: systemd unit grants `CAP_IPC_LOCK, CAP_SYS_NICE, CAP_PERFMON, CAP_BPF, CAP_DAC_READ_SEARCH` to the Keeper binary

The k8s-Canopy operator CRD exposes these as deploy-plan fields; the operator translates them into a PreferredNodeAffinity plus the appropriate privileged securityContext. When a node lacks the requested kernel config or capabilities, the Keeper starts in DEGRADED mode and the operator marks the node as such in its status — orchestrator can then weight scheduling decisions accordingly.

---

## 17. Observability and debugging

### 16.1 Time-travel debugging

Cipher persists event-sourced DAG chain (from CLAUDE.md L14). Any step of any past run can be replayed exactly via the §10 replay mechanism. Debugging workflow:

```bash
crucible replay --checkpoint=step-12500 --to=step-12847
# replays steps 12500 → 12847, producing byte-identical results to original run

crucible inspect --tensor=layer.47.attn.qkv --step=12847
# returns the tensor's value; loaded from Cipher or recomputed

crucible blame --tensor=loss --step=12847
# walks DAG backward from loss; identifies which upstream tensor + op produced the NaN
```

### 16.2 Per-op provenance

Every CrucibleTensorImpl carries provenance:

- Producer op (which IR001 node)
- Source line in user code (when captured, Python stack preserved in arena)
- Content hash
- Shape + dtype + recipe

On error (`NaN detected at step 12847`), the stack trace includes the full chain:

```
CrucibleRuntimeError: NaN in tensor layer.47.attn.qkv at step 12847

  First NaN detected at:
    content_hash: 0x5B1C...
    producer: aten::matmul (train.py:154)
    inputs:
      attn_input: 0x9E37...   (train.py:152: self.norm(x))
      qkv_weight: 0x7C15...   (train.py:89: parameter)

  Ancestor chain:
    step 12846: qkv_weight was finite (last valid)
    step 12847: qkv_weight is NaN (first error)

  Suggested action: gradient explosion; check step 12846's gradients
```

### 16.3 Per-kernel attribution

Every CompiledKernel carries its Mimic-predicted cost breakdown (per-pipe utilization, per-stall-reason cycles, memory traffic, insights). User can query:

```bash
crucible profile --step=12847 --kernel-id=42
# returns: predicted vs measured, insights, per-stall cycles, recommendation

crucible hot-kernels --top=10 --window=1h
# returns: 10 hottest kernels by measured cycles in the last hour
```

### 16.4 Crucible Top

A `crucible top` TUI, analogous to `nvtop` / `nvidia-smi` / `rocm-smi` but Crucible-native:

```
CRUCIBLE TOP      Cluster: llama-70b-train     Epoch: 43     Step: 12847/50000

Members:  64 active  (3 degraded)           Training: 87.4 tok/s
Recipe:   f16_f32accum_pairwise (BITEXACT)  MFU: 58.3%

Relay            Chip          UID     Step     GPU%   HBM%   Comm     Recipe
r0042.dc1.ex    h100_sxm5     8F2...  12847    92%    67%    4.1GB/s  ✓
r0043.dc1.ex    h100_sxm5     8F3...  12847    93%    67%    4.0GB/s  ✓
r0044.dc1.ex    h100_sxm5     8F4...  12847    88%    67%    4.2GB/s  ✓ (degraded: T=85°C)
...

Bottleneck: attention kernel at layer 34 (14.2ms, 19% of step)
Recent drift events: 0 (clean)
```

### 16.5 No vendor-tool interop

`cuda-gdb`, `rocgdb`, `nsight-compute`, `tpu-profile` won't recognize our binaries — we emit our own vendor-ISA-compliant bytes but don't embed vendor debug metadata (ELF DWARF sections in vendor-specific formats). Instead, we ship our own:

- `crucible-gdb` — GDB-compatible debugger that reads our cubin/HSACO/NEFF/TPU-exec metadata
- `crucible-profile` — nsight-compute-equivalent timeline viewer
- `crucible-trace` — per-kernel trace explorer

These are built on top of Mimic's per-vendor runtime (which exposes counter/trace APIs) plus Cipher's event log. ~20K LoC of tooling, Python-based with C++ binding for perf-critical paths.

### 16.6 Plan-level observability

Plans are Cipher-addressable first-class objects. `crucible plan` subcommands give textual, diffable, bisectable views into what the runtime is actually submitting.

#### 16.6.1 `crucible plan show <plan_hash>`

Dump a plan's structure:

```
$ crucible plan show 0x9e37_79b9_7f4a_7c15

Plan 9e3779b97f4a7c15
  Target:      nv_sm_100a (H200 SXM5)
  Forge:       1.2.3
  Mimic-NV:    1.2.3
  Recipe:      f16_f32accum_tc_pairwise_rn (BITEXACT_TC)
  Compiled:    2026-04-17T12:34:56Z
  Bucket:      seq_len=[8193,32768], batch=[9,32]

  Pushbuffer: 2048 bytes, hash=0x8a1f...
  GPFIFO:     72 entries × 8B
  Kernels:    9
    [0] layer_norm_001     tile=[1,256,1]      predicted=2200 cycles
    [1] gemm_qkv_001       tile=[128,128,64]   predicted=14500 cycles
    [2] rope_001           tile=[128,128]      predicted=1800 cycles
    ... (6 more)

  Patch points (7):
    "batch_size"      @ 0x0120  width=4  kind=SHAPE_DIM    value=16
    "seq_len"         @ 0x0124  width=4  kind=SHAPE_DIM    value=8192
    "learning_rate"   @ 0x0234  width=4  kind=SCALAR f32   value=1.00e-04
    "dropout_p"       @ 0x0238  width=4  kind=SCALAR f32   value=0.10
    "seed"            @ 0x0014  width=8  kind=RNG_COUNTER  value=0x8675309
    "step_sem"        @ 0x08a0  width=8  kind=SEMAPHORE    value=12847
    "expert_routes"   @ 0x1400  width=32 kind=EVENT_TENSOR shape=[8,4]

  Chain in:  sem_weight >= 12847, sem_data >= 12847
  Chain out: sem_step signals 12847

  Guards (3):
    schema_hash == 0x7c15a3f2 (forward_attn_mlp)
    shape_hash  ∈ [seq_len=[8193,32768], batch=[9,32]]
    recipe_hash == 0x5b1c3fa6c7d8e4a9

  Predicted step time: 14.2 ms
  Measured (last 100):  mean=14.4 ms  p95=14.6 ms
  Drift: +1.4% (nominal)
```

#### 16.6.2 `crucible plan diff <hash_a> <hash_b>`

Structural comparison:

```
$ crucible plan diff 0x9e37...bb 0x2c94...8f

kernel_03 (attention @ layer 5):
  content_hash:     0x8a1f... → 0x2c94...    (recompiled)
  tile:             {M=128,N=128,K=64} → {M=64,N=256,K=64}   (MAP-Elites picked different cell)
  predicted_cycles: 14500 → 12800            (-11.7%)
  reg_pressure:    152 → 196                 (+29%, within budget)

patch_point "learning_rate":
  value: 1.00e-04 → 3.00e-05                 (schedule change, expected)

memory_plan:
  total_pool:       2147483648 → 2147483648  (unchanged)

no other changes.
```

#### 16.6.3 `crucible plan bisect`

Find the plan change that caused a regression:

```
$ crucible plan bisect --good step-12000 --bad step-12500 --metric=mfu
Bisecting 7 plan changes between step-12000 and step-12500...
Testing plan at step-12256 ... mfu=63.2% (good)
Testing plan at step-12384 ... mfu=58.1% (bad)
Testing plan at step-12320 ... mfu=63.0% (good)
Testing plan at step-12352 ... mfu=58.2% (bad)

First bad plan: 0x2c94...8f at step-12345
  Cause: kernel_47 (attention) recompiled due to shape_bucket [8193,32768] sub-specialization
  MFU change: -5.1% (regression exceeds threshold)

Suggested remediations:
  1. Revert plan:        crucible plan revert 0x2c94...8f
  2. Widen bucket:       crucible bucket extend seq_len [8193,32768] [8193,49152]
  3. Force re-search:    crucible recompile kernel=attention_layer_47 budget=500ms
```

#### 16.6.4 `crucible plan validate <plan_hash>`

Offline validation without executing. Verifies:

- All referenced CompiledKernel hashes exist in L3 cache
- All slot offsets fit within the reserved pool
- All PatchPoints target valid byte ranges within their kernels' pushbuffer chunks
- All ChainEdge semaphores are allocated
- The guard set covers the declared shape bucket
- The recipe is in the fleet's current intersection

O(plan size), runs in microseconds. Called automatically on plan load from Cipher and in CI before any plan enters production.

#### 16.6.5 `crucible plan export`

Emit a plan as human-readable JSON or binary MessagePack for debugging or external tool integration:

```
crucible plan export 0x9e37... --format=json > plan.json
```

#### 16.6.6 Cost

These tools are built on Cipher's content-addressed storage plus Phase L measurement records. Zero runtime overhead on the training Keeper; dumped views are computed on the introspection host.

---

## 18. Security and multi-tenancy

### 17.1 Per-Relay isolation

Each Keeper runs in its own process. Memory isolation is process-level (kernel MMU). No shared writable memory across Keepers except via explicit RDMA regions (pre-registered, known protection).

Within a Keeper: single process, no intra-process isolation. If user code (captured op bodies, custom ops) contains a memory bug, the Keeper can crash. Raft detects death, Canopy reshards, training replays from last checkpoint.

### 17.2 Multi-tenancy

Multiple CrucibleClusters on the same physical cluster:

- Each cluster has its own `kubectl namespace` (k8s) or its own `SLURM_JOBID` (SLURM)
- Keepers of different clusters don't gossip (they filter SWIM by cluster id)
- Different clusters may share physical GPUs via k8s device plugin time-slicing; Crucible is agnostic to this
- Different clusters definitely do not share RDMA memory regions

### 17.3 Cipher access control

L3 cache entries (compiled binaries) are safe to share across clusters — they're content-addressed, no secrets. Configurable per-namespace: `cipher.l3_sharing = "cluster" | "tenant" | "public"`.

L1 / L2 same — shareable.

TrainingCheckpoints are private by default. Namespace-scoped. Opt-in federation.

### 17.4 Federation trust model

Two Crucible installations federating:

- Agree on a shared cold-tier bucket (S3, GCS) with read-write ACLs per namespace
- Each installation writes L1/L2 entries with their own content hashes
- Other installation reads via content hash, verifies integrity (hash matches)
- No secrets flow; entries are pure kernel/algorithm artifacts

If you want cross-org weight sharing (federated learning), that's a different layer — see CLAUDE.md's L16 Ecosystem discussion. CRUCIBLE.md doesn't cover federated weight exchange; that's a research area.

### 17.5 Attack surface

Crucible's attack surface, relative to a traditional vendor-library stack:

| Attack vector | Traditional stack | Crucible |
|---|---|---|
| Malicious vendor library update | Possible; libraries update via apt/yum | N/A; no vendor libs |
| Compiled-kernel poisoning | Possible if shared cache includes bad entries | Content-addressed hashes verified on read |
| RDMA memory-access attacks | Possible if MR pinning is lax | Our MRs are pinned to pool bases, read-only where appropriate |
| Raft log tampering | Possible if adversary in quorum | Same risk as any Raft system; mitigated by TLS-over-CNTP-transport |
| eBPF program malware | Possible if attacker loads BPF | Our BPF programs are signed; unsigned programs rejected |
| Cipher cross-tenant read | Possible via misconfigured ACLs | Namespace-scoped; audit logs available |

Net: fewer vendor-library attack surfaces, same RDMA/Raft risks as everyone else in the space.

### 17.6 Network topology requirements and discovery

CNTP's promise of zero-userspace-tax steady-state (§6) assumes specific capabilities on the underlying fabric. These are probed at Keeper startup; missing capabilities fall back to software paths.

**RDMA QP scaling beyond 128 peers.** Classical RDMA creates one Queue Pair per peer-pair; for N peers this is `N(N-1)/2` QPs per Keeper. NIC firmware typically supports ~10K-100K QPs; clusters >256 nodes exhaust RC-mode QP tables and require Dynamically Connected (DC) mode.

- ≤128 peers: full-mesh RC QPs. ~1μs latency, best bandwidth.
- >128 peers: DC QPs (ConnectX-5+). One DC QP per side, addressed per-destination via DCT key. ~1.1-1.3μs latency, scales to arbitrary peer count.
- **SWIM islands**: the mesh is partitioned into 32-64-peer islands; intra-island uses RC (most traffic, latency-sensitive), inter-island uses DC. Canopy's gossip structure maps naturally onto this.

Capability probe: `ibv_query_device` at init reports `max_qp`, `max_dc_ini_num`, `max_dc_tgt_num`. Missing DC support on a NIC → Keeper refuses to join a cluster larger than its RC budget.

**Multi-NIC rail assignment.** Modern nodes have multiple NICs (H100 SXM5: 8 NICs, each 400 Gb/s; total 3.2 Tbps per node). Single-QP, single-NIC cannot achieve aggregate bandwidth.

- One QP per GPU-NIC pair; round-robin striping per transfer (simple, ~linear scaling).
- Multi-rail spray per peer for very large transfers (>1GB): shards across all NICs.
- Per-NIC MR registration; per-vendor Mimic backend (§37 of MIMIC.md) chooses the NIC-GPU affinity based on PCIe topology.

**GDR topology constraints.** GPUDirect RDMA requires GPU and NIC on the same PCIe root complex (or NVLink bridge). Absent or broken topology → host-staged DMA (GPU → host → NIC → wire, 2× copies).

- At Keeper init, probe each (GPU, NIC) pair's GDR capability via `/proc/driver/nvidia-fs` or equivalent.
- Topology-aware rail assignment: prefer GDR-capable pairs; fall back to host-staged only for uncovered pairs with a warning.
- STRICT mode (`network.require_gdr=true`): Keeper refuses to start if any GPU lacks GDR-capable NIC.

**eBPF program chain via tail calls.** The fast-path BPF program exceeds the 1M-instruction verifier limit when SWIM + Raft + data-redirect are in one program. Solution: tail calls chain dispatcher → handler:

```
xdp_dispatcher (match CNTP magic, classify message)
    → tail_call BPF_MAP_TYPE_PROG_ARRAY[role]
            ├── xdp_swim_handler     (~15K insts)
            ├── xdp_raft_handler     (~25K insts)
            ├── xdp_data_redirect    (~40K insts, uses AF_XDP REDIRECT)
            └── xdp_fallback_to_userspace
```

Each handler is compiled separately, attached to the program-array map by role index. Kernel ≥5.8 required (BPF ringbuf for zero-copy userspace notifications).

**VPC bootstrap without multicast.** Cloud environments (AWS VPC, GCP VPC) lack multicast between instances. Four alternatives:

- **k8s headless Service**: DNS SRV records for all Keeper pods. Preferred for k8s deployments.
- **Bootstrap URL**: one HTTPS endpoint (in cloud metadata service or an ELB) returning the current peer list. Not a master — just a rendezvous.
- **ConfigMap seed peers**: list of stable peer addresses in the `CrucibleCluster` CR. Falls over if all seeds die; acceptable for small dev clusters.
- **Gossipsub over libp2p DHT**: self-organizing, heavier overhead. Useful for federated deployments across administrative boundaries.

Default: k8s headless Service on k8s, `bootstrap_url` on bare-metal/cloud VMs, ConfigMap seeds on SLURM.

**NUMA-aware placement.** Crossing NUMA domains adds 100s of ns per memory access plus QPI/UPI bandwidth cost. At Keeper init:

1. Detect each NIC's NUMA affinity (`/sys/class/net/<nic>/device/numa_node`)
2. Detect each GPU's NUMA affinity
3. Pin compute threads to NUMA nodes matching the (GPU, NIC) pair they service
4. Allocate MRs on the NIC's NUMA via `mbind()`
5. For cross-NUMA GPU↔NIC pairings: bounce buffer on the NIC's NUMA

Thread pinning via `sched_setaffinity`; memory binding via `mbind` or per-NUMA `numa_alloc_onnode`.

**Soft-RoCE fallback.** When the NIC doesn't support hardware RDMA (cheap gear, older hardware), Linux Soft-RoCE (`rxe`) emulates RDMA in software. ~10× slower than hardware RoCE but functional.

- At init, probe NIC capabilities: `ibv_query_device` reports `HCA_CORE_CLOCK` and transport attributes
- If emulated (Soft-RoCE), check primitive support: atomics are spotty, DC QPs don't exist on rxe
- CNTP's capability registry records the available subset; collective-algorithm picker routes around missing primitives (fall back from DC to per-peer RC, from atomics to messaging)
- Not recommended for production training; acceptable for development and CI

**Persistent-kernel concurrent collective dispatch.** Bucketed async all-reduce (§12.7) requires multiple concurrent collective launches. Per-vendor requirement:

- NV: dedicated "collective stream" per bucket group; MSCCL compiler output supports this natively
- AM: XGMI concurrent collective contexts
- TPU: ICI supports multiple aggregation groups; torus-order care
- TRN: NeuronLink NCC groups

Verified at Keeper init via `mimic::<vendor>::comm::supports_concurrent()`. Missing support → bucketing disabled, fall back to monolithic all-reduce per step.

### 17.7 Driver-gate bypass policy

NVIDIA's driver artificially gates features for consumer GPUs (and sometimes older datacenter SKUs) via per-chip property tables. The silicon typically supports these features at the PCIe/XAL layer; segmentation is software-only. We enumerate bypasses here as explicit design commitments, because they shape fleet-composition decisions (§7.3, §12.3) and deployment documentation.

Principle: **we enable every hardware-capable feature regardless of SKU marketing tier** on any chip we support. Physical-silicon limitations (e.g., NVLink absent on 4090+) are honored; software-only gates are bypassed.

| Feature | Stock driver behavior | Crucible behavior | Mechanism |
|---|---|---|---|
| BAR1 P2P writes | Disabled on pre-Blackwell consumer via `PDB_PROP_KBUS_SUPPORT_BAR1_P2P_BY_DEFAULT=false` | Enabled unconditionally | `RMForceStaticBar1=1 RMPcieP2PType=1` + aikitoria-style property override for AD102/GA102 |
| BAR1 P2P reads | Blocked via `PDB_PROP_KBIF_P2P_READS_DISABLED` on consumer | Enabled unconditionally | Patch overrides SKU-gated property |
| P2P atomics | Blocked on consumer | Enabled when PCIe root port advertises support | Probe capability; patch overrides |
| Resizable BAR → full VRAM visible | Driver requests small BAR on consumer | Full-VRAM BAR1 | `NVreg_EnableResizableBar=1` + BIOS rBAR on + `iommu=pt` |
| GPUDirect RDMA (NIC ↔ VRAM) | `nvidia-peermem.ko` refuses to bind on consumer | Works via static BAR1 | Bypass peermem entirely; register BAR1 region as one MR at Keeper init (§14.8 step 11) |
| L1 / L1.2 ASPM substates (10-100μs exit) | Enabled by default | Disabled | `pcie_aspm=off` kernel param + per-link sysfs write |
| RC watchdog polling at 1 Hz | Enabled; runs its own channel | Disabled | `RmWatchDogTimeOut=0 RmRcWatchdog=0` |
| Dynamic clock management (~5-10% variance) | Enabled | Clock pinned to max sustained | `NV2080_CTRL_CMD_PERF_BOOST(DURATION_INFINITE)` at Keeper init |
| Confidential Computing encryption of GSP RPCs (~20-30%) | Auto-enabled on CC SKUs | Disabled | SBIOS `CC mode = off`; verify `PDB_PROP_CONFCOMPUTE_CC_FEATURE_ENABLED=false` |
| Per-process GSP reboot on last-fd-close | Default | Disabled | `nvidia-persistenced` or Keeper sentinel fd |
| MSI-X for kernel completion (~2-3 μs eventfd wake) | Used by stock libcuda | Disabled (kept for fatal paths only) | `NVreg_EnableMSI=0`; spin-poll pinned semaphore instead |
| GPU accounting (per-channel proctype bookkeeping) | Enabled | Disabled | `PDB_PROP_GPU_ACCOUNTING_ON=false` |

**What we do NOT bypass** (hardware-physical or safety-critical):

- **NVLink**: physically absent on 4090/5090/consumer-Blackwell — PCB-level, not software. Use BAR1 P2P instead.
- **ECC memory**: preserved. GSP still reports uncorrectable errors; our response is crash + FLR + Cipher replay (§17.8).
- **Firmware signing**: GSP firmware is NVIDIA-signed. Boot sequence requires it (§14.8 step 2). Unbypassable and we don't want to.
- **FSP/SEC2 mailbox PLM protections**: unreachable from userspace. nvidia.ko stays in the boot path (hybrid model per MIMIC.md §36.4.2).

**Mixed-fleet considerations**: Asymmetric fleets mixing consumer and datacenter SKUs are supported — the bypass applies equally. See §12.3 `partition_policy` for per-vendor scope rules; MIMIC.md §42.6 for mixed Blackwell deployment; FORGE.md §25.8 for partition-solver asymmetric-fleet handling.

**Warranty / support implications**: driver-patched systems are unsupported by NVIDIA for warranty or CUDA SDK issues. Crucible ships pre-patched `open-gpu-kernel-modules` source (MIT, inherited upstream) as `crucible-driver` package; users opt in at install time with full disclosure. For compliance-critical workloads (healthcare, finance), all-datacenter fleets remain recommended.

### 17.8 Failure recovery via FLR

On GPU hang or uncorrectable error, recovery is:

1. Detection (BAR0 register `0xFFFFFFFF`, fatal MSI-X vector, watchdog timeout)
2. FLR + GSP re-upload — target ~1.5-2.5 s (stock driver: 5-10 s)
3. Replay from last Cipher checkpoint — cost `N × step_time` where N is checkpoint interval

Stock driver is slow because it re-enumerates the full device tree, re-initializes all RM client state, re-uploads GSP firmware from disk. We compress by caching firmware in hugepage sysmem and skipping client-state rebuild (single-tenant, nothing to preserve).

#### 17.8.1 Detection

| Signal | Latency | Source |
|---|---|---|
| BAR0 read returning `0xFFFFFFFF` (surprise removal) | ~1 ms (next hot-path poll) | `NV_IS_DEVICE_IN_SURPRISE_REMOVAL` check in spin-poll |
| PMC_INTR_0 fatal subtree bit set | <1 μs (MSI-X vector) | vfio-pci eventfd, Keeper IRQ thread |
| FIFO/GR RC notifier set | <1 ms (GSP-originated RPC) | Keeper's RPC listener |
| Plan completion timeout | `plan.timeout_ms` (default 30 s) | Plan runtime watchdog |

Detection → Keeper enters "degraded" state → Raft commits epoch bump → in-flight plans aborted → FLR sequence begins.

#### 17.8.2 FLR sequence

1. **Mask MSI-X + clear Bus-Master** (PCI config writes) — ~5 ms
2. **Issue FLR** (`ioctl(VFIO_DEVICE_RESET)` or `NV_XVE_DEVICE_CONTROL_STATUS._INITIATE_FN_LVL_RST`) — writes + **100 ms mandatory PCIe settle window** (hardware-imposed)
3. **Restore PCI config space** (kernel PCI core) — ~5 ms
4. **GFW boot complete poll** (`NV_PGC6_AON_SECURE_SCRATCH_GROUP_05[GFW_BOOT_COMPLETE]`) — ~200-400 ms
5. **GSP re-upload from cache** — DMA-map cached GSP-FMC + GSP-RM from hugepage sysmem, program WPR2, kick FSP secure-boot doorbell. ~800 ms - 1.2 s (dominated by GSP internal boot + SPDM on CC SKUs)
6. **Init-RPC handshake** — `GSP_SET_SYSTEM_INFO`, `GSP_SET_REGISTRY`, poll `GSP_INIT_DONE`. ~150-250 ms
7. **Re-establish channel + pin clocks** — repeat §14.8 steps 10-11 (cache-warmed). ~50-100 ms
8. **Declare recovered** — Raft commits reinstatement; Plan replay begins

**Total: ~1.3-2.2 s** from FLR initiation to "ready for pushbuffer submission". Detection-to-recovered wall-clock: ~1.5-2.5 s.

#### 17.8.3 Plan replay after FLR

Per §10.7:

1. Keeper queries Cipher for latest TrainingCheckpoint manifest at `T₀ ≤ T_failure`
2. Loads `weights_T₀`, `optimizer_T₀`, `data_cursor_T₀`, `seed` from Cipher (hot-tier from healthy peers; warm NVMe fallback)
3. Rewinds DataNode cursors to T₀ positions
4. Replays steps T₀ → T_failure deterministically using cached plans (same plan_hashes; L1/L2/L3 caches valid)
5. Resumes training at T_failure + 1

Replay cost: `N × step_time` where `N = T_failure - T₀`, bounded by checkpoint interval.

#### 17.8.4 Total unavailability bounds

Single-Keeper failure in an 8-node DP=8 training run:

| Step | Cost |
|---|---|
| Detection | ~1-5 s (worst-case SWIM indirect confirmation) |
| Raft membership commit | ~50-100 ms |
| FLR + GSP re-upload | ~1.3-2.2 s |
| Plan reshard | ~200-500 ms |
| **Recovery excluding replay** | **~3-8 s** |
| Plan replay (N=500, 1 s/step) | ~500 s |
| **Total fleet unavailability (N=500)** | **~8-10 min** |

Per checkpoint-interval tier:

- Chinchilla training (N=500): ~8-10 min per failure — tolerable
- Mid-tier (N=100): ~2-3 min per failure
- Aggressive (N=50): ~1-1.5 min per failure
- Serving (N=10): ~10-15 s per failure

**Proactive reseed**: if the same GPU hangs twice within a 1-hour window, Canopy marks it "unhealthy"; Raft commits removal; fleet continues on N-1 with partition reshard (~350 ms on 70B model for 8→7 TP decrease, per §12.3).

#### 17.8.5 CI validation

`bit_exact_recovery_invariant` (§10.8 harness addition):

1. Train 1000 steps from seed=42 on 8 H100s
2. Kill GPU 3 at step 500 (ioctl-simulated uncorrectable ECC)
3. Verify FLR completes <3 s
4. Verify replay from T₀=500 resumes at T=501
5. Verify weights at step 1000 byte-identical to baseline

Must pass for every BITEXACT recipe on every supported backend before release gate.

---

## 19. What ships where — Crucible runtime vs Forge vs Mimic

Clean ownership cut, codified for future contributors:

### 18.1 Crucible runtime owns

- L4 Dispatch (Vessel adapters, mock-tensor capture, TraceRing, MetaLog, sync-point handling)
- L6 TraceGraph construction
- L7 Merkle DAG (RegionNode, BranchNode, LoopNode, ContentHash, MerkleHash)
- L3 PoolAllocator + memory-plan application (Forge computes the plan; runtime applies it)
- L14 Cipher (three-tier persistence, content-addressed, federation)
- Keeper daemon (per-Relay process, lifecycle, health monitoring)
- Canopy mesh (SWIM gossip + Raft consensus + membership + fleet recipe intersection)
- CNTP (transport + collectives protocol + NetworkOffload plane + eBPF fast path)
- Elastic membership + live reshard
- Data loading (DataNode IR001 op, CPU Relay worker pool)
- Augur (sampling, drift detection, regression detection, recommendations)
- Time-travel debugging + profiling + `crucible top`
- k8s operator (`CrucibleCluster` CRD) + SLURM launcher + systemd unit
- Native Python / C++ / Rust frontends
- Replay Determinism CI harness
- Genesis Kernel Pack distribution mechanism (the data is built in CI via Forge+Mimic; Crucible ships + loads it at runtime)

### 18.2 Forge owns

Everything in FORGE.md:

- IR001 → IR002 lowering
- Kernel template matching (the 21 KernelKinds)
- NumericalRecipe pinning + recipe registry
- IR002 tile/memory/schedule passes
- Phase A-L pipeline
- Abstract TargetCaps
- The dispatch shim to Mimic per-vendor backend

### 18.3 Mimic owns

Everything in MIMIC.md:

- Per-vendor IR003*
- Per-vendor ISA emitter + binary writer + decoder
- Per-vendor three-tier simulator
- Per-vendor MAP-Elites search + mutations + insights
- Per-vendor runtime library (the `mimic::<vendor>::rt::` namespace, replaces libcuda/libnrt/libtpu/libhsa)
- Per-vendor collective library (the `mimic::<vendor>::comm::` namespace, replaces NCCL/RCCL/libnccom)
- Per-vendor calibration harness
- Per-chip data files (JSON)
- Per-vendor peephole rules + math templates
- Cross-vendor numerics CI harness (compiles the same IR002 on every backend and compares)

### 18.4 Shared / ambiguous

Some things could go either way; here's the decision:

- **NumericalRecipe struct** — defined in Forge (IR002 layer); consumed by Mimic. Forge owns.
- **CNTP collective algorithm implementations (ring, tree, HD)** — Crucible owns the *protocol* and *algorithm*; Mimic owns the per-vendor *transport primitives* (NVLink peer-access, XGMI, ICI, NeuronLink). The CNTP `all_reduce` function calls into per-vendor `mimic::<vendor>::rt::send/recv` functions.
- **Genesis Kernel Pack** — data is built in CI via Forge+Mimic. Crucible ships the `.bin` files + loads them into L3 at Keeper startup. Crucible owns the loader; Forge+Mimic own the builder.
- **TargetCaps struct** — Abstract caps in Crucible/Forge visibility; vendor-specific extension only in Mimic. Opaque pointer forwarding.

---

## 20. Build plan — runtime milestones

Dependency-ordered; interleaves with Forge/Mimic milestones from FORGE.md §29.6.

**M1 (weeks 1-2): Scaffolding**

- `include/crucible/ir002/` headers per FORGE.md §18 (JSON-driven recipe registry, RecipePool/TilePool, KernelGraph)
- `crucible/cpu/` Mimic CPU backend skeleton (OpenBLAS wrapper for GEMM at ≤FP32; pure C++26 reference-eager for the rest)
- Keeper daemon skeleton: starts, connects to config, exits cleanly
- Cipher hot-tier in-process implementation

**M2 (weeks 3-6): Single-node IR001→IR002→execute**

- Vessel-PyTorch adapter: mock-tensor capture working for forward pass of an MLP
- Forge Phase A-E scaffolding
- CPU backend emits + executes a native GEMM via Crucible runtime
- First end-to-end test: 2-layer MLP forward pass in PyTorch → Crucible → CPU → matches numpy reference

**M3 (weeks 7-10): Replay determinism on CPU**

- Philox RNG integration across all RNG consumers
- Content-addressed memory plan fully implemented
- TrainingCheckpoint format finalized
- Replay Determinism CI harness running (§10.8)
- First green test: bit_exact_single_backend_replay on CPU

**M4 (weeks 11-18): NV backend in Mimic + basic CNTP**

- Mimic-NV emitter generates cubin for a GEMM (leverages nvopen-tools RE)
- Mimic-NV runtime wraps `/dev/nvidia*` ioctls; loads + executes cubin
- Single-node Crucible + Forge + Mimic-NV end-to-end
- CNTP TCP-only transport for single-node → multi-node readiness
- Second green test: bit_exact_single_backend_replay on NV

**M5 (weeks 19-26): CNTP full stack**

- CNTP RDMA transport (libibverbs wrapper, QP management, MR registration)
- SWIM gossip (initially TCP, RDMA once verified)
- Raft-scoped consensus
- Ring collective over RDMA (first pattern; tree/HD later)
- eBPF + XDP for heartbeat fast path
- Canopy mesh forms across 2 nodes; Raft-commits membership
- Third green test: fleet_reshard_replay on 2 NV nodes

**M6 (weeks 27-34): k8s operator + elastic training**

- `CrucibleCluster` CRD + operator controller (Go)
- DaemonSet pattern; peer discovery via multicast
- Elastic scale-up / scale-down during training
- Graceful pod eviction + reshard
- Demo: 8-node Llama-2 7B training, kill 1 node mid-training, recover, verify replay-determinism

**M7 (weeks 35-44): AMD backend + cross-vendor CI**

- Mimic-AM emitter via LLVM-AMDGPU
- Mimic-AM runtime via KFD ioctls
- Mimic-AM collectives via XGMI + CNTP
- Cross-vendor numerics CI matrix (NV × AM × CPU for ~30 canonical recipes × ~20 kernel shapes)
- Fourth green test: bit_exact_cross_backend on NV × AM for BITEXACT recipes

**M8 (weeks 45-58): Trainium backend**

- Mimic-TRN emitter from RE'd Neuron ISA
- Mimic-TRN runtime from `/dev/neuron*` ioctls
- Mimic-TRN collectives over NeuronLink + EFA
- Tests extended to include trn2

**M9 (weeks 59-70): TPU backend**

- Mimic-TPU emitter from RE'd libtpu
- Mimic-TPU runtime from `/dev/accel*` ioctls
- ICI collective integration (including SHARP-equivalent in-fabric aggregation)
- Tests extended to include v5p / v6e

**M10 (weeks 71-78): Production hardening**

- Full Replay Determinism CI across all backends
- Time-travel debugging tools (`crucible replay`, `inspect`, `blame`, `top`)
- Plan-level observability (`crucible plan show/diff/bisect/validate/export`, §16.6)
- Genesis Kernel Pack CI for all supported chips
- `bit_exact_recovery_invariant` CI test (§17.8.5)
- Documentation polish + tutorials

**M11+ (weeks 79+): Ecosystem**

- JAX Vessel adapter
- Native Rust frontend
- Cerebras backend (optional)
- Federated cache sharing (L16 of CLAUDE.md)

**Parallel track — ExecutionPlan primitive (M3-M5)**

Pre-requisite for sub-μs dispatch target. Lands in stages:

- M3 end: ExecutionPlan struct + basic PatchPoint kinds (SCALAR, SLOT_PTR, SHAPE_DIM, RNG_COUNTER) on CPU backend
- M4 mid: NV pushbuffer composer (§15.4.1), static BAR1 setup (CRUCIBLE.md §14.8), first sub-μs dispatch on H100
- M4 end: CONDITIONAL + COUNTER_BUMP PatchPoints (LoopNode/BranchNode device-side jumps)
- M5 mid: ChainEdge semaphore pool + plan chaining (§J.5)
- M5 end: SEMAPHORE_VALUE + EVENT_TENSOR PatchPoints; first megakernel training step plan

**Parallel track — Performance levers (M5-M10)**

- M5: Bucketed async all-reduce (CNTP + recipe registry support)
- M6: Zero-bubble PP scheduling
- M6: Persistent-kernel multi-layer fusion (Forge Phase D LAYER FuseKind)
- M7: ExecutionAttrs warp-specialization + setmaxnreg emission (MIMIC.md §15.5)
- M7: Green contexts for role-split SM allocation (CRUCIBLE.md §14.9)
- M8: FP8 mixed precision + dynamic loss scaling
- M8: Hybrid search mode (MAP-Elites + real-hardware validation, FORGE.md §22.8)
- M9: Dynamic on-GPU scheduler escape valve (CRUCIBLE.md §3.9)
- M9: Asymmetric-fleet Z3 partitioning (FORGE.md §25.8)
- M10: MFU CI gate — NV-only Llama-70B 55-65% MFU target (FORGE.md §25.9.6)

**Parallel track — Driver integration (M4-M6)**

- M4: Upstream nvidia.ko + module params + `nvidia-persistenced` (§14.8 steps 1-5)
- M4: Aikitoria P2P patch for AD102/GA102 consumer SKUs (§17.7)
- M5: FLR recovery protocol (§17.8); target <2.5s
- M5: Mixed Blackwell CI test — RTX PRO 6000 + 5090 fleet (MIMIC.md §42.6)
- M6: AM-style full userspace driver (AMD; MIMIC.md §36.4.3)

Timeline: ~18 months for the full multi-vendor story. 6 months to a solid NV-only single-node MVP with provable replay determinism. 12 months to multi-node NV + AM + CPU with elastic training. The long poles are Trainium/TPU RE (depends on how much leakage from our intel), and the correctness validation (not code-writing).

**Critical-path dependencies**

- ExecutionPlan primitive (M3) unlocks everything downstream — patch-point-based mutation, plan chaining, sub-μs dispatch. Slipping this slips M5+ by equal amount.
- NV direct-SASS emission (M4) unlocks: direct-ISA perf wins, setmaxnreg, warp specialization. Blocked until nvopen-tools RE tables are production-ready (covered in Phase 4 of nvopen-tools project).
- CNTP RDMA transport (M5) unlocks: multi-node training, asymmetric fleets, production deployment. Blocked on libibverbs RE (trivial, well-documented).
- CPU ref oracle (M3) unlocks: BITEXACT_STRICT validation for every subsequent backend. Must ship first regardless of vendor priority.

---

## 21. Open questions deferred

1. **Python GIL contention** in mock-tensor capture at very high op rates (>100K ops/sec). Measurement needed to see if Vessel becomes a bottleneck in trace-heavy workloads. Mitigation: batch multiple ops into one GIL release window.

2. **Cross-run data-order determinism with shuffled data**. Shuffle seed is deterministic; but if user uses a shuffling pipeline that isn't Philox-seeded (e.g., their own `random.shuffle`), replay breaks. Policy: Crucible rejects non-Philox shuffling in pure-capture mode.

3. **L1 IR002 cache staleness across Crucible version bumps**. If we change IR002 semantics in a new release, old L1 entries become invalid. Policy: IR002 content hash includes `crucible_version`. Cache keys invalidate on version bump. Minor churn at release time.

4. **Federated cache poisoning**. If two Crucible installations share a cold-tier bucket and one has bit-exactness-violating kernels, the cache entries propagate. Mitigation: cross-vendor numerics CI runs as a validator; entries with failing CI are quarantined.

5. **Vessel compatibility with user code that uses `torch.no_grad()` / `torch.inference_mode()` / autograd disabling**. These change dispatch behavior. Need to verify our capture respects them.

6. **Data loader workers on non-CPU Keepers**. Can compute Keepers also run data loading if they're not saturated? Policy: yes, but only configurable per-workload; defaults separate the roles for simplicity.

7. **Eager-mode fallback performance**. When users opt into `with cr.eager_mode():`, how slow is it? Depends on how much we can leverage compiled kernels per-op. Probably 5-10× slower than captured mode; acceptable for debugging.

8. **k8s operator for mixed-cloud deployments**. Can a single `CrucibleCluster` span AWS + on-prem? Yes via VPN or Transit Gateway. Open: how do we measure bandwidth variations between regions and adjust partitioning?

9. **Live binary upgrade during training**. We described it (§14.5); it's untested at scale. Needs a large-cluster validation.

10. **Cold-tier storage format versioning**. If we change Cipher layout at version v2, how do v1 stored entries get migrated? Policy: eager rewrite on first access; background bulk migration in an idle window.

11. **Plan-patching concurrency protocol**. What happens if two Keepers attempt to patch the same Plan at the same time while a third is reading? Raft-committed ref-counting for semaphore pools is specified (§J.5.2); plan-mutation itself needs a similar story. Minor in practice — training runs don't hit it — but live reshard scenarios might.

12. **CNTP wire protocol byte-level spec**. §5 describes the five-layer stack semantically; message headers (magic, version, sequence, checksum), SWIM probe layout, Raft AppendEntries format, and CNTP-immediate signaling format are not yet byte-specified. Deferred until CNTP implementation starts (M5).

13. **Genesis Kernel Pack concrete content**. MIMIC.md §39 says "~500 canonical kernels per chip" but doesn't specify which 500. A concrete (kernel-family × shape × dtype) matrix is needed for the build plan.

14. **CLI surface specification**. `crucible plan show/diff/bisect/validate/export/revert`, `crucible recompile`, `crucible bucket extend`, `crucible-fleet --mixed-sku`, `crucible-driver` are referenced across sections but lack a unified CLI reference chapter.

15. **Dynamic scheduler atomic-ordering determinism vs byte-exact replay**. §3.9 commits to "input-deterministic replay" for dynamic-scheduled sections but not byte-deterministic across hardware atomic ordering. For strict-BITEXACT serving use cases, this is an opt-out; detection logic needs spec.

### 20.X Recently resolved (closed from backlog)

Items resolved by Pass 1-3 design commitments, moved here for traceability:

- ~~How does Crucible handle asymmetric fleets?~~ → FORGE.md §25.8 (STRICT_UNIFORM / CAPACITY_WEIGHTED / TIERED policies)
- ~~What's the FLR recovery time target?~~ → §17.8 (~1.5-2.5s with cached GSP firmware)
- ~~How do extension-point bodies inline?~~ → FORGE.md §18.7 + MIMIC.md §40.7
- ~~Sub-μs kernel dispatch achievable?~~ → §14.7 (120-200ns CPU critical path); MIMIC.md §36.5.4 byte sequence
- ~~How do we replace CUDA Graphs?~~ → ExecutionPlan primitive §11.9, PatchPoints FORGE.md §18.8
- ~~DeepSeek-style SM partitioning / warp specialization?~~ → Green contexts §14.9, ExecutionAttrs FORGE.md §18.3.1, setmaxnreg MIMIC.md §15.5
- ~~Event Tensor pattern integration?~~ → EVENT_TENSOR PatchPoint FORGE.md §18.8, per-vendor realization MIMIC.md §40.8
- ~~Full userspace driver feasibility on NV?~~ → hybrid model with nvidia.ko for GSP (§14.8, MIMIC.md §36.4.2)
- ~~Mixed consumer/datacenter Blackwell support?~~ → MIMIC.md §42.6
- ~~Real-hardware validation mode?~~ → HYBRID search mode FORGE.md §22.8, MIMIC.md §19

---

## 22. Glossary

**Canopy** — the fleet mesh. Union of all live Keepers in one cluster. No master; SWIM + Raft.

**Cipher** — three-tier content-addressed persistence. Hot (RAM), warm (NVMe), cold (S3).

**CNTP** — Crucible Native Transport Protocol. Five-layer stack replacing NCCL + RCCL + UCX + MPI + hcoll.

**CrucibleCluster** — k8s CRD for deploying a Crucible workload.

**CrucibleTensorImpl** — the mock tensor returned from every dispatched op. Full tensor interface, no real storage until materialized.

**DataNode** — IR001 op for data loading. Runs on CPU Relay, produces batches, DMAs to device.

**Elastic membership** — Canopy allows join/leave during a training run. Partitions reshape dynamically.

**Fleet intersection** — across current Canopy membership, the set of NumericalRecipes natively supported everywhere.

**Forge** — the vendor-agnostic compiler. See FORGE.md.

**Genesis Kernel Pack** — per-chip precompiled seed of canonical kernels, shipped with each installation.

**Hollow Vessel** — the capture pattern: mock tensors returned, no framework-side execution, Crucible owns everything below dispatch.

**Keeper** — per-Relay daemon. One per physical or virtual node.

**L1 / L2 / L3 KernelCache** — three-level content-addressed compile cache. L1 = IR002 snapshot (vendor-neutral); L2 = IR003* (per-vendor); L3 = compiled binary (per-chip).

**Mimic** — the per-vendor backend framework. See MIMIC.md.

**Mock tensor** — a `CrucibleTensorImpl` instance without real storage, returned during capture.

**NetworkOffload** — fifth CNTP plane for in-fabric aggregation (SHARP, ICI, XGMI, SwarmX).

**Philox** — counter-based RNG (Philox4x32). Stateless. Key to replay determinism.

**Raft** — consensus algorithm for critical decisions (topology, membership, Cipher commits). Scoped to decisions that must be atomic.

**Relay** — a physical compute or CPU node in Canopy. Hosts one Keeper.

**Replay determinism** — the invariant that training state is a pure function of `(weights, optimizer, cursor, seed)`.

**ReduceGroup** — a logical membership set for collective ops (DP, TP, PP, EP, CP, or user-defined).

**SWIM** — gossip-based membership protocol (Das, Gupta, Motivala 2002) + Lifeguard extensions.

**Sync point** — user action that forces materialization (`tensor.item()`, `print(t)`, etc.).

**TrainingCheckpoint** — the persisted state enabling replay: `(weights, optimizer, cursor, seed, step, epoch)`.

**Vessel** — frontend adapter. PyTorch-Vessel, JAX-Vessel, native Python / C++ / Rust frontends are peers.

**Extension point** — a named `ComputeBody*` field on an IR002 KernelNode's attrs struct, populated by user-supplied IR001 fragments (score_mod, mask_mod, stat_compute, normalize_fn, update_body, etc.). Inlines at IR002→IR003* lowering; fuses with structural kernel code via peephole. The generalization of FlashAttention's pattern to every kernel family that admits one.

**BITEXACT_TC / BITEXACT_STRICT** — two of the four NumericalRecipe determinism tiers (UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT). TC realizes via K≤8 tensor-core fragments plus pinned outer reduction (0-1 ULP cross-vendor, ~5-8% perf tax). STRICT uses scalar FMA throughout (0 ULP byte-identical cross-vendor, 10-50× slower).

**PagedKVCache** — per-inference-session runtime structure holding KV pages (typically 16 tokens each), page table mapping logical positions to physical pages, pool allocator for growth, optional prefix dedup via content-addressing.

**InferenceSession** — first-class session object for variable-context LLM serving. Owns KV cache, sampling state, token position; routes to prefill or decode plans based on call size; supports forking for beam search and speculative decoding.

**Zero-bubble PP** — DeepSeek V3-style pipeline scheduling that splits backward into activation-gradient and weight-gradient halves, scheduling the latter to fill forward bubbles. Reduces pipeline overhead to <2% on well-sized configurations.

**Bucketed specialization** — Forge Phase F.4 partitions each symbolic dim into log-spaced buckets and compiles one specialized kernel per bucket. Runtime dispatch is O(1) by bucket index; combines with parametric fallback for novel shapes.

**Ring attention / Context parallelism** — rotating K/V around a ring of GPUs while each holds a slice of Q. N-way parallelism for long context with O(N) communication rounds. Declared via `ATTENTION` with `algorithm=RING` attribute.

**MFU** — Model FLOPs Utilization. `achieved_flops / peak_flops` per step. The primary production-perf metric; Crucible targets 55-70% on H100 Llama-class training (baseline 30-40%) via the compounding levers in §12.6-12.10.

**ExecutionPlan** — the primary execution primitive. Pre-composed vendor-native command stream + memory plan + compiled kernel handles + patch points + guards + chain edges + content hash. Submitted via one doorbell write. See §11.9.

**PatchPoint** — a typed, named runtime-mutable value in an ExecutionPlan. Eight kinds: SCALAR, SLOT_PTR, SHAPE_DIM, RNG_COUNTER, CONDITIONAL, COUNTER_BUMP, SEMAPHORE_VALUE, EVENT_TENSOR. Patched via O(1) byte-width MMIO write; no recomposition. See FORGE.md §18.8.

**ChainEdge** — pinned-memory semaphore link between ExecutionPlans. Enables training-step sequencing without host round-trip. See §11.9.3, FORGE.md §J.5.

**SemaphorePool** — per-Keeper pinned-memory pool of 8-byte semaphore slots allocated at init, Raft-committed metadata, Cipher-persistent values. ~64-256 slots per Keeper.

**ExecutionAttrs** — 8-byte shared tail on every KernelKind attrs struct. Contains warp_spec_split, reg_alloc_policy, sm_mask_handle, per-role register counts. Enables DeepSeek-class warp specialization as first-class compile output. See FORGE.md §18.3.1.

**SearchMode** — enum in CompileConfig: MAP_ELITES (default, simulator fitness), HYBRID (MAP-Elites + real-hardware validation), BEAM (tinygrad-style real-hardware only, dev-only). See FORGE.md §22.8.

**EVENT_TENSOR** — PatchPoint kind for data-dependent task dependencies; multi-dim atomic counter array. Used for MoE routing, speculative decoding, iterative reasoning. Pattern borrowed from Jin et al. 2026. See FORGE.md §18.8, MIMIC.md §40.8.

**Green context** — NV Hopper+ hardware SM partitioning mechanism. Crucible allocates separate contexts for compute / dispatch / combine / scheduler roles; kernels in different contexts run on disjoint SMs without preemption. Implements the DeepSeek-V3 role-split pattern. See §14.9.

**setmaxnreg** — NV PTX instruction `setmaxnreg.{inc,dec}.sync.aligned.u32 N` that dynamically rebalances registers between warp-group roles. Producer warps release; consumer warps grab. Used by DeepSeek-V3 and emitted by Mimic-NV when `reg_alloc_policy=DYNAMIC_SETMAXNREG`. See MIMIC.md §15.5.

**warp_spec_split** — ExecutionAttrs field, 8 discrete values matching Mimic MAP-Elites behavior axis. Patterns like 1-0-0 (no spec), 2-2-0 (two producer, two consumer), 1-2-1 (producer-barrier-consumer), etc.

**Dynamic on-GPU scheduler** — escape-valve pattern for irregular control flow. A tiny scheduler CTA running push/pop on a global task queue; default static, opt-in dynamic. See §3.9.

**FLR** — Function Level Reset. PCIe-standard mechanism to reset a GPU without host reboot. Crucible target: ~1.5-2.5 s recovery including cached GSP firmware re-upload. See §17.8.

**Driver-gate bypass** — policy of enabling hardware-capable features regardless of SKU marketing tier. Applies BAR1 P2P / RDMA / L1 ASPM / clock pinning etc. across consumer + datacenter GPUs. See §17.7.

**CAPACITY_WEIGHTED partitioning** — Z3 policy for asymmetric fleets where work is proportional to each node's TFLOPs rating. Alternative to STRICT_UNIFORM (slowest dictates) and TIERED (anchor + worker roles). See FORGE.md §25.8.3.

**Pushbuffer** — per-vendor command stream of 4-dword kernel launches (NV Hopper: 16 bytes/launch via SEND_PCAS_A + SEND_SIGNALING_PCAS2_B). Pre-composed at Phase J, submitted via doorbell. See MIMIC.md §15.4.

**Hybrid userspace driver** — NVIDIA backend model: upstream nvidia.ko handles GSP boot + FLR; Crucible owns channel + pushbuffer + doorbell in userspace. See MIMIC.md §36.4.2.

**AM-style userspace driver** — AMD backend model (tinygrad-AM pattern): `rmmod amdgpu`, full vfio-pci userspace, bypass MES, bind directly to MEC at pipe=0/queue=0. See MIMIC.md §36.4.3.

**Static BAR1** — NVIDIA-specific mode (`RMForceStaticBar1=1`) installing a 2MB-aligned identity map of full VRAM into BAR1. After setup, BAR1 VA == FB physical offset; P2P + RDMA work at wire speed with zero per-op registration cost.

**Event Tensor** — the Jin et al. 2026 (MLSys) paper on megakernel compilation for dynamic GPU workloads. Crucible adopts its counter-based synchronization pattern via EVENT_TENSOR PatchPoint. Closest published prior art; 1.4× vs cuBLAS+NCCL. See FORGE.md §28.0.1.

**Mixed Blackwell fleet** — supported deployment: consumer 5090 + datacenter RTX PRO 6000 + B200 in one Canopy. Cross-SKU P2P at 55 GB/s unidirectional via BAR1. See MIMIC.md §42.6.

---

## Summary

Crucible is the runtime. Forge is the compiler. Mimic is the per-vendor backend framework. This document covers the runtime half — everything user-facing, everything fleet-orchestrating, everything persistence-guaranteeing.

The twelve pillars:

1. **Mock-tensor dispatch capture** — PyTorch's backend never runs; we own everything below dispatch from iteration 0.
2. **Canopy mesh** — no master node; SWIM + Raft; elastic membership; scheduler-agnostic (k8s, SLURM, systemd).
3. **CNTP** — zero-userspace-tax networking via RDMA + eBPF + shared-memory + NIC/switch offload; DC QPs beyond 128 peers; multi-rail per node; NUMA-pinned; Soft-RoCE fallback.
4. **Cipher** — three-tier content-addressed persistence; chunk-level dedup; federation-safe at the L1 IR002 layer.
5. **Replay determinism** — Philox + content-addressed memory plan + bit-exact per-vendor kernels + canonical reduction topology + four-tier determinism (UNORDERED / ORDERED / BITEXACT_TC / BITEXACT_STRICT).
6. **Frontend-agnostic** — PyTorch, JAX, native Python/C++/Rust as peers, each ~2K-LoC adapter over shared runtime.
7. **Research kernel primitives** — `cr.kernel.*` decorators + escape valves (compound / custom / raw) + control-flow primitives (`scan` / `while_loop` / `cond` / `scatter_sparse`) lowering to `LoopNode` and `BranchNode` in the Merkle DAG.
8. **MFU at 55-70% target** — zero-bubble PP, bucketed async all-reduce, persistent-kernel multi-layer fusion, FP8 mixed precision with dynamic scaling, MAP-Elites kernel search, direct-ISA emission, in-network offload, Z3 5D partition search — eight compounding levers.
9. **ExecutionPlan as the primitive** — pre-composed pushbuffer + PatchPoints + ChainEdges + guards + content hash. Submitted via one doorbell write. Sub-μs dispatch (120-200 ns CPU critical path). No CUDA-Graph-style recapture on shape changes; PatchPoints mutate in place. See §11.9.
10. **Userspace driver depth per vendor** — NV hybrid (nvidia.ko for GSP boot; userspace channel + pushbuffer + doorbell), AMD full-userspace (AM-style, rmmod amdgpu), TPU /dev/accel* direct, Trainium /dev/neuronN direct, CPU trivial. Driver-gate bypass unlocks P2P + RDMA on any SKU. See §14.8, §17.7, MIMIC.md §36.4.
11. **DeepSeek role-split + Event Tensor integration** — green contexts partition SMs per role (compute / dispatch / combine / scheduler); ExecutionAttrs.warp_spec_split + reg_alloc_policy expose DeepSeek's setmaxnreg-class warp specialization as first-class compile output; EVENT_TENSOR PatchPoint enables Jin et al.-style data-dependent synchronization for MoE and irregular workloads. See §14.9, §3.9.
12. **Asymmetric fleets as first-class target** — mixed consumer/datacenter (PRO 6000 + 5090), mixed generation (H100 + H200), mixed vendor (H100 + MI300X); Z3 partition solver with CAPACITY_WEIGHTED / STRICT_UNIFORM / TIERED policies; FLR recovery <2.5 s vs stock 5-10 s. See §17.8, FORGE.md §25.8.

~18 months to ship full multi-vendor with all eight properties. 6 months to a solid NV-only MVP. The code-writing is fast (agentic loops); the correctness-validation is the long pole (§10.8 Replay Determinism CI + §12.10 collective failure recovery are where most of the eventual engineering effort lives).

Crucible is a different category of tool from the ML frameworks it replaces. This document is its runtime design reference.

---

*End of Crucible runtime design document. Companion to FORGE.md (vendor-agnostic compiler) and MIMIC.md (per-vendor backend framework). Upstream overview in CLAUDE.md.*


