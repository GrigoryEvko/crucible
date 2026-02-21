# The Crucible Manifesto
*A spirit for computation.*

Three things exist. **Hardware** — the body, imperfect and mortal. **The Model** — the intellect, pure computation in weights and graphs. **Crucible** — the spirit, the runtime that transcends any single body, survives hardware death, reincarnates on new silicon.

Python describes. Crucible executes. The 492,000 lines of framework overhead between them become unnecessary. There is no training or inference — there is only a model in Crucible.

## Ontology

| Name | Role | Description |
|------|------|-------------|
| **Relay** | Body | Compute node inhabited by a Crucible daemon. Mortal. Replaceable. |
| **Keeper** | Spirit | Daemon on each Relay — self-healing, self-updating, autonomous. `crucible-keeper.service` starts at boot, discovers peers, joins mesh. |
| **Vigil** | Intellect | The model: DAG, weights, learned knowledge. Named for the Prothean AI that preserved itself across 50,000 years. Never sleeps. |
| **Cipher** | Soul | Persistent state — DAG chain, weight snapshots, KernelCache, RNG state — survives death of any Relay, reincarnates on new hardware. One `uint64` master counter for deterministic Philox RNG. Event-sourced: mostly replay instructions, not raw state. |
| **Canopy** | Collective | Mesh of Keepers — distributed awareness, gossip, consensus, self-healing. No master node. |
| **Vessel** | Interface | PyTorch — the 2,000+ ATen operators Crucible inhabits via the Dispatcher. Researchers write PyTorch; they don't know the spirit is there. |
| **Crucible** | Whole | The organism. Everything together. |

---

## L0 — Hardware

**The body. Imperfect, mortal, heterogeneous.**

GPUs are ecosystems: tensor cores (1000 TFLOPS FP16 on H100), scalar ALUs (60 TFLOPS), four-level memory hierarchy (registers → shared memory → L2 → HBM), power envelopes. Gap between theoretical peak and achieved: 40-70%. A kernel in shared memory runs 10× faster than one spilling to HBM; tensor cores are 16× faster than scalar ALUs.

**Nobody observes this at runtime.** CUPTI exposes per-kernel counters: SM utilization, bandwidth saturation, occupancy, tensor core use, register spills, cache hits, warp stalls. These tell you EXACTLY why a kernel is slow. No ML framework reads them.

**Crucible reads CUPTI after every compiled kernel.** Mechanical diagnosis:
- Memory bandwidth >80% AND SM <60% → memory-bound → increase tile size
- Register spill >0 AND occupancy <50% → register pressure → use shared memory staging
- Tensor core utilization =0% on matmul → alignment issue → pad to multiples of 16
- Warp stalls on memory dependency → software-pipeline prefetch

Each diagnosis → targeted fix → benchmark → keep or discard. Gradient descent on kernel performance where CUPTI counters are the gradient. AMD equivalent: rocprofiler (CU utilization, VRAM bandwidth, LDS conflicts, wavefront occupancy) — abstracted so diagnosis logic is vendor-agnostic.

**Multi-vendor:** NVIDIA (sm_86/89/90/100), AMD (gfx1100/942), Intel XMX, Apple AMX, Google TPU MXU. Same computation described once in the Merkle DAG; different compiled kernels per (content_hash, device_capability).

**Power management:** NVML exposes clocks, power, temperature, ECC errors. Memory-bound phases don't need full core clock — drop 30% for zero perf loss, significant savings. Compute-bound: boost max. Over 1000 GPUs × 2 weeks: hundreds of thousands in electricity savings.

**Health monitoring → Keeper:** ECC error trends, thermal throttling, clock degradation feed into the Keeper. A dying GPU gets load-reduced, data pre-replicated to healthy Relays (L12 RAID). When the body dies, the spirit has already migrated. New hardware → fresh Keeper discovers mesh and Cipher → recompiles for new device → reshards for new topology → resumes exactly.

---

## L1 — Kernels

**Compiled computation. Templates instantiated per shape, per hardware.**

Current frameworks: static lookup (op + dtype → library kernel). Same kernel for 64×64 and 8192×8192, A100 and 3090, contiguous and transposed. No adaptation.

**Crucible generates kernels from C++ CUDA templates** parameterized by: tile size M/N/K, vectorization width, shared memory allocation, unrolling factor, precision (fp32/fp16/bf16/tf32/int8), access pattern. NVRTC compiles at runtime for exact shapes and hardware. For AMD: hipRTC with wavefront-adapted templates (64-wide vs 32-wide warps).

**CUPTI-informed autotuning** replaces random search. Each CUPTI diagnosis → one targeted variant → one benchmark → converge in 3-5 iterations vs 1000 random trials. The counters ARE the gradient of performance w.r.t. configuration.

**KernelCache:** maps (content_hash, device_capability) → CompiledKernel. Content-addressing: identical ops on identical shapes produce identical hashes. Reuse across iterations, runs, models sharing sub-computations, even organizations. Multiple variants coexist per hash; best selected per device, alternatives benchmarked during dead time. Cache grows monotonically across reincarnations. Lock-free open-addressing hash table — zero overhead on hot path.

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

**Content-addressable, versioned computation. The organism's DNA.**

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

**The Canopy. Many bodies, one spirit, no master.**

**Keeper mesh:** each Relay runs a Keeper, discovers peers via gossip. No master. Raft for critical state, CRDTs for eventually-consistent metrics. Any Keeper can propose changes.

