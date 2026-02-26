# The Crucible Runtime
*Adaptive ML infrastructure.*

Three layers: **Hardware** — compute nodes, heterogeneous and replaceable. **The Model** — weights and computation graphs. **Crucible** — the runtime that abstracts hardware, persists state across node failures, and migrates to new devices.

Python describes. Crucible executes. The 492,000 lines of framework overhead between them become unnecessary. There is no training or inference — there is only a model in Crucible.

## Ontology

| Name | Role | Description |
|------|------|-------------|
| **Relay** | Node | Compute node running a Crucible daemon. Replaceable. |
| **Keeper** | Daemon | Per-Relay daemon — self-healing, self-updating, autonomous. `crucible-keeper.service` starts at boot, discovers peers, joins mesh. |
| **Vigil** | Model | The model: DAG, weights, learned knowledge. Named for the Prothean AI. Always-on. |
| **Cipher** | State | Persistent state — DAG chain, weight snapshots, KernelCache, RNG state — survives node failure, restores on new hardware. One `uint64` master counter for deterministic Philox RNG. Event-sourced: mostly replay instructions, not raw state. |
| **Canopy** | Mesh | Mesh of Keepers — distributed awareness, gossip, consensus, self-healing. No master node. |
| **Vessel** | Interface | PyTorch — the 2,000+ ATen operators Crucible intercepts via the Dispatcher. Researchers write PyTorch; Crucible handles the rest transparently. |
| **Crucible** | System | The complete runtime system. |

---

## L0 — Hardware

**Compute hardware. Heterogeneous, replaceable.**

GPUs are ecosystems: tensor cores (1000 TFLOPS FP16 on H100), scalar ALUs (60 TFLOPS), four-level memory hierarchy (registers → shared memory → L2 → HBM), power envelopes. Gap between theoretical peak and achieved: 40-70%. A kernel in shared memory runs 10× faster than one spilling to HBM; tensor cores are 16× faster than scalar ALUs.

**Nobody observes this at runtime.** CUPTI exposes per-kernel counters: SM utilization, bandwidth saturation, occupancy, tensor core use, register spills, cache hits, warp stalls. These tell you EXACTLY why a kernel is slow. No ML framework reads them.

**Crucible reads CUPTI after every compiled kernel.** Mechanical diagnosis:
- Memory bandwidth >80% AND SM <60% → memory-bound → increase tile size
- Register spill >0 AND occupancy <50% → register pressure → use shared memory staging
- Tensor core utilization =0% on matmul → alignment issue → pad to multiples of 16
- Warp stalls on memory dependency → software-pipeline prefetch

Each diagnosis maps to a targeted fix, benchmarked and kept or discarded. CUPTI counters guide kernel optimization like a gradient. AMD equivalent: rocprofiler (CU utilization, VRAM bandwidth, LDS conflicts, wavefront occupancy) — abstracted so diagnosis logic is vendor-agnostic.

**Multi-vendor:** NVIDIA (sm_86/89/90/100), AMD (gfx1100/942), Intel XMX, Apple AMX, Google TPU MXU. Same computation described once in the Merkle DAG; different compiled kernels per (content_hash, device_capability).

**Power management:** NVML exposes clocks, power, temperature, ECC errors. Memory-bound phases don't need full core clock — drop 30% for zero perf loss, significant savings. Compute-bound: boost max. Over 1000 GPUs × 2 weeks: hundreds of thousands in electricity savings.

**Health monitoring → Keeper:** ECC error trends, thermal throttling, clock degradation feed into the Keeper. A failing GPU gets load-reduced, data pre-replicated to healthy Relays (L12 RAID). State is already replicated before failure completes. New hardware → fresh Keeper discovers mesh and Cipher → recompiles for new device → reshards for new topology → resumes exactly.

---

## L1 — Kernels

**Compiled computation. Templates instantiated per shape, per hardware.**

Current frameworks: static lookup (op + dtype → library kernel). Same kernel for 64×64 and 8192×8192, A100 and 3090, contiguous and transposed. No adaptation.

**Crucible generates kernels from C++ CUDA templates** parameterized by: tile size M/N/K, vectorization width, shared memory allocation, unrolling factor, precision (fp32/fp16/bf16/tf32/int8), access pattern. NVRTC compiles at runtime for exact shapes and hardware. For AMD: hipRTC with wavefront-adapted templates (64-wide vs 32-wide warps).

**CUPTI-informed autotuning** replaces random search. Each CUPTI diagnosis → one targeted variant → one benchmark → converge in 3-5 iterations vs 1000 random trials. The counters ARE the gradient of performance w.r.t. configuration.

**KernelCache:** maps (content_hash, device_capability) → CompiledKernel. Content-addressing: identical ops on identical shapes produce identical hashes. Reuse across iterations, runs, models sharing sub-computations, even organizations. Multiple variants coexist per hash; best selected per device, alternatives benchmarked during dead time. Cache grows monotonically across restarts. Lock-free open-addressing hash table — zero overhead on hot path.

**Stream parallelism:** DFG reveals independent ops → launch on different CUDA streams → concurrent SM execution. Schedule compiled statically from topological sort + earliest-start-time assignment. Zero scheduling overhead at runtime.

**Deterministic Philox RNG:** cuRAND is hardware-dependent — different sequences on H100 vs 3090. Crucible uses Philox4x32: counter-based, platform-independent, stateless. Each op derives key from `hash(master_counter, op_index, content_hash)`. Each thread: `philox(thread_idx, op_key)` — ~10 integer instructions in registers. For memory-bound kernels like dropout: runs free in otherwise-wasted ALU cycles. Same (counter, key) → same bits on any architecture.