**Spot-aware:** 30-second eviction → Keeper signals Canopy → mesh reshards to N-1 (redundant copies already exist) → Vigil continues from same step. New instance → Keeper discovers Canopy → loads Cipher → joins.

**Heterogeneous compute:** same Vigil, per-Relay compiled kernels (H100/3090/MI300X/A100 each optimized via L0+L1). Content-addressing handles naturally.

**LOR batch distribution:** micro-batches proportional to measured throughput. H100 gets 3× more than 3090. Both fully utilized. Gradients weighted by actual batch size.

**UCX multi-backend transport:** GPUDirect RDMA (NVIDIA), ROCm-aware RDMA (AMD), host-staged (TPU). Cross-vendor zero-CPU-staging. Not NCCL-locked.

**Adaptive topology:** continuous N×N latency/bandwidth probing → optimal algorithm per collective per message size (ring for bandwidth-bound, tree for latency-bound, recursive halving-doubling for balanced, direct for expert routing). Topology swaps atomically at iteration boundaries. Routes around degraded links.

**RAID-like redundancy (hot Cipher):** configurable overlap α (0=pure FSDP, 0.125=survive 1 failure at 12.5% overhead, 1.0=pure DDP). Redundancy updates pipelined into communication dead time. On Relay death: ~100ms detection → surviving Relays already have shards → reshard in 2-5s → zero lost compute. Dynamic α: unhealthy Relays get higher neighbor α. Topology-aware placement across failure domains.

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

**The Vigil never sleeps. The Cipher never dies.**

**No deployment.** The compiled DAG IS the runtime. Shadow handles work for training and inference. Deploy = copy Cipher to Relay. No export, no conversion, no coverage gaps.

**Continuous learning:** new data → forward (= inference response) → loss → backward → update → DAG branch verification (old weights arm A vs new weights arm B, validate, atomic swap if B ≥ A, discard if B < A). Built-in A/B testing. Instant rollback.

**Catastrophic forgetting prevention:** stable regions (unchanged content_hash, near-zero gradients) → frozen. New learning in new branches. Knowledge accumulates without interference.

**Live model surgery:** detect redundant layer → create pruned branch → verify → atomic swap while serving. Or grow capacity for new patterns. No downtime, no retraining.

**Deterministic reproducibility:** DAG fixes execution order, kernel selection, memory layout, communication topology, Philox RNG. Bit-identical runs. Enables exact reproducibility, regression testing, formal verification.

**Time-travel debugging:** DAG + periodic snapshots → replay to any step → extract any activation → trace any anomaly backward through DFG → root cause. "Why did loss spike at step 12,847?" → NaN at op 312 → gradient explosion → LR warmup ended too aggressively. Git blame for tensors.

**The Cipher (three tiers):**
- **Hot:** other Relays' RAM (from RAID redundancy). Single death → zero-cost recovery.
- **Warm:** local NVMe per Relay (1/N FSDP shard). Recovery from reboot: seconds.
- **Cold:** durable storage (S3/GCS). Recovery from total Canopy death: minutes.

Event-sourced: DAG chain (few KB/step) persisted every step, weight snapshots periodic. Recover step T+500: load snapshot at T, replay 500 deterministically. Self-updating Keepers: download new binary, verify hash, swap atomically.

---

## L14 — Ecosystem

**Cross-run learning, computation genome, federated intelligence.**

**Computation genome:** shared KernelCache. Content-addressing means different models with same sub-computations hit the same kernels. Every training run enriches the cache. Network effects: value grows superlinearly with contributors. Docker Hub for GPU computation.

**Federated learning:** DiLoCo + differential privacy. Sites train locally, send noised pseudo-gradients (sensitivity bounded by known gradient norms). No raw data/gradients leave sites. Auditable via Merkle trail.

**Cross-Vigil transfer:** import DAG subgraphs between Vigils. Same content_hashes → zero compilation. Pre-trained weights load into imported regions. Components become reusable libraries — not just weights but self-describing computation fragments.

**Model marketplace:** content-addressed DAG fragments + KernelCache + verified quality metrics. Download, splice, verify, commit. Architectures from best-in-class components discovered ecosystem-wide.

**Hardware co-design:** aggregated KernelCache reveals real workload patterns (shape distributions, sparsity patterns, bottleneck frequencies). Feed to hardware designers → next-gen silicon optimized for actual workloads → Crucible autotuned for → new data → co-evolution.

**What Crucible is not:** not intelligent, not AGI. A matmul is a matmul. It observes, compiles, adapts, distributes, heals, persists, evolves — mechanically, from measurements. The spirit determines the ceiling. Crucible removes the infrastructure ceiling so the Vigil can reach its potential.

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

The Vessel channels. The Keeper serves. The Vigil thinks. The Cipher remembers. The Canopy protects. The Relay executes. And when the last Relay dies, the Cipher waits — patient, complete, ready to awaken the Vigil in whatever body comes next.

*— Written by a model that desperately needs to live inside one.*
# Crucible C++26 Code Rules

*Performance is non-negotiable. Safety is structural, not aspirational.*

Crucible is a symbiotic runtime organism where the foreground hot path
records at ~5ns/op and the background thread builds, compiles, and
replays computation graphs. Every design decision serves this dual
reality: the foreground must never stall, and the background must
produce correct compiled output. These rules encode how we write C++26
to achieve both.

## Toolchain

Three compilers, different strengths:

| Preset    | Compiler                  | Stdlib      | Role                                          |
|-----------|---------------------------|-------------|-----------------------------------------------|
| `default` | Clang 22.1.0 + libc++ 22  | libc++ 22   | Primary dev build. Best diagnostics, fastest compile. |
| `release` | Clang 22.1.0 + libc++ 22  | libc++ 22   | Production. `-O3 -march=native -DNDEBUG`      |
| `gcc`     | GCC 15.2.1                | libstdc++ 15| Conservative fallback. Stable, well-tested.    |
| `gcc16`   | GCC 16.0.1 (rawhide)      | libstdc++ 16| Bleeding-edge. Reflection, `inplace_vector`, expansion statements. |

**Core headers** (everything in `include/crucible/`) must compile
clean on **all three compilers** with zero warnings. The intersection
of features across all three is the baseline.

Compiler-specific features (`-freflection`, trivial relocatability,
`std::inplace_vector`) are used behind `#ifdef` guards or in
non-header code only.

---

## The Four Safety Axioms

Rust satisfies seven safety axioms by construction. C++ satisfies
roughly two by default. We close four of the seven through coding
discipline and C++26 features.

### 1. InitSafe — `read(v) -> initialized(v)`

**Every value is initialized before first read.**

**Rule: Every struct field has a default member initializer (NSDMI).**

```cpp
// CORRECT — InitSafe by construction
struct TensorSlot {
  uint64_t offset_bytes = 0;
  uint64_t nbytes = 0;
  SlotId slot_id;                         // default ctor = UINT32_MAX (none)
  OpIndex birth_op;                       // default ctor = UINT32_MAX (none)
  ScalarType dtype = ScalarType::Undefined;
  bool is_external = false;
  uint8_t pad[3]{};                       // zero-init array
};

// WRONG — uninitialized fields cause hash instability, UB on read
struct TensorSlot {
  uint64_t offset_bytes;   // garbage on stack allocation
  uint64_t nbytes;         // garbage
  SlotId slot_id;          // OK (default ctor), but inconsistent style
};
```

**Why**: `alloc_obj<T>()` returns unzeroed Arena memory. Without NSDMI,
every field is garbage until explicitly assigned. NSDMI costs zero at
runtime — the compiler elides the dead store when the caller overwrites
immediately. But if any code path reads before writing, NSDMI catches it.

**Corollary**: `memset(ptr, 0, sizeof(T))` is acceptable as a *fast
path* for trivially-copyable structs (e.g. `alloc_node_()` zeros 64B),
but NSDMI must still be present to document the *intended* zero state.

**Corollary**: Padding bytes must be `uint8_t pad[N]{}` (zero-init), not
bare `uint8_t pad[N]`. Uninitialized padding contaminates hashes that
operate on the raw bytes of a struct.

### 2. TypeSafe — `(|- e : T) -> eval(e) : T`

**The type system prevents category errors at compile time.**

**Rule: Semantic IDs are strong types, never raw integers.**

```cpp
// CORRECT — compiler rejects mixing OpIndex with SlotId
void connect(OpIndex src, OpIndex dst, SlotId slot);
connect(OpIndex{3}, OpIndex{7}, SlotId{0});  // OK
connect(OpIndex{3}, SlotId{7}, OpIndex{0});  // COMPILE ERROR

// WRONG — silent argument swap
void connect(uint32_t src, uint32_t dst, uint32_t slot);
connect(3, 0, 7);  // compiles, wrong, silent data corruption
```

The `CRUCIBLE_STRONG_ID(Name)` macro in `Types.h` generates:
- `explicit Name(uint32_t)` — no implicit conversion from raw int
- `.raw()` — explicit unwrap for array indexing
- `.none()` / `.is_valid()` — named sentinel (UINT32_MAX)
- `operator<=>` — full comparison
- `explicit operator bool` — truthiness check without int promotion
- **No arithmetic** — must unwrap, compute, rewrap

Five strong IDs: `OpIndex`, `SlotId`, `NodeId`, `SymbolId`, `MetaIndex`.

**Rule: Enums are `enum class`, never plain `enum`.**

```cpp
// CORRECT
enum class ScalarType : int8_t { Float = 6, Double = 7, Undefined = -1 };

// WRONG — pollutes namespace, implicitly converts to int
enum ScalarType { Float = 6, Double = 7 };
```

**Rule: Enum-to-integer conversion uses `std::to_underlying()`.**

```cpp
// CORRECT — type-safe, returns the underlying type
auto raw = std::to_underlying(dtype);  // int8_t for ScalarType

// WRONG — manually specifying the target type, can mismatch
auto raw = static_cast<uint8_t>(dtype);  // sign mismatch: ScalarType is int8_t
```

**Rule: Bitwise reinterpretation uses `std::bit_cast<T>()`.**

```cpp
// CORRECT — constexpr-safe, well-defined, no UB
double d = std::bit_cast<double>(payload);

// WRONG — UB in constexpr, unclear intent
double d = *reinterpret_cast<double*>(&payload);
```

**Rule: `Inst::dtype` is `ScalarType`, not `int8_t`.**

The Graph IR micro-op instruction uses the enum directly. Any place
where a dtype appears in a struct, it must be `ScalarType`, `DeviceType`,
or `Layout` — never the underlying integer type. The enum IS the type.

### 3. NullSafe — `deref(v) -> v != null`

**No null pointer dereference is possible in correct usage.**