**Kernel fusion:** adjacent ops with single producer-consumer chain fuse into one kernel keeping intermediates in registers/shared memory, eliminating HBM round trips. Decision from DFG topology at compile time.

KernelCache is part of the **Cipher** — write-once, persists across reincarnations.

---

## L2 — Memory

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

## L3 — Operations

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

No training/inference distinction at L3 — same fallback, same recording, same compiled execution.

---

## L4 — Tensors

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

## L5 — Graphs

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

## L6 — The Merkle DAG

**Content-addressable, versioned computation graph.**

Central data structure. L0-L5 feed in, L7-L14 read/modify. Simultaneously: computation specification, compilation cache key, guard system, versioning mechanism, and deployment artifact.

**RegionNodes:** compilable op sequences. **content_hash** = hash(schema_hashes, input shapes/strides/dtypes/devices, scalar values). Identical computation → identical hash, even across models. **merkle_hash** = content_hash + child hashes → O(1) equality for entire subtrees (like git commits).

**BranchNodes:** dynamic behavior. Guard = the op sequence itself. Mismatch at op N → branch arms for different paths. Both arms independently compilable. Shared suffixes share content_hashes and kernels.

BranchNodes are THE mechanism for everything that changes: architecture mutation (L9), attention replacement (L8), hyperparameter changes (L10), continuous learning (L13). Every adaptation is a branch. Every branch is versioned and rollbackable.

**KernelCache:** (content_hash, device_capability) → CompiledKernel. Lock-free reads. Persists across runs, models, organizations. The **computation genome** — every run enriches it.

**Atomic swaps:** background thread builds new DAG structures → one atomic pointer swap at iteration boundary → zero-downtime activation. Same mechanism for compilation activation, branch swaps, memory plan updates, topology changes, rollbacks. Coordinated across Canopy at same iteration boundary.

**The DAG IS the Vigil.** No torch.export(), ONNX, TorchScript. Same DAG trains and serves. Deploy = copy Cipher to Relay.

**The DAG IS the audit trail.** Root merkle_hash captures entire computation state. Divergence found in O(log N) by walking tree. Cryptographic provenance for regulatory compliance.

**Git operations on models:** diff (which regions changed), merge (non-overlapping clean, overlapping = conflict), bisect (binary search through versions for regression), cherry-pick (select specific region updates), blame (trace value through version history).

**LoopNodes in the DAG:** cycle semantics within acyclic hash framework. `merkle_hash = hash(body.content_hash ⊕ "loop" ⊕ feedback_signature ⊕ termination)`. Transforms DAG from computation snapshot to computation PROGRAM. Entire training run = one compact cyclic graph.

---

## L7 — Tokens

**Input granularity. Matching compute to information density.**

Fixed tokenization violates information theory. A blank wall gets same compute as a circuit diagram. Shannon: minimum bits = entropy. Crucible observes information density at runtime:

**Token merging (proven):** pairwise cosine similarity between adjacent representations after layer N. Similarity > threshold → merge (average). 40-50% reduction, <0.5% accuracy loss (Bolya et al. 2023). Crucible makes it **adaptive per-input per-layer** — ocean photo merges 80% at layer 2; circuit diagram merges 5%. Attention is O(n²), so 4× fewer tokens = 16× less attention. DAG BranchNode for merge/no-merge.

**Early exit per token (proven):** measure ||h_N - h_{N-1}|| per token. Below threshold → freeze, skip remaining layers. Bucket tokens into convergence groups for batch efficiency. Average tokens converge around layer 4-6: 50-60% compute savings.

**Adaptive patching (images):** quadtree decomposition by information content (gradient magnitude, frequency, entropy). Rock photo → 8-16 tokens. Blueprint → 256-512. Compiled as a LoopNode.

**Variable-length batching:** pack sequences contiguously, compile kernels for ragged shapes with known offsets. Complexity hidden below the model.

**Per-token precision:** high-information tokens in FP16, low-information in INT8/INT4. Separate kernels per precision group.

**Extensions:** video (delta frames like H.264), audio (coarse for silence, fine for transients), time series (one token for flat, many for spikes).

---

## L8 — Layers

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

## L9 — Models

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

## L10 — Training

**Hessian, meta-gradients, curriculum, and self-tuning.**

The entire training loop (forward + backward + optimizer) is in the DAG → observable, differentiable, modifiable.

**Meta-gradients:** lr → θ_{t+1} → val_loss. Compute ∂(val_loss)/∂(lr) via one additional backward pass. Same for weight_decay, β₁, β₂, ε. Hyperparameters tune themselves by gradient descent on validation loss. No grid/random/Bayesian search.

**Per-layer LR from curvature:** Hessian diagonal gives optimal lr ∝ 1/H_ii. Hybrid: Hessian for periodic calibration, Adam for step-to-step adaptation.

**K-FAC natural gradient:** F ≈ A⊗G per layer, tractable inverse. Steepest descent in distribution space. 2-3× fewer steps, ~2× cost/step. Activated where SNR is moderate.

**Curriculum learning:** per-sample loss observable → order by difficulty. Try random/hard-first/easy→hard for 100 steps each → keep best → re-evaluate. 20-40% faster convergence.

**Loss function evolution:** add/weight auxiliary losses, regularization terms. Meta-gradients on term weights. **Optimizer evolution:** Adam/AdaFactor/Lion/learned-update-rule as DAG branches → measure → keep best. Logical endpoint: optimizer IS a learned function trained by meta-gradients.