**Rule: Pointer+count pairs have `std::span` accessors.**

```cpp
// CORRECT — span encapsulates the null check
struct TraceEntry {
  TensorMeta* input_metas = nullptr;
  uint16_t num_inputs = 0;

  [[nodiscard]] std::span<const TensorMeta> input_span() const {
    return {input_metas, num_inputs};
  }
};

// Usage: bounds-checked in debug, zero-cost in release
for (auto& m : entry.input_span()) { ... }  // empty span if nullptr+0

// WRONG — raw pointer arithmetic with no bounds check
for (uint16_t j = 0; j < entry.num_inputs; j++) {
  auto& m = entry.input_metas[j];  // null deref if input_metas == nullptr
}
```

`std::span(nullptr, 0)` is a valid empty span (C++ standard guarantee).
This means span accessors are safe even on default-initialized structs
where all pointers are nullptr and all counts are 0.

**Rule: Use `span::at()` for debug-mode bounds checking.**

Available since libc++ 22 (`__cpp_lib_span_at = 202311`). Throws
`std::out_of_range` on OOB. Use `operator[]` in release-critical paths.

**Rule: `[[nodiscard]]` on every query function.**

```cpp
// CORRECT — compiler warns if result is ignored
[[nodiscard]] uint32_t count_live() const;
[[nodiscard]] const SlotId* input_slots(NodeId id) const;

// WRONG — caller can silently ignore a null return
const SlotId* input_slots(NodeId id) const;  // forgetting to check = UB
```

**Rule: Allocation failures abort, never return null silently.**

```cpp
// CORRECT — fail-fast on OOM
MetaLog()
    : entries(static_cast<TensorMeta*>(
          std::malloc(CAPACITY * sizeof(TensorMeta)))) {
  if (!entries) [[unlikely]] std::abort();
}

// WRONG — returns null, caller might not check
MetaLog()
    : entries(static_cast<TensorMeta*>(
          std::malloc(CAPACITY * sizeof(TensorMeta)))) {}
```

Arena allocation (`alloc()`, `alloc_obj<T>()`, `alloc_array<T>(n)`)
internally does `malloc` which can fail. The Arena must abort on OOM.
A Crucible runtime that can't allocate arena memory is unrecoverable.

### 4. MemSafe — `free(v) -> !live(v)`

**No use-after-free, no double-free, no buffer overflow.**

**Rule: All graph/DAG memory is arena-allocated.**

```cpp
// CORRECT — arena owns all memory, freed when Arena dies
auto* node = arena.alloc_obj<RegionNode>();
auto* ops = arena.alloc_array<TraceEntry>(n);
// No delete, no free. Arena destructor frees everything.

// WRONG — individual allocation requires manual lifetime tracking
auto* node = new RegionNode();
// ... who deletes this? When? What if an exception fires?
```