**Automatic mixed precision from measurement:** run each op in FP32 and FP16/BF16/TF32/INT8/FP8, measure per-op difference, pick cheapest precision maintaining quality. Per-model, per-op, per-training-stage. Not a static allow-list.

---

## L11 — Data

**Pipeline absorption, augmentation, and steering.**

Crucible dissolves the boundary between data loading and training.

**Backpressure:** measure GPU idle between iterations → signal DataLoader to prefetch more/less. **GPU-side augmentation:** tensor ops (crop, flip, jitter, blur) moved to GPU as compiled DAG ops. ~500μs CPU → ~5μs GPU. **Curriculum integration:** L10 measures difficulty → L11 reorders data stream.

**Manifold Mixup:** interpolate hidden states at layer K: h_mix = α·h_A + (1-α)·h_B → forward remainder → loss against interpolated label. Layer K chosen by linear probe accuracy. DAG modification at L6.

**Representation steering at inference:** add α × direction_vector to hidden state at optimal layer. No weight changes. Direction discovery automated: difference of means between desired/undesired behavior activations.

**Distribution shift monitoring:** KL divergence between current activations and training reference → trigger continuous learning (L13) or alert.

---

## L12 — Distribution

**Distributed mesh. Multiple nodes, shared state, no master.**

**Keeper mesh:** each Relay runs a Keeper, discovers peers via gossip. No master. Raft for critical state, CRDTs for eventually-consistent metrics. Any Keeper can propose changes.

**Spot-aware:** 30-second eviction → Keeper signals Canopy → mesh reshards to N-1 (redundant copies already exist) → Vigil continues from same step. New instance → Keeper discovers Canopy → loads Cipher → joins.

**Heterogeneous compute:** same Vigil, per-Relay compiled kernels (H100/3090/MI300X/A100 each optimized via L0+L1). Content-addressing handles naturally.

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

## L13 — Lifecycle

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

## L14 — Ecosystem

**Cross-run learning, computation genome, federated intelligence.**

**Computation genome:** shared KernelCache. Content-addressing means different models with same sub-computations hit the same kernels. Every training run enriches the cache. Network effects: value grows superlinearly with contributors. Docker Hub for GPU computation.

**Federated learning:** DiLoCo + differential privacy. Sites train locally, send noised pseudo-gradients (sensitivity bounded by known gradient norms). No raw data/gradients leave sites. Auditable via Merkle trail.

**Cross-Vigil transfer:** import DAG subgraphs between Vigils. Same content_hashes → zero compilation. Pre-trained weights load into imported regions. Components become reusable libraries — not just weights but self-describing computation fragments.

**Model marketplace:** content-addressed DAG fragments + KernelCache + verified quality metrics. Download, splice, verify, commit. Architectures from best-in-class components discovered ecosystem-wide.

**Hardware co-design:** aggregated KernelCache reveals real workload patterns (shape distributions, sparsity patterns, bottleneck frequencies). Feed to hardware designers → next-gen silicon optimized for actual workloads → Crucible autotuned for → new data → co-evolution.