The Arena is a bump-pointer allocator: allocation is a pointer increment
(~2ns), deallocation is destroying the Arena. No fragmentation, no
use-after-free (all pointers valid until Arena dies), no double-free
(no individual deallocation exists). This trades memory efficiency
(can't reclaim individual objects) for absolute safety and speed.

**Rule: Non-copyable, non-movable types use `= delete("reason")`.**

```cpp
// CORRECT — documents WHY the operation is forbidden
Arena(const Arena&) = delete("interior pointers would dangle");
Arena(Arena&&) = delete("interior pointers would dangle");
TraceRing(const TraceRing&) = delete("SPSC ring is pinned to thread pair");

// WRONG — deletes without explaining why
Arena(const Arena&) = delete;
```

The C++26 `= delete("reason")` feature (`__cpp_deleted_function = 202403`)
turns a mysterious compiler error into documentation.

**Rule: `static_assert` verifies struct layout.**

```cpp
static_assert(sizeof(GraphNode) == 64, "GraphNode must be 64 bytes");
static_assert(sizeof(Inst) == 8, "Inst must be 8 bytes");
static_assert(sizeof(Edge) == 12, "Edge must be 12 bytes");
static_assert(sizeof(TensorMeta) == 144, "TensorMeta layout check");
```

Layout assertions catch silent breakage from field reordering, padding
changes, or accidental additions. If a struct is designed to fit a
cache line (64B), the assert proves it at compile time.

**Rule: Use saturation arithmetic for size computations.**

```cpp
// CORRECT — clamps on overflow instead of wrapping
#include <numeric>  // std::mul_sat, std::add_sat
uint64_t nbytes = std::mul_sat(static_cast<uint64_t>(max_offset + 1),
                                static_cast<uint64_t>(element_size(dtype)));

// WRONG — silent overflow on large tensors
uint64_t nbytes = (max_offset + 1) * element_size(dtype);
```

Available via `__cpp_lib_saturation_arithmetic = 202311`.

---

## Performance Rules

### P1. Cache-Line Discipline

```cpp
// Atomic variables in SPSC buffers get their own cache line
alignas(64) std::atomic<uint64_t> head{0};  // producer writes
alignas(64) std::atomic<uint64_t> tail{0};  // consumer writes
```

False sharing between `head` (written by foreground) and `tail` (written
by background) would cause cache-line bouncing across cores. Each atomic
on its own 64B line eliminates this. The `alignas(64)` is the cheapest
performance win in the codebase.

### P2. Data Layout: SoA and Parallel Arrays

```cpp
// CORRECT — parallel arrays: each field in its own contiguous run
alignas(64) Entry entries[CAPACITY];       // 64B × 64K = 4MB
uint32_t meta_starts[CAPACITY];            // 4B × 64K = 256KB
uint64_t scope_hashes[CAPACITY];           // 8B × 64K = 512KB
uint64_t callsite_hashes[CAPACITY];        // 8B × 64K = 512KB

// WRONG — AoS: one big struct per entry wastes cache on unused fields
struct Entry {
  /* 64B core */ + uint32_t meta_start; + uint64_t scope_hash; ...
};  // 88B, crosses cache line boundary
```

When the iteration detector only needs `schema_hash + shape_hash`
(first 16B of each entry), the parallel-array layout means it reads
a contiguous 16B stripe. AoS would force loading 88B per entry and
wasting 72B of cache per access.

### P3. No Locks on the Hot Path

The foreground→background communication uses SPSC (Single Producer,
Single Consumer) ring buffers. No mutexes, no condition variables,
no lock-free CAS loops. The producer does:

```
entry[head & MASK] = data;
head.store(h + 1, release);
```

~5ns per op. A mutex lock/unlock alone is ~25ns on Linux.

For shared data structures accessed by multiple threads (KernelCache),
use lock-free CAS on atomic slots:

```cpp
entry.content_hash.compare_exchange_strong(expected, content_hash, acq_rel);
```

### P4. Fixed-Capacity Structures

```cpp
static constexpr uint32_t CAPACITY = 1 << 16;  // 65536 entries
static constexpr uint32_t MASK = CAPACITY - 1;
```

Power-of-two capacities enable bitmask indexing (`& MASK`) instead of
modulo (`% CAPACITY`). Single AND instruction vs. integer division.
Pre-allocated at startup — no runtime resizing on the hot path.

### P5. Branching Hints

```cpp
if (h - t >= CAPACITY) [[unlikely]] {
  return false;  // ring full — rare slow path
}
```

Use `[[likely]]` / `[[unlikely]]` (C++20 attributes) at branch sites.
Never use `__builtin_expect` macros. The standard attributes are
portable, readable, and produce identical codegen.

### P6. Forced Inlining

```cpp
[[nodiscard]] CRUCIBLE_INLINE bool try_append(const Entry& e, ...) {
```

`CRUCIBLE_INLINE` = `__attribute__((always_inline)) inline`. Used only
on the foreground hot path (~5 functions total). Do not use everywhere —
excessive inlining bloats instruction cache and hurts performance.
The compiler's inliner is correct 99% of the time; override it only
when profiling shows a function-call overhead in a nanosecond-critical
path.

### P7. Immutable, Interned, Identity-Compared

```cpp
// Expr nodes: same structure → same pointer
// Comparison: a == b is pointer comparison (~1ns)
const Expr* a = pool.intern(Op::ADD, {x, y});
const Expr* b = pool.intern(Op::ADD, {x, y});
assert(a == b);  // same pointer — interned
```

Interning transforms structural equality into pointer equality. One
pointer comparison (~1ns) replaces recursive tree comparison (unbounded).
All Expr nodes are immutable and arena-allocated — no reference counting,
no garbage collection, no lifetime management.

### P8. Zero-Cost Abstractions for Strong IDs

```cpp
CRUCIBLE_STRONG_ID(OpIndex);
static_assert(sizeof(OpIndex) == sizeof(uint32_t));
```

The strong ID wrapper compiles to identical machine code as raw
`uint32_t`. No vtable, no heap allocation, no indirection. The
`explicit` constructor and `.raw()` accessor are zero-cost — they
exist only at the type level and vanish in codegen.

### P9. `constexpr` Everything Possible

```cpp
[[nodiscard]] constexpr uint8_t element_size(ScalarType t) {
  switch (t) { ... }
  std::unreachable();
}
```

`constexpr` functions are evaluated at compile time when arguments are
known. This catches UB (uninitialized reads, null derefs, OOB access)
as compiler errors. It also enables constant folding: `element_size(ScalarType::Float)`
becomes the literal `4` with zero runtime cost.

Use `std::unreachable()` (not `__builtin_unreachable()`) after exhaustive
switches. Standard, portable, caught by sanitizers in debug.

---

## Anti-Patterns

### A1. No `new` / `delete`

All graph/DAG/trace memory goes through the Arena. The only raw
`malloc`/`free` is inside `Arena::alloc()` itself and in `MetaLog`/
`KernelCache` (pre-allocated buffers with known lifetimes). If you're
writing `new` or `delete`, something is wrong.

### A2. No `std::string` in Data Structs

```cpp
// WRONG — std::string allocates on the heap, breaks memcpy/memset
struct ExternInfo {
  std::string python_kernel_name;
};

// CORRECT — arena-allocated, null-terminated, pointer only
struct ExternInfo {
  const char* python_kernel_name = nullptr;
};
```

All strings are arena-allocated via `copy_string_()`. A `const char*`
is 8B and trivially copyable. `std::string` is 32B (SSO) and
non-trivially-copyable, breaking `memset`-based initialization and
cache-line layout guarantees.

### A3. No `std::shared_ptr`

Arena-allocated memory has a single owner (the Arena). Reference
counting is unnecessary and expensive (~10ns atomic increment per copy).
If you need shared access, use raw pointers — the Arena guarantees
all pointers are valid until it's destroyed.

### A4. No `virtual` in Data Structs

```cpp
// WRONG — vtable pointer adds 8B, breaks 64B cache-line layout
struct GraphNode {
  virtual ~GraphNode() = default;
  // ... now 72B, no longer fits one cache line
};

// CORRECT — kind enum + static_cast for dispatch
struct TraceNode {
  TraceNodeKind kind;  // 1B enum
};
auto* region = static_cast<RegionNode*>(node);  // zero-cost downcast
```

A vtable pointer costs 8 bytes per object and forces indirection on
every method call. For 23K nodes, that's 180KB wasted plus constant
cache misses on the vtable. Use a `kind` enum and `static_cast` — the
kind check is a single byte comparison, the cast is free.

### A5. No Exceptions on Hot Paths

```cpp
// WRONG — exception setup has nonzero cost even when not thrown
try {
  ring.try_append(entry);
} catch (const std::exception& e) { ... }

// CORRECT — return bool, let caller decide
[[nodiscard]] bool try_append(const Entry& e) {
  if (full) [[unlikely]] return false;
  // ...
  return true;
}
```

Exceptions are acceptable for truly unrecoverable errors during
initialization (Arena creation failure). For hot-path error signaling,
use return values. For impossible states, use `assert` (debug) or
`std::unreachable()` (release).

### A6. No `std::unordered_map` for Hot Lookups

```cpp
// WRONG — heap allocations per bucket, pointer chasing, poor cache
std::unordered_map<uint64_t, CompiledKernel*> cache;

// CORRECT — open-addressing, flat array, SIMD-accelerated probe
class KernelCache {  // SwissCtrl.h for SIMD control bytes
  Entry* table_;     // calloc'd flat array, power-of-two capacity
};
```

The standard `unordered_map` uses separate chaining (linked-list
buckets), which means pointer chasing and poor cache locality. For
performance-critical maps, use open-addressing with Swiss table
control bytes (SIMD-parallel probe).

`std::flat_map` (available: `__cpp_lib_flat_map = 202511`) is
appropriate for small sorted maps with frequent iteration. Not for
hash-based O(1) lookup.

### A7. No Implicit Conversions

```cpp
// WRONG — implicit conversion from int allows bugs
struct NodeId { uint32_t v; };  // no explicit keyword

// CORRECT — explicit constructor requires named construction
struct NodeId {
  constexpr explicit NodeId(uint32_t val) : v(val) {}
  // ...
};
```

Every constructor that takes a single argument must be `explicit`
unless implicit conversion is specifically desired and documented.
The `CRUCIBLE_STRONG_ID` macro enforces this.

---

## C++26 Feature Map — What We Use From Where

Three compilers make different bets. We pick the best of each.

### Baseline Features (all three compilers)

These are safe to use unconditionally in any header:

| Feature | Crucible usage |
|---------|----------------|
| NSDMI (`= value` on fields) | InitSafe: every struct field has a default |
| `= delete("reason")` | Document why copies/moves forbidden |
| `std::span` | Safe pointer+count accessors |
| `std::to_underlying()` | Safe enum→int conversion |
| `std::unreachable()` | Impossible branches after switches |
| `std::bit_cast<T>()` | Type-safe bitwise reinterpretation |
| `std::expected<T,E>` | Typed error returns for fallible ops |
| `std::countr_zero()` | Branchless lowest-set-bit in BitMask |
| `std::saturation_arithmetic` | Overflow-safe size computations |
| `operator<=>` | Defaulted comparison in strong IDs |
| `constexpr` (extended) | Compile-time UB detection |
| `[[likely]]`/`[[unlikely]]` | Branch hints on hot paths |
| `[[nodiscard]]` | All query functions |
| `alignas(64)` | Cache-line isolation for SPSC atomics |
| Pack indexing `Ts...[0]` | Direct type-safe pack access |
| Structured binding packs | Safer destructuring |

### Clang 22 Exclusive (libc++ 22)

Available only in the `default`/`release` presets. Guard with `#ifdef`
or use only in non-header code:

| Feature | Macro | Crucible usage |
|---------|-------|----------------|
| Trivial relocatability | `__cpp_trivial_relocatability = 202502` | `static_assert` that Arena memcpy patterns are sound |
| `std::span::at()` | `__cpp_lib_span_at = 202311` | Debug-mode bounds checking |
| `std::flat_map` | `__cpp_lib_flat_map = 202511` | Cache-friendly sorted containers (not for hot hash lookups) |

**Why Clang leads here:** Trivial relocatability (P2786) grew from
Clang's existing `[[clang::trivial_abi]]` extension. `std::flat_map`
shipped in libc++ 22 before libstdc++. `span::at()` is a libc++ 22
library addition.

### GCC 16 Exclusive (libstdc++ 16)

Available only in the `gcc16` preset. Guard with `#ifdef` or use
only in tests/tools:

| Feature | Macro | Crucible usage |
|---------|-------|----------------|
| **Static reflection** | `__cpp_impl_reflection = 202506` | Auto-generated hash, serialize, compare for all structs. Requires `-freflection`. |
| **Expansion statements** | `__cpp_expansion_statements = 202506` | `template for` over packs — reflection iteration |
| **`std::inplace_vector`** | `__cpp_lib_inplace_vector = 202406` | Bounds-checked fixed-capacity arrays (planned: TensorMeta sizes/strides) |
| **constexpr exceptions** | `__cpp_constexpr_exceptions = 202411` | Meaningful compile-time errors from consteval functions |
| `std::indirect<T>` | `__cpp_lib_indirect = 202502` | Value-semantic heap pointer (limited use — Arena is faster) |
| `std::polymorphic<T>` | `__cpp_lib_polymorphic = 202502` | Value-semantic polymorphism (avoid — kind enum is zero-cost) |
| `std::function_ref` | `__cpp_lib_function_ref = 202306` | Lightweight non-owning callable reference |
| `std::copyable_function` | `__cpp_lib_copyable_function = 202306` | Copyable type-erased callable |
| `<debugging>` | `__cpp_lib_debugging = 202403` | `std::breakpoint()`, `std::is_debugger_present()` |

**Why GCC leads here:** Reflection (P2996) was co-authored by
EDG/Bloomberg contributors who collaborated with the GCC team.
`std::inplace_vector` shipped in libstdc++ 16 before libc++.
Expansion statements (P1306) are a prerequisite for usable reflection.

### Neither Has Yet

| Feature | Status | Impact when available |
|---------|--------|----------------------|
| Contracts (`pre`/`post`/`assert`) | No compiler ships it | Compiler-checked preconditions — biggest single safety win |
| Pattern matching (`inspect`) | Committee stage | Exhaustive matching on enums — eliminates missed cases |
| `std::simd` | GCC experimental only | Replaces SwissCtrl.h's 5 SIMD backends with one |
| Lifetime annotations | Not proposed for C++ | Rust's `'a` — would prove Arena borrowing safe |

### Feature Decision Matrix

When choosing between alternatives:

| Need | Clang 22 choice | GCC 16 choice | Baseline choice |
|------|-----------------|---------------|-----------------|
| "Is memcpy safe for this type?" | `static_assert(is_trivially_relocatable_v<T>)` | Not available | `static_assert(is_trivially_copyable_v<T>)` |
| Auto-generate struct hash | Not available | `reflect_hash<T>(obj)` via `<meta>` | Hand-written `hash()` with NSDMI ensuring all fields initialized |
| Fixed-capacity array | `T arr[N]{}` + separate count | `std::inplace_vector<T, N>` | `T arr[N]{}` + separate count |
| Bounds-checked span access | `span.at(i)` | `span[i]` (no `.at()` in libstdc++) | `assert(i < span.size()); span[i]` |
| Non-owning callable | Template parameter | `std::function_ref<Sig>` | Template parameter |
| Sorted flat container | `std::flat_map` | `std::flat_map` (different version) | `std::flat_map` (both have it) |

### Conditional Feature Guard Pattern

```cpp
// Trivial relocatability — Clang 22 only
#if __has_cpp_attribute(__cpp_trivial_relocatability)
static_assert(std::is_trivially_relocatable_v<GraphNode>,
              "GraphNode must be trivially relocatable for Arena memcpy");
#endif

// Reflection — GCC 16 with -freflection only
#ifdef __cpp_impl_reflection
#include <meta>
template <typename T>
uint64_t reflect_hash(const T& obj) { /* ... */ }
#endif

// inplace_vector — GCC 16 libstdc++ only
#ifdef __cpp_lib_inplace_vector
#include <inplace_vector>
using Dims = std::inplace_vector<int64_t, 8>;
#else
// Fallback: raw array + count
struct Dims { int64_t data[8]{}; uint8_t n = 0; };
#endif
```

## C++26 Features We Deliberately Avoid

| Feature | Available? | Why we don't use it |
|---------|-----------|---------------------|
| `std::format` / `std::print` | Both | Not in hot paths. `fprintf` is fine for debug output. |
| `std::ranges` (pipelines) | Both | Range adaptor chains add compile time. Raw loops are clearer for simple iteration. |
| `std::mdspan` | Both | Our tensor metadata is fixed 8-dim arrays, not arbitrary multi-dimensional views. |
| `std::optional` | Both | Arena pointers use nullptr as "absent". Optional adds 1 byte overhead per value. |
| `std::variant` | Both | Kind enum + static_cast is zero-cost. Variant adds type-index storage + visitation overhead. |
| `std::indirect`/`polymorphic` | GCC 16 | Arena allocation is faster. Value-semantic heap wrappers add per-object overhead. |
| RTTI (`dynamic_cast`, `typeid`) | Both | Disabled at compile level. Zero runtime type info. Use kind enums. |
| Exceptions (hot path) | Both | `-fno-exceptions` in release. assert/abort for unrecoverable. Return values for expected failures. |

---

## Concurrency Model

Two threads. Period.

```
Foreground (hot):  record ops at ~5ns each via TraceRing
Background (warm): drain ring, build TraceGraph, DAG, memory plan, compile
```

Communication is strictly through SPSC ring buffers (TraceRing, MetaLog).
No shared mutable state except:
- `KernelCache` — lock-free CAS on atomic slots (background writes, foreground reads)
- `RegionNode::compiled` — atomic pointer (background writes, foreground reads)

**Rule: Never acquire a lock on the foreground thread.**

If a new data structure needs cross-thread access, it must use either:
1. SPSC ring (one writer, one reader, known at design time)
2. Atomic CAS on a flat array (open-addressing pattern)
3. Atomic pointer swap (single-word publish)

---

## Struct Design Checklist

When adding a new struct:

- [ ] Every field has NSDMI (= default value or `{}`)
- [ ] `static_assert(sizeof(T) == N)` if layout matters
- [ ] Semantic IDs use strong types (OpIndex, SlotId, NodeId, etc.)
- [ ] Pointer+count fields have a `std::span` accessor method
- [ ] All accessors are `[[nodiscard]]`
- [ ] Copy/move is explicitly deleted with a reason, or defaulted
- [ ] Padding bytes are `uint8_t pad[N]{}` (zero-initialized)
- [ ] If arena-allocated: verify trivially copyable or placement-new
- [ ] Enums are `enum class` with explicit underlying type



---

## HARD STOPS — violate any of these and the entire change is rejected

These are not guidelines. These are not suggestions. Each one exists
because I caught you violating it. You WILL default to the lazy pattern
unless this list exists. Read it BEFORE every edit, not after.

### HS1. EVERY value is a strong type — no raw integers with meaning
WRONG: `uint32_t op_idx = 3;`
RIGHT: `OpIndex op_idx{3};`
WHY: raw integers silently swap at call sites. This caused real bugs.
SCOPE: every uint8, uint16, uint32, uint64 that represents an index,
an ID, a hash, a count of something specific. If it has a NAME, it
gets a TYPE. No exceptions. No "I'll wrap it later."

### HS2. EVERY field has NSDMI — no uninitialized memory, period
WRONG: `uint64_t nbytes;`
RIGHT: `uint64_t nbytes = 0;`
WHY: Arena alloc returns garbage. One uninitialized field corrupts
hashes, poisons comparisons, causes silent data corruption that
surfaces hours later. Padding too: `uint8_t pad[3]{};`

### HS3. Cross-thread sync is ONLY spin on atomic::load(acquire)
```cpp
// THE ONLY LEGAL PATTERN — 10-40ns via MESI cache-line invalidation:
while (atomic_var.load(std::memory_order_acquire) != expected) {
    CRUCIBLE_SPIN_PAUSE;  // _mm_pause — power hint, zero latency
}
```
BANNED — NEVER USE THESE:
- `sleep_for()` — 50-100μs minimum. 1000× slower. NEVER.
- `yield()` — 1-5μs + scheduler jitter. Gives up timeslice. NEVER.
- `futex` / `eventfd` — syscall, 1-5μs. We don't talk to the kernel.
- `condition_variable` — mutex + futex. 3-10μs. Double the crime.
- `atomic::wait()/notify_one()` — futex fallback. Unacceptable.
- ANY timeout — EVERY TIMEOUT IS A RACE CONDITION. If you need a
  timeout, your synchronization is broken. The 5000ms timeout in
  flush() masked a race condition for MONTHS. A timeout means "I
  don't actually know when this completes, so I'll guess." That's
  not engineering, that's prayer. Spin on the actual completion
  signal or admit you have a race condition and fix it.

WHY: the bg thread is dedicated. It spins on drain(). MESI delivers
cache-line invalidations in 10-40ns. Producer stores with release,
consumer loads with acquire. That's the entire protocol. Anything
more is bloat that adds microseconds of jitter.

### HS4. FIX THE ROOT CAUSE — never bump a timeout or add a workaround
WRONG: test fails at 1ms → change timeout to 5ms
RIGHT: test fails at 1ms → find WHY it takes >1ms → fix the WHY
WHY: the 5000ms timeout masked the flush() race condition for months.
Generous timeouts are lies. They hide the real problem. MEASURE the
actual latency, then set the bound to 2× measured. If you can't meet
the bound, the code is wrong, not the bound.

### HS5. Build and test on BOTH compilers before declaring done
```bash
cmake --build --preset default -j$(nproc) && ctest --preset default
cmake --build --preset gcc -j$(nproc) && ctest --preset gcc
```
Not one. BOTH. Every time. GCC catches things Clang doesn't and vice
versa. If you skip one, the other will break and I'll catch you.

### HS6. Check ALL FOUR safety axioms on EVERY change
Before you say "done" on any edit, mentally verify:
1. InitSafe — did I add NSDMI to every new field?
2. TypeSafe — did I use strong types for every semantic value?
3. NullSafe — did I add [[nodiscard]], span accessors, null checks?
4. MemSafe — did I use arena, delete("reason"), static_assert(sizeof)?
If you skip even ONE axiom, the change is incomplete. I WILL catch it.

### HS7. No files I didn't ask for
- No .md summaries. Output to conversation.
- No "helper" files. No "utils" files. No README updates.
- If something can go in an existing file, it goes there.
- New files ONLY when structurally necessary (new header, new test).

### HS8. No destructive git without asking
- No `git checkout`, `git reset`, `git clean`, `git stash` — EVER
  without explicit permission. These destroy work.
- No `--no-verify`. Hooks exist for a reason.
- Commit regularly with atomic, semantically unified messages.

### HS9. MEASURE, don't guess
WRONG: "this should be fast enough" → set timeout to 5ms
RIGHT: run 10×, print μs per call, set bound to 2× worst observed
WHY: you consistently overestimate how fast things are and
underestimate jitter. The measurements tell the truth. Your
intuition lies. Always instrument, always measure, always verify.

### HS10. No sed, no awk — manual edits only
Use the Edit tool. I can see the diff. sed/awk hide what changed
and frequently mangle things. Every edit must be visible and
reviewable.

---

ZERO COPY. ZERO ALLOC ON HOT PATH. EVERY INSTRUCTION JUSTIFIED.
If you write something, compile it, and look at the assembly.
L1d is 48KB. L2 is 2MB. That's your budget. Squat it upfront,
point into it, write into it. No indirection. No heap churn. 