**What Crucible is not:** not intelligent, not AGI. A matmul is a matmul. It observes, compiles, adapts, distributes, heals, persists, evolves — mechanically, from measurements. The model determines the quality ceiling. Crucible removes infrastructure overhead so the model can reach its potential.


  Updated Ontology

  Name: Relay
  Role: Node
  Description: Compute node running a Crucible daemon. Replaceable.
  ────────────────────────────────────────
  Name: Keeper
  Role: Daemon
  Description: Per-Relay daemon — self-healing, self-updating, autonomous. Executes Augur's recommendations.
  ────────────────────────────────────────
  Name: Vigil
  Role: Model
  Description: The model: DAG, weights, learned knowledge. Named for the Prothean AI. Always-on.
  ────────────────────────────────────────
  Name: Cipher
  Role: State
  Description: Persistent state — DAG chain, weight snapshots, KernelCache, RNG state, proof certificates. Event-sourced. Survives node failure, restores on new hardware.
  ────────────────────────────────────────
  Name: Canopy
  Role: Mesh
  Description: Mesh of Keepers — distributed awareness, gossip, consensus, self-healing. No master.
  ────────────────────────────────────────
  Name: Vessel
  Role: Interface
  Description: PyTorch — the 2,000+ ATen operators Crucible intercepts via the Dispatcher.
  ────────────────────────────────────────
  Name: Axiom
  Role: Verification
  Description: Build-time formal verification. Z3 universal proofs. consteval bounded proofs. Reflection structural proofs. Type-system boundary proofs. Proof-guided optimization. Every claim carries a certificate.
  ────────────────────────────────────────
  Name: Meridian
  Role: Calibration
  Description: Startup calibration. Measured hardware characteristics. Z3-optimal topology, parallelism, communication, placement. Re-solves on topology change. Inherits Axiom's universal proofs as axioms.
  ────────────────────────────────────────
  Name: Augur
  Role: Monitoring
  Description: Continuous prediction, monitoring, model analysis. Digital twin. Loss landscape analysis. Convergence bounds. Scaling laws. Bottleneck diagnosis. Recommendations engine. Proved bounds where possible, calibrated models elsewhere.
  ────────────────────────────────────────
  Name: Crucible
  Role: System
  Description: The complete runtime system.

  ---
  L15 — Axiom (Build-time Formal Verification)

  Four-layer proof architecture. Each layer proves what the layers below cannot:

  Layer 4 — Z3 SMT: universal proofs (∀x. P(x)) via custom Z3 fork (+CaDiCaL, 85 commits, +13.7K lines)
  Layer 3 — consteval: bounded verification, UB detection, model checking (compiler as verifier)
  Layer 2 — reflection: structural completeness (GCC 16 -freflection, P2996/P1306)
  Layer 1 — type system: fx::Alloc/IO/Block effect tokens, FgTag/BgTag phantom types, typestate mode transitions, refinement types (NonZero, InRange), strong ID wrappers

  Z3 proof targets (CMake custom_target crucible_verify, build fails if any theorem fails):
  - Arena alignment: ∀ base,offset,align ∈ [0,2^64). UNSAT in 0.03s
  - fmix64 no fixed points: ∀ x ∈ [0,2^64). UNSAT in 0.8s
  - fmix64 avalanche ≥20 bits: 64× UNSAT in 12s
  - SPSC bitmask==modulo, invariant preservation, no index collision: ∀ head,tail. UNSAT in ms
  - mul_sat/add_sat correctness: ∀ a,b ∈ [0,2^64). UNSAT in 0.1s
  - Sweep-line non-overlap N=2..16: ∀ lifetime configs. 15× UNSAT in 4s
  - Kernel configs: no bank conflicts, coalesced access, no OOB for ∀ (blockIdx,threadIdx)
  - Algebraic laws: hash monoid, ScalarType lattice, gradient commutative monoid

  Z3 as optimizer (not just verifier):
  - Memory plan: Z3 optimize minimizes total footprint; heuristic must be ≤1.15× optimal
  - Kernel configs: Z3 finds optimal (tile_M/N/K, pipeline_stages, warps) per shape in 2-5s
  - Topology: optimal TP×DP×PP×EP×CP factorization as joint constraint problem
  - Instruction scheduling: LLVM MC disassembly → Z3 finds minimum makespan per hot function

  consteval verification:
  - Arena dual-mode via `if consteval` — compiler tracks every allocation at compile time
  - Philox-driven fuzzing: 500 random memory plans, 500 toposorts, 100 serde roundtrips
  - SPSC model checking: exhaustive BFS over 81 states (3×3×9), proves deadlock-free
  - Mode transition completeness: 4 states × 3 events = 12 transitions, no livelock

  Reflection (GCC 16): per-struct static_assert checks NSDMI, padding, type safety, sizeof.
  Auto-generates: reflect_hash, reflect_serialize, reflect_compare, reflect_print.
  Cross-checks hand-written vs reflected for 100 random instances; divergence = build failure.

  Proof certificates: build manifest with cryptographic hash of all theorems. Stored in Cipher.
  Compositional: DAG fragment proofs compose when spliced. Content-addressing enables reuse.
  Gap: memory ordering (acquire/release) requires TSan at runtime — no compile-time proof possible.

  ---
  L16 — Meridian (Hardware Calibration + Topology Optimization)

  Runs at startup (5-15s) and on topology changes. Inherits Axiom's universal proofs as axioms.

  Calibration protocol (4 phases, ~10s total):
  1. GPU profiling (2s): GEMM→actual TFLOPS, streaming copy→HBM/PCIe BW, NVML→power/temp/ECC/memory
     → GPUProfile { actual_tflops, actual_hbm_bw, sustained_clock, power_watts, usable_memory }
  2. Network probing (3s): N×N ping-pong→latency, flood→bandwidth, topology detection (NVSwitch/NVLink/PCIe/IB/RoCE)
     → LinkProfile links[N][N]
  3. Kernel calibration (5s): top-20 kernels × 5 shapes → correction factors vs roofline prediction (±3-5% after)
  4. Z3 topology optimization: single joint query over ALL feasible configurations

  Z3 solves jointly: TP×DP×PP factorization, GPU-to-group placement (max intra-group BW), per-collective communication algorithm (ring/tree/halving per message size), gradient bucket sizes, per-tensor activation checkpointing, per-op mixed precision (FP32/TF32/BF16/FP16/FP8), batch size.

  Output: MeridianConfig — all decisions proven optimal for measured hardware. Re-probes on Relay failure/addition.

  ---
  L17 — Augur (Continuous Monitoring + Model Analysis)

  Runs every iteration. Three time scales: Axiom=build-time, Meridian=startup, Augur=per-iteration.

  Digital twin: DAG + Axiom kernel predictions + Meridian corrections → complete iteration prediction (±5-10%).
  - Per-kernel: roofline × correction × wave quantization. Bottleneck classification: COMPUTE/MEMORY/LAUNCH.
  - Per-iteration: critical-path compute + exposed communication + pipeline bubble.
  - Memory timeline: EXACT from Axiom's plan (zero fragmentation).
  - What-if engine: change batch/TP/hardware/model-size → instant re-prediction, no GPU needed.

  Continuous monitoring: predicted vs actual per iteration. >10% drift → diagnose (thermal throttle, ECC errors, network, contention) → trigger Meridian recalibration.

  Bottleneck identification: COMPUTE / MEMORY_BW / COMMUNICATION / BUBBLE / IMBALANCE.
  Recommendations ranked by expected_speedup × confidence, tagged auto-hot/auto-cold/manual.

  Model analysis (periodic, from recorded tensor data):
  - Hessian spectrum: Lanczos iteration (10 Hv products), condition number κ=L/μ, optimal lr=2/(L+μ)
  - Gradient health: per-layer SNR, Jacobian σ_max, vanishing/exploding detection
  - Representation capacity: effective rank via randomized SVD, dead neuron detection
  - Layer redundancy: CKA between adjacent layers, provable output-change bound
  - Convergence prediction: exponential fit L(t) = L* + (L₀-L*)·exp(-t/τ)
  - Scaling laws: Chinchilla fit L(N,D) = E + A/N^α + B/D^β, Z3 optimizes N*,D* for budget C

  ---
  Updated Layer Summary

  L17  Augur            prediction, monitoring, model intelligence, recommendations
  L16  Meridian         calibration, topology optimization, configuration
  L15  Axiom            Z3 proofs, consteval, reflection, effects, proof certificates
  ────────────────────────────────────────────────────────────────────────────────
  L14  Ecosystem        computation genome, federated learning, hardware co-design
  L13  Lifecycle        Cipher persistence, reincarnation, deterministic replay
  L12  Distribution     Canopy, Relays, no master, RAID, DiLoCo, 5D parallelism
  L11  Data             pipeline absorption, curriculum, latent augmentation
  L10  Training         meta-gradients, Hessian, K-FAC, curriculum, optimizer evolution
  L9   Models           growing, pruning, width mutation, composition, live surgery
  L8   Layers           attention replacement, local losses, per-layer gradient strategy
  L7   Tokens           merging, early exit, adaptive patching, per-token precision
  L6   Merkle DAG       specification, branches, guards, LoopNodes, atomic swaps
  L5   Graphs           CSR property graph, DFG/alias edges, LoopNodes, deterministic order
  L4   Tensors          shadow handles, TensorMeta, latent space observation, provenance
  L3   Operations       Vessel dispatch interception, recording, event sourcing, divergence
  L2   Memory           static plans, OOM impossible, arena allocation, per-Relay budgets
  L1   Kernels          template codegen, CUPTI autotuning, KernelCache, Philox, streams
  L0   Hardware         Relays, CUPTI profiling, multi-vendor, health → Keeper

  ---
  Development Plan

  Phase 1: Foundation (DONE — 9.5K lines, 24 tests, Clang 22 + GCC 15)

  L3 Operations: TraceRing SPSC, MetaLog, recording pipeline. L5 Graphs: TraceGraph CSR. L6 Merkle DAG: RegionNode, BranchNode, content/merkle hashing. L2 Memory: MemoryPlan sweep-line, PoolAllocator. L3/L6 Compiled Tier 1: ReplayEngine,
  CrucibleContext, dispatch_op, divergence recovery. L1 Kernels: CKernel 146-op taxonomy. L13: Serialize/Deserialize, Cipher. L5 Graph IR: Graph.h, ExprPool, SymbolTable. L15 partial: Effects.h (fx::Alloc/IO/Block), Reflect.h (reflect_hash,
  reflect_print). Vessel: PyTorch adapter.

  Phase 2: Axiom Core (NEXT)

  Goal: The four-layer proof architecture, fully operational.

  Layer 4 (Z3):
  - verify/ directory with crucible_verify.cpp linking libz3.
  - Proof suites: prove_arena.cpp (alignment, bump allocator), prove_hash.cpp (fmix64 no fixed points, avalanche ≥20 bits, determinism), prove_ring.cpp (SPSC bitmask==modulo, invariant preservation, no index collision, linearizability),
  prove_arithmetic.cpp (mul_sat, add_sat correctness), prove_memory_plan.cpp (sweep-line non-overlap for N=2..16, optimality comparison).
  - Z3 optimization: memory plan (Z3-optimal vs heuristic, build fails if gap >15%), kernel configs (GEMM, attention, elementwise — Z3-optimal tile/stages/warps for representative shapes).
  - Z3 protocol: mode transition guards correct for all hash values.
  - Z3 communication: ring all-reduce correctness, FSDP shard coverage.
  - CMake integration: add_custom_target(verify), all targets depend on proofs passing.

  Layer 3 (consteval):
  - constexpr Arena via if consteval (compiler-tracked allocation).
  - consteval algorithm verification: memory plan (500 random), toposort (500 random), serialization roundtrip, hash determinism, CSR consistency.
  - consteval model checking: SPSC deadlock freedom (exhaustive BFS over 81 states), mode transition completeness.
  - constexpr Philox4x32 for deterministic random generation at compile time.

  Layer 2 (reflection):
  - Expand Reflect.h: verify_init_safe<T>(), verify_type_safe<T>(), verify_layout<T>(expected_sizeof), verify_hash_completeness<T>() (reflect_hash vs hand-written).
  - Auto-serde: reflect_serialize<T>(), reflect_deserialize<T>().
  - define_class() for auto-SoA generation and optimal physical layouts.
  - Registration: static_assert(verify_struct<RegionNode>({.expected_size = 184})) for every layout-critical struct.

  Layer 1 (types):
  - Complete fx::IO and fx::Block wiring through all effectful functions.
  - Thread-affinity phantom types: FgTag, BgTag, TraceRingHandle<Tag>.
  - State-machine typestate: Inactive, Recording, Compiled, Diverged types for mode transitions.
  - Refinement types: NonZero, InRange, Positive for critical parameters.
  - Algebraic concepts: Lattice, Monoid, Semiring, Functor — constrained templates + consteval law proofs + Z3 universal proofs where encodable.

  Proof certificates: build manifest with cryptographic hash of all proved theorems.

  Phase 3: Meridian

  Goal: Hardware calibration + Z3-optimal topology.

  - GPU profiling protocol: GEMM/copy benchmarks, NVML queries, sustained clock measurement. 2s parallel.
  - Network probing: N×N latency/bandwidth matrix, topology detection. 3s parallel.
  - Kernel calibration: top-20 kernels × 5 shapes → correction factors. 5s.
  - Z3 topology solver: optimal TP×DP×PP factorization, GPU placement, communication algorithms, gradient bucketing, activation checkpointing, mixed precision, batch size — all jointly.
  - Keeper integration: Meridian produces config, Keeper applies it.
  - Re-probe on topology change: Relay failure, new Relay, Augur drift detection.
  - Calibration data → Augur: measured profiles feed the digital twin.

  Phase 4: Augur

  Goal: Digital twin + continuous monitoring + model intelligence.

  - Digital twin: DAG + Axiom kernel predictions + Meridian corrections → complete iteration prediction.
  - Per-kernel, per-iteration, per-run metrics. What-if engine. Hardware comparison.
  - Continuous monitoring: predicted vs actual, drift detection, diagnosis, recalibration trigger.
  - Bottleneck identification: COMPUTE/MEMORY_BW/COMMUNICATION/BUBBLE/IMBALANCE.
  - Recommendations engine: ranked by impact, auto-applicable flag, side-effect analysis.
  - Model intelligence: Hessian spectrum (Lanczos), gradient health (Jacobian SVs), effective rank (randomized SVD), layer redundancy (CKA), dead neurons, convergence prediction (exponential fit), scaling law analysis (Chinchilla fit + Z3
  optimization).
  - Integration: Augur advises → Keeper acts → Meridian re-solves if needed → Axiom re-proves if needed.

  Phase 5: Compiled Tier 2-3

  Goal: Shadow handles (~2ns/op) and CUDA Graph replay (~50ns/iteration).

  - Shadow handles: ConductorTensorImpl with metadata pointing into PoolAllocator.
  - Batched kernel launch: accumulate compiled kernels, one stream submission.
  - CUDA Graph capture: record compiled kernels, replay at ~50ns/iteration.
  - Integration: Z3-proved kernel configs compiled into the graph. Meridian's stream assignment from topology-aware scheduling.

  Phase 6: Keeper + Canopy + Cipher

  Goal: Distributed, self-healing, persistent, proof-carrying.

  - Keeper daemon: systemd service, health monitoring, self-updating. Executes Augur's recommendations.
  - Canopy mesh: gossip protocol, Raft consensus, peer discovery. No master.
  - Cipher: hot tier (other Relays' RAM from RAID redundancy), warm tier (NVMe per Relay), cold tier (S3/GCS). Event-sourced: DAG chain + periodic snapshots → deterministic recovery.
  - Proof certificates in Cipher: universal proofs (hash, protocol, arithmetic) survive reincarnation. Hardware-specific proofs (kernel configs, topology) re-proved by Meridian on new hardware.
  - Compositional proofs: DAG splicing inherits sub-DAG proof certificates.

  Phase 7: L7-L11 Intelligence

  Goal: Model-aware optimizations, guided by Augur, verified by Axiom.

  - L7: Token merging (adaptive per-input per-layer from Augur's representation analysis), early exit (per-token convergence from measured layer-to-layer difference), adaptive patching (quadtree by information content).
  - L8: Attention head classification (from Augur's measured attention patterns), local losses (per-layer learning signal), per-layer gradient strategy (from Augur's gradient health + Hessian spectrum).
  - L9: Layer growing/pruning (from Augur's capacity analysis + CKA), width mutation (from Augur's effective rank), architecture evolution.
  - L10: Meta-gradients (∂val_loss/∂lr), per-layer LR from curvature (Augur's Hessian), optimizer evolution.
  - L11: Curriculum learning (from per-sample loss), manifold mixup (at Augur-selected optimal layer).
  - All optimizations are DAG branches (L6). Axiom Z3-verifies new branches before activation. Augur predicts expected improvement before activation. The Keeper activates via atomic swap only if both Axiom approves (proved safe) and Augur approves
  (predicted improvement > threshold).

  ---
  Axiom verifies correctness at build time. Meridian calibrates hardware at startup. Augur monitors continuously at runtime. The Keeper executes based on proved-optimal decisions. The Vigil runs within verified infrastructure. The Cipher persists everything, including proof certificates — ready to restore and re-verify on any new hardware.

---

## The Layers

```
L14  Ecosystem       computation genome, federated learning, hardware co-design
L13  Lifecycle        Cipher persistence, reincarnation, deterministic replay, self-updating
L12  Distribution     Canopy, Relays, no master, RAID, DiLoCo, 5D parallelism
L11  Data             pipeline absorption, curriculum, latent augmentation, steering
L10  Training         meta-gradients, Hessian, K-FAC, curriculum, optimizer evolution
L9   Models           growing, pruning, width mutation, composition, live surgery
L8   Layers           attention replacement, local losses, per-layer gradient strategy
L7   Tokens           merging, early exit, adaptive patching, per-token precision
L6   Merkle DAG       specification, branches, guards, LoopNodes, atomic swaps
L5   Graphs           CSR property graph, DFG/alias edges, LoopNodes, deterministic order
L4   Tensors          shadow handles, TensorMeta, latent space observation, provenance
L3   Operations       Vessel dispatch interception, recording, event sourcing, divergence
L2   Memory           static plans, OOM impossible, arena allocation, per-Relay budgets
L1   Kernels          template codegen, CUPTI autotuning, KernelCache, Philox, streams
L0   Hardware         Relays, CUPTI profiling, multi-vendor, health → Keeper
```

Vessel intercepts PyTorch ops. Keeper manages each node. Vigil holds the model. Cipher persists state. Canopy coordinates the mesh. Relay provides compute.
# Crucible C++26 Code Rules

*Dual-threaded: foreground records ~5ns/op, background builds/compiles/replays. Foreground never stalls.*

## Toolchain

| Preset    | Compiler                  | Stdlib       | Role                                     |
|-----------|---------------------------|--------------|------------------------------------------|
| `default` | Clang 22.1.0 + libc++ 22  | libc++ 22    | Primary dev. Best diagnostics.           |
| `release` | Clang 22.1.0 + libc++ 22  | libc++ 22    | Production. `-O3 -march=native -DNDEBUG` |
| `gcc`     | GCC 15.2.1                | libstdc++ 15 | Conservative fallback.                   |
| `gcc16`   | GCC 16.0.1 (rawhide)      | libstdc++ 16 | Reflection, `inplace_vector`, expansion. |

Core headers (`include/crucible/`) compile clean on **all three** with zero warnings. Compiler-specific features behind `#ifdef` or non-header code only.

---

## The Eight Safety Axioms

Check ALL EIGHT on EVERY change. No exceptions. No "I'll fix it later."

### Compile-Enforced

**1. InitSafe** — `read(v) → initialized(v)` — Every field has NSDMI. Padding = `uint8_t pad[N]{}`. Arena returns garbage; NSDMI catches it at zero cost.
```cpp
struct TensorSlot {
  uint64_t offset_bytes = 0;              // NSDMI on every field
  SlotId slot_id;                         // default ctor = UINT32_MAX
  ScalarType dtype = ScalarType::Undefined;
  uint8_t pad[3]{};                       // zero-init padding
};
```

**2. TypeSafe** — `(⊢ e : T) → eval(e) : T` — Every semantic value is a strong type. `CRUCIBLE_STRONG_ID(Name)` → `explicit(uint32_t)`, `.raw()`, `.none()`, `<=>`, no arithmetic, zero-cost codegen. IDs: `OpIndex`, `SlotId`, `NodeId`, `SymbolId`, `MetaIndex`. Enums: `enum class` + `std::to_underlying()`. Bitcast: `std::bit_cast<T>()`. Dtypes in structs: `ScalarType`, never `int8_t`.
```cpp
void connect(OpIndex src, OpIndex dst, SlotId slot);      // CORRECT — type-safe
void connect(uint32_t src, uint32_t dst, uint32_t slot);  // WRONG — silent swap
```

**3. NullSafe** — `deref(v) → v ≠ null` — Pointer+count → `std::span` accessor (`span(nullptr,0)` = valid empty). `[[nodiscard]]` on every query. OOM → `abort()`. Debug bounds: `span::at()` (Clang 22).
```cpp
[[nodiscard]] std::span<const TensorMeta> input_span() const {
  return {input_metas, num_inputs};
}
```

**4. MemSafe** — `free(v) → ¬live(v)` — All graph/DAG memory → Arena (bump pointer, ~2ns, no fragmentation, no UAF). No `new`/`delete`. No `shared_ptr`. `= delete("reason")` for non-copyable. `static_assert(sizeof(T) == N)` on layout-critical structs. `std::mul_sat`/`add_sat` for size math.
```cpp
auto* node = arena.alloc_obj<RegionNode>();                      // CORRECT
Arena(const Arena&) = delete("interior pointers would dangle");  // CORRECT with reason
```

### Discipline-Enforced

**5. BorrowSafe** — no aliased mutation. SPSC = one writer, one reader. Document ownership. No shared mutable state except through atomics.

**6. ThreadSafe** — fg owns ring head + MetaLog head. bg owns ring tail + MetaLog tail. Cross-thread: atomic acquire/release only. See **Concurrency**.

**7. LeakSafe** — Arena bulk-frees graph memory. `std::unique_ptr` for ring/MetaLog buffers. `bg_` thread member declared LAST (destroyed first → joins before other members die).

**8. DetSafe** — `same(inputs) → same(outputs)`. DAG fixes execution order. Memory plan fixes addresses. Philox4x32 fixes RNG (counter-based, platform-independent). KernelCache is content-addressed. Bit-identical across hardware.

---

## Concurrency — Two Threads, Spin Only

```
Foreground (hot):  record ops at ~5ns each via TraceRing
Background (warm): drain ring, build TraceGraph, DAG, memory plan, compile
```

Communication: SPSC ring buffers only. Cross-thread shared state:
- `KernelCache` — lock-free CAS on atomic slots (bg writes, fg reads)
- `RegionNode::compiled` — atomic pointer swap (bg writes, fg reads)

**Memory ordering — acquire/release ONLY, never relaxed:**
```cpp
atomic_var.store(value, std::memory_order_release);                       // store
while (atomic_var.load(std::memory_order_acquire) != expected) {          // load
    CRUCIBLE_SPIN_PAUSE;  // _mm_pause — 10-40ns via MESI invalidation
}
atomic_var.compare_exchange_strong(exp, des, std::memory_order_acq_rel);  // RMW
```
Same-thread reads of own variable: sequenced, acquire still preferred. `relaxed` = ARM reordering = data race; works on x86 by accident until optimizer breaks it. acquire/release on x86 = same MOV instructions.

**BANNED** — all add μs of jitter to a ns path:
`sleep_for` (50-100μs) · `yield` (1-5μs) · `futex`/`eventfd` (1-5μs) · `condition_variable` (3-10μs) · `atomic::wait/notify` (futex) · **any timeout** (timeouts mask race conditions — spin on the completion signal or fix the race).

---

## Performance

**P1. Cache-line isolation:** `alignas(64)` on SPSC head/tail atomics. Eliminates false sharing.

**P2. SoA / parallel arrays:** Each field in contiguous array. Iteration detector reads 16B/entry vs 88B AoS.

**P3. Power-of-two capacity:** `MASK = CAPACITY - 1`, bitmask replaces modulo. Pre-allocated, no resizing.

**P4. Branch hints:** `[[likely]]`/`[[unlikely]]`, never `__builtin_expect`.

**P5. Selective inlining:** `CRUCIBLE_INLINE` on ~5 hot-path functions only. Excessive inlining bloats icache.

**P6. Interning:** Same structure → same pointer → pointer equality (~1ns). All Expr nodes immutable, arena-allocated.

**P7. constexpr everything:** Compile-time eval catches UB. `std::unreachable()` after exhaustive switches.

---

## Banned Patterns

| Pattern | Cost | Use instead |
|---------|------|-------------|
| `new`/`delete` | manual lifetime | Arena `alloc_obj<T>()` |
| `std::shared_ptr` | ~10ns atomic refcount | raw arena pointers |
| `std::string` in structs | 32B SSO, breaks memcpy/cache | `const char*` + `copy_string_()` |
| `virtual` in data structs | +8B vtable, cache-line break | `kind` enum + `static_cast` |
| exceptions on hot path | setup cost even unthrown | return bool, `assert`/`abort` |
| `std::unordered_map` | chained buckets, pointer chasing | open-addressing Swiss table |
| implicit single-arg ctors | silent type coercion | `explicit` everywhere |
| `std::optional`/`variant` | +1B / visitation overhead | `nullptr` / `kind` enum |
| RTTI (`dynamic_cast`) | disabled globally | `kind` enum |
| `std::ranges` pipelines | compile-time bloat | raw loops |
| `std::format`/`print` | not hot-path relevant | `fprintf` for debug |

---

## C++26 Feature Map

### Baseline (all compilers)

| Feature | Usage |
|---------|-------|
| NSDMI, `= delete("reason")`, `std::span`, `std::to_underlying()` | Safety axioms |
| `std::unreachable()`, `std::bit_cast<T>()`, `std::expected<T,E>` | Type safety |
| `std::countr_zero()`, saturation arithmetic, `operator<=>` | Primitives |
| `constexpr` (extended), `[[likely]]`/`[[unlikely]]`, `[[nodiscard]]` | Performance |
| `alignas(64)`, pack indexing `Ts...[0]`, structured binding packs | Layout / generics |

### Clang 22 Exclusive

| Feature | Macro | Usage |
|---------|-------|-------|
| Trivial relocatability | `__cpp_trivial_relocatability` | `static_assert` Arena memcpy sound |
| `span::at()` | `__cpp_lib_span_at` | Debug bounds checking |
| `std::flat_map` | `__cpp_lib_flat_map` | Sorted containers |

### GCC 16 Exclusive

| Feature | Macro | Usage |
|---------|-------|-------|
| Static reflection | `__cpp_impl_reflection` | Auto hash/serialize/compare (`-freflection`) |
| Expansion statements | `__cpp_expansion_statements` | `template for` — reflection iteration |
| `std::inplace_vector` | `__cpp_lib_inplace_vector` | Bounds-checked fixed-capacity |
| constexpr exceptions | `__cpp_constexpr_exceptions` | Compile-time errors |
| `std::function_ref` | `__cpp_lib_function_ref` | Non-owning callable |
| `<debugging>` | `__cpp_lib_debugging` | `std::breakpoint()` |

### Not Yet Available

Contracts (`pre`/`post`/`assert`) · Pattern matching (`inspect`) · `std::simd` · Lifetime annotations

### Feature Guard Pattern

```cpp
#if __has_cpp_attribute(__cpp_trivial_relocatability)
static_assert(std::is_trivially_relocatable_v<GraphNode>);
#endif
#ifdef __cpp_impl_reflection
template <typename T> uint64_t reflect_hash(const T& obj) { /* ... */ }
#endif
#ifdef __cpp_lib_inplace_vector
using Dims = std::inplace_vector<int64_t, 8>;
#else
struct Dims { int64_t data[8]{}; uint8_t n = 0; };
#endif
```

### Decision Matrix

| Need | Clang 22 | GCC 16 | Baseline |
|------|----------|--------|----------|
| memcpy safety | `is_trivially_relocatable_v` | N/A | `is_trivially_copyable_v` |
| Auto struct hash | N/A | `reflect_hash<T>` | Hand-written |
| Fixed-cap array | `T arr[N]{}` + count | `inplace_vector<T,N>` | `T arr[N]{}` + count |
| Bounds-checked span | `span.at(i)` | `assert` + `span[i]` | `assert` + `span[i]` |

---

## Hard Stops

Violate any → change rejected. Each exists because I caught you doing it.

**HS1. Fix root cause** — never bump a timeout or workaround. Find WHY, fix WHY. Timeouts mask race conditions.

**HS2. Both compilers** before done: `cmake --build --preset default && ctest --preset default` then `--preset gcc`.

**HS3. No unwanted files** — no .md summaries, no helper/utils. New files only when structurally necessary.

**HS4. No destructive git** — no checkout/reset/clean/stash without permission. No `--no-verify`. Atomic commits.

**HS5. Measure, don't guess** — run 10×, print μs, bound = 2× worst observed. Intuition lies.

**HS6. No sed/awk** — Edit tool only. Every edit visible and reviewable.

---

ZERO COPY. ZERO ALLOC ON HOT PATH. EVERY INSTRUCTION JUSTIFIED.
L1d=48KB. L2=2MB. Squat upfront, point into it, write into it.

IF YOU SEE VARIANCE > 5% IN BENCHES - IGNORE RESULTS, IT'S MY LAPTOP BEING THROTTLED
