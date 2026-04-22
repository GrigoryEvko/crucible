# The Crucible Runtime
*Adaptive ML infrastructure.*

Three layers: **Hardware** — compute nodes, heterogeneous and replaceable. **The Model** — weights and computation graphs. **Crucible** — the runtime that abstracts hardware, persists state across node failures, and migrates to new devices.

Python describes. Crucible executes. The 492,000 lines of framework overhead between them become unnecessary. There is no training or inference — there is only a model in Crucible.

## Ontology

| Name | Role | Description |
|------|------|-------------|
| **F\*X** | Foundation | Fork of F\* — 10× more powerful. Proved allocators, SMT-optimal kernel synthesis, fusion engine. Extracts to hardware-optimized C++/CUDA. Every kernel proved optimal. Every allocator proved correct. The runtime is a theorem. |
| **Relay** | Body | Compute node inhabited by a Crucible daemon. Mortal. Replaceable. |
| **Keeper** | Spirit | Per-Relay daemon — self-healing, self-updating, autonomous. `crucible-keeper.service` starts at boot, discovers peers, joins mesh. Executes Augur's advice. |
| **Vigil** | Intellect | The model: DAG, weights, learned knowledge. Named for the Prothean AI. Never sleeps. |
| **Cipher** | Soul | Persistent state — DAG chain, weight snapshots, KernelCache, RNG state, proof certificates. Event-sourced. Survives death, reincarnates on new hardware. |
| **Canopy** | Collective | Mesh of Keepers — distributed awareness, gossip, consensus, self-healing. No master node. |
| **Vessel** | Interface | PyTorch — the 2,000+ ATen operators Crucible intercepts via the Dispatcher. |
| **Meridian** | Map | Startup calibration. Measured hardware truth. SMT-optimal topology, parallelism, communication, placement. Re-solves on topology change. |
| **Augur** | Sight | Continuous prediction, monitoring, model intelligence. Digital twin. Loss landscape analysis. Convergence bounds. Scaling laws. Bottleneck diagnosis. Recommendations engine. |
| **Crucible** | Whole | The organism. Everything together. |

---

## L0 — F\*X

**The proved foundation. Every claim carries a certificate.**

Fork of F\* proof assistant — 10× more powerful. F\*X is the source of truth: write, prove, extract to hardware-optimized C++/CUDA. The runtime is a theorem. No autotuning — SMT-optimal kernel synthesis for every (op, shape, device). Proved allocators. Proved fusion. Proof certificates persist in the Cipher and survive reincarnation.

**SMT kernel engine:** hardware spec = axioms, kernel config = variables, cost function = objective. For any shape on any device, F\*X finds THE optimal kernel configuration. One SMT query. Global optimum. Proved safe: no OOB, no bank conflicts, coalesced access, no register spill. CUTLASS tries 500 configs via benchmarking. Triton tries 50. F\*X solves the exact constraint system.

**Proved allocators:** jemalloc-like CPU allocator with size classes and thread-local caches — proved disjointness, bounded fragmentation. CUDA pool allocator with static plans — proved non-overlap, proved OOM-impossible. Arena bump-pointer — proved alignment and no UAF.

**Proved fusion engine:** fusion legality + fused kernel config as joint SMT problem. Intermediates proved to stay in registers/smem. The fused kernel is proved optimal for the composite computation.

**Extraction pipeline:** F\*X → C++26 (hot paths), F\*X → CUDA/HIP (kernel bodies). Every extraction preserves proved properties. Compile with Clang 22 / NVCC / hipcc.

---

## L1 — Hardware

**Compute hardware. Heterogeneous, replaceable.**

GPUs are ecosystems: tensor cores (1000 TFLOPS FP16 on H100), scalar ALUs (60 TFLOPS), four-level memory hierarchy (registers → shared memory → L2 → HBM), power envelopes. Gap between theoretical peak and achieved: 40-70%.

**Multi-vendor:** NVIDIA (sm_86/89/90/100), AMD (gfx1100/942), Intel XMX, Apple AMX, Google TPU MXU. Same computation described once in the Merkle DAG; different F\*X-compiled kernels per (content_hash, device_capability).

**Power management:** NVML exposes clocks, power, temperature, ECC errors. Memory-bound phases don't need full core clock — drop 30% for zero perf loss, significant savings.

**Health monitoring → Keeper:** ECC error trends, thermal throttling, clock degradation feed into the Keeper. A failing GPU gets load-reduced, data pre-replicated to healthy Relays (L13 Distribution, RAID). State is already replicated before failure completes. New hardware → fresh Keeper discovers mesh and Cipher → F\*X computes optimal kernels for new device → reshards for new topology → resumes exactly.

---

## L2 — Kernels

**SMT-optimal computation. Proved for every shape, every device.**

Current frameworks: static lookup (op + dtype → library kernel). Same kernel for 64×64 and 8192×8192, A100 and 3090, contiguous and transposed. No adaptation.

**Crucible generates kernels from C++ CUDA templates** parameterized by: tile size M/N/K, vectorization width, shared memory allocation, unrolling factor, precision (fp32/fp16/bf16/tf32/int8), access pattern. NVRTC compiles at runtime for exact shapes and hardware. For AMD: hipRTC with wavefront-adapted templates (64-wide vs 32-wide warps).

**F\*X SMT kernel engine** replaces autotuning entirely. Hardware spec = SMT axioms, kernel config = SMT variables, cost function = SMT objective. For any (op, shape, device): one query, global optimum. Proved safe: no OOB, no bank conflicts, coalesced access, no register spill. CUPTI validates predictions post-hoc; corrections feed back into Meridian calibration.

**KernelCache:** maps (content_hash, device_capability) → CompiledKernel. Content-addressing: identical ops on identical shapes produce identical hashes. Reuse across iterations, runs, models sharing sub-computations, even organizations. Multiple variants coexist per hash; best selected per device, alternatives benchmarked during dead time. Cache grows monotonically across restarts. Lock-free open-addressing hash table — zero overhead on hot path.

**Stream parallelism:** DFG reveals independent ops → launch on different CUDA streams → concurrent SM execution. Schedule compiled statically from topological sort + earliest-start-time assignment. Zero scheduling overhead at runtime.

**Deterministic Philox RNG:** cuRAND is hardware-dependent — different sequences on H100 vs 3090. Crucible uses Philox4x32: counter-based, platform-independent, stateless. Each op derives key from `hash(master_counter, op_index, content_hash)`. Each thread: `philox(thread_idx, op_key)` — ~10 integer instructions in registers. For memory-bound kernels like dropout: runs free in otherwise-wasted ALU cycles. Same (counter, key) → same bits on any architecture.

**Kernel fusion:** adjacent ops with single producer-consumer chain fuse into one kernel keeping intermediates in registers/shared memory, eliminating HBM round trips. Decision from DFG topology at compile time.

KernelCache is part of the **Cipher** — write-once, persists across reincarnations.

---

## L3 — Memory

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

## L4 — Operations

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

No training/inference distinction at L4 — same fallback, same recording, same compiled execution.

---

## L5 — Tensors

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

## L6 — Graphs

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

## L7 — The Merkle DAG

**Content-addressable, versioned computation graph.**

Central data structure. L1-L6 feed in, L8-L16 read/modify. L0 proves correctness. Simultaneously: computation specification, compilation cache key, guard system, versioning mechanism, and deployment artifact.

**RegionNodes:** compilable op sequences. **content_hash** = hash(schema_hashes, input shapes/strides/dtypes/devices, scalar values). Identical computation → identical hash, even across models. **merkle_hash** = content_hash + child hashes → O(1) equality for entire subtrees (like git commits).

**BranchNodes:** dynamic behavior. Guard = the op sequence itself. Mismatch at op N → branch arms for different paths. Both arms independently compilable. Shared suffixes share content_hashes and kernels.

BranchNodes are THE mechanism for everything that changes: architecture mutation (L10), attention replacement (L9), hyperparameter changes (L11), continuous learning (L14). Every adaptation is a branch. Every branch is versioned and rollbackable.

**KernelCache:** (content_hash, device_capability) → CompiledKernel. Lock-free reads. Persists across runs, models, organizations. The **computation genome** — every run enriches it.

**Atomic swaps:** background thread builds new DAG structures → one atomic pointer swap at iteration boundary → zero-downtime activation. Same mechanism for compilation activation, branch swaps, memory plan updates, topology changes, rollbacks. Coordinated across Canopy at same iteration boundary.

**The DAG IS the Vigil.** No torch.export(), ONNX, TorchScript. Same DAG trains and serves. Deploy = copy Cipher to Relay.

**The DAG IS the audit trail.** Root merkle_hash captures entire computation state. Divergence found in O(log N) by walking tree. Cryptographic provenance for regulatory compliance.

**Git operations on models:** diff (which regions changed), merge (non-overlapping clean, overlapping = conflict), bisect (binary search through versions for regression), cherry-pick (select specific region updates), blame (trace value through version history).

**LoopNodes in the DAG:** cycle semantics within acyclic hash framework. `merkle_hash = hash(body.content_hash ⊕ "loop" ⊕ feedback_signature ⊕ termination)`. Transforms DAG from computation snapshot to computation PROGRAM. Entire training run = one compact cyclic graph.

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

## L15 — Meridian+Augur

**Operational intelligence. Two time scales in one layer.**

Meridian = startup calibration (5-15s). Augur = continuous per-iteration monitoring.

**Meridian (startup):**
- GPU profiling: GEMM→actual TFLOPS, streaming copy→HBM/PCIe BW, NVML→power/temp/ECC/memory
- Network probing: N×N latency/bandwidth matrix, topology detection
- F\*X computes optimal kernel configs for measured hardware (SMT, not benchmarking)
- F\*X solves topology: TP×DP×PP factorization, placement, communication algorithms — all jointly
- Output: complete device-specific kernel set + MeridianConfig. Re-probes on topology change.

**Augur (continuous):**
- Digital twin: DAG + F\*X kernel predictions + Meridian corrections → iteration prediction (±5-10%)
- Per-kernel bottleneck classification: COMPUTE/MEMORY_BW/COMMUNICATION/BUBBLE/IMBALANCE
- Predicted vs actual monitoring. >10% drift → diagnose → trigger Meridian recalibration
- Recommendations ranked by expected_speedup × confidence, tagged auto-hot/auto-cold/manual
- Model intelligence (periodic): Hessian spectrum (Lanczos), gradient health, effective rank (randomized SVD), CKA layer redundancy, convergence prediction, Chinchilla scaling laws

---

## L16 — Ecosystem

**Cross-run learning, computation genome, federated intelligence.**

**Computation genome:** shared KernelCache. Content-addressing means different models with same sub-computations hit the same kernels. Every training run enriches the cache. Network effects: value grows superlinearly with contributors. Docker Hub for GPU computation.

**Federated learning:** DiLoCo + differential privacy. Sites train locally, send noised pseudo-gradients (sensitivity bounded by known gradient norms). No raw data/gradients leave sites. Auditable via Merkle trail.

**Cross-Vigil transfer:** import DAG subgraphs between Vigils. Same content_hashes → zero compilation. Pre-trained weights load into imported regions. Components become reusable libraries — not just weights but self-describing computation fragments.

**Model marketplace:** content-addressed DAG fragments + KernelCache + verified quality metrics. Download, splice, verify, commit. Architectures from best-in-class components discovered ecosystem-wide.

**Hardware co-design:** aggregated KernelCache reveals real workload patterns (shape distributions, sparsity patterns, bottleneck frequencies). Feed to hardware designers → next-gen silicon optimized for actual workloads → F\*X re-solves optimal kernels for new silicon → new data → co-evolution.

**What Crucible is not:** not intelligent, not AGI. A matmul is a matmul. It observes, compiles, adapts, distributes, heals, persists, evolves — mechanically, from measurements. The model determines the quality ceiling. Crucible removes infrastructure overhead so the model can reach its potential.

---

## Development Plan

**Phase 1: Foundation (DONE — 9.5K lines, 24 tests, Clang 22 + GCC 15)**

L4 Operations: TraceRing SPSC, MetaLog, recording pipeline. L6 Graphs: TraceGraph CSR. L7 Merkle DAG: RegionNode, BranchNode, content/merkle hashing. L3 Memory: MemoryPlan sweep-line, PoolAllocator. L4/L7 Compiled Tier 1: ReplayEngine, CrucibleContext, dispatch_op, divergence recovery. L2 Kernels: CKernel 146-op taxonomy. L14: Serialize/Deserialize, Cipher. L6 Graph IR: Graph.h, ExprPool, SymbolTable. L0 partial: Effects.h (fx::Alloc/IO/Block), Reflect.h (reflect_hash, reflect_print). Vessel: PyTorch adapter.

**Phase 2: F\*X Core (NEXT)**

Goal: F\*X foundation — proved allocators, SMT kernel engine, extraction pipeline.

- **SMT kernel engine:** Hardware spec → SMT axioms. Kernel config → SMT variables. Cost function → SMT objective. For any (op, shape, device): F\*X finds THE optimal kernel config. Proved optimal. Proved safe (no OOB, no bank conflicts, coalesced access). Fusion as joint SMT: producer+consumer configs solved simultaneously, intermediates proved to stay in registers/smem. Per-device kernel compilation: Meridian measures hardware, F\*X computes optimal kernels, compiles them.
- **Proved allocators:** CPU: jemalloc-like with size classes, thread-local caches, proved disjointness and bounded fragmentation. CUDA: pool allocator with static plans, proved non-overlap, proved OOM-impossible. Arena: bump-pointer for DAG metadata, proved alignment and no UAF via lifetime tracking.
- **Extraction:** F\*X → C++26 (hot paths, zero-overhead). F\*X → CUDA/HIP (kernel bodies, proved properties). Proof certificates: build manifest with cryptographic hash of all proved theorems.

**Phase 3: Meridian+Augur**

Goal: Hardware calibration + continuous monitoring as one operational intelligence layer.

- GPU profiling + network probing at startup. F\*X computes optimal topology + kernel set.
- Digital twin: DAG + F\*X kernel predictions + calibration corrections → iteration prediction.
- Continuous monitoring, bottleneck diagnosis, recommendations engine.
- Model intelligence: Hessian spectrum, gradient health, effective rank, CKA, scaling laws.

**Phase 4: Compiled Tier 2-3**

Goal: Shadow handles (~2ns/op) and CUDA Graph replay (~50ns/iteration).

- Shadow handles: ConductorTensorImpl with metadata pointing into PoolAllocator.
- Batched kernel launch: accumulate F\*X-proved-optimal kernels, one stream submission.
- CUDA Graph capture: record compiled kernels, replay at ~50ns/iteration.

**Phase 5: Keeper + Canopy + Cipher**

Goal: Distributed, self-healing, persistent, proof-carrying.

- Keeper daemon: systemd service, health monitoring, self-updating. Executes Augur's advice.
- Canopy mesh: gossip protocol, Raft consensus, peer discovery. No master.
- Cipher: hot tier (RAID redundancy), warm tier (NVMe), cold tier (S3/GCS). Event-sourced.
- Proof certificates in Cipher survive reincarnation. Hardware-specific proofs re-computed by F\*X on new hardware.

**Phase 6: L8-L12 Intelligence**

Goal: Model-aware optimizations, guided by Augur, verified by F\*X.

- L8: Token merging, early exit, adaptive patching.
- L9: Attention head classification, local losses, per-layer gradient strategy.
- L10: Layer growing/pruning, width mutation, architecture evolution.
- L11: Meta-gradients, per-layer LR from curvature, optimizer evolution.
- L12: Curriculum learning, manifold mixup, pipeline absorption.
- All optimizations are DAG branches (L7). F\*X proves new branches safe. Augur predicts improvement. The Keeper activates via atomic swap only if both F\*X approves (proved safe) and Augur approves (predicted improvement > threshold).

---

F\*X proves everything. Meridian measures truth. Augur monitors reality. The Keeper acts on proved-optimal decisions. The Vigil thinks within proved-safe infrastructure. The Cipher remembers everything, including the proofs. When the last Relay dies, the Cipher carries not just the model, but the certificates — ready to prove itself correct on whatever hardware comes next.

---

## The Layers (17 layers, L0–L16)

```
L16  Ecosystem        computation genome, federated learning, hardware co-design
L15  Meridian+Augur   calibration, digital twin, monitoring, recommendations
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
L3   Memory           proved allocators (jemalloc CPU, CUDA pool), static plans, OOM impossible
L2   Kernels          SMT-optimal kernel composition, proved fusion, KernelCache, Philox, streams
L1   Hardware         Relays, hardware profiling, multi-vendor, health → Keeper
─────────────────────────────────────────────────────────────────────────────
L0   F*X              proved foundation: SMT kernel synthesis, extraction to C++/CUDA
```

F\*X proves. Meridian maps. Augur sees. Vessel intercepts. Keeper serves. Vigil thinks. Cipher remembers. Canopy protects. Relay executes.



# Crucible Code Guide

*The canonical reference for writing Crucible code. Every rule has a cost-of-violation, a compiler-enforcement mechanism, and a discipline fallback. Nothing is style; everything is measured.*

Design target: **~5 ns/op foreground recording, ~2 ns shadow-handle dispatch, zero UB, bit-identical across hardware under BITEXACT recipes**. Every rule below serves one or more of those targets.

---

## I. Toolchain

| Preset    | Compiler              | Role                                          |
|-----------|-----------------------|-----------------------------------------------|
| `default` | GCC 16.0.1 (rawhide)  | Primary dev. Debug. Contracts + reflection.   |
| `release` | GCC 16.0.1            | Production. `-O3 -march=native -flto=auto -DNDEBUG` |
| `bench`   | GCC 16.0.1            | Release + `CRUCIBLE_BENCH=ON`                 |
| `tsan`    | GCC 16.0.1            | ThreadSanitizer (mutually exclusive with ASan)|
| `verify`  | GCC 16.0.1            | + Z3 formal verification suite                |

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

**Compiler enforcement:** `-Werror=conversion -Werror=sign-conversion -Werror=arith-conversion -Werror=enum-conversion -Werror=old-style-cast -fno-rtti`.

**Discipline:**
- Every semantic value is a strong type. No raw `uint32_t` for anything with meaning.
- IDs: `OpIndex`, `SlotId`, `NodeId`, `SymbolId`, `MetaIndex`, `KernelId` (all `CRUCIBLE_STRONG_ID(Name)` → `explicit(uint32_t)`, `.raw()`, `.none()`, `<=>`, no arithmetic).
- Hashes: `SchemaHash`, `ShapeHash`, `ContentHash` (all `CRUCIBLE_STRONG_HASH(Name)`).
- Enums: `enum class` with explicit underlying type. Convert via `std::to_underlying()` only.
- Bit reinterpretation: `std::bit_cast<T>()` only. `reinterpret_cast` is BANNED.
- Arithmetic: `std::add_sat` / `std::sub_sat` / `std::mul_sat` for all size/offset math.
- Casts: C-style cast is a compile error. `const_cast` is BANNED.

```cpp
// ✓ CORRECT — silent parameter swap impossible
void connect(OpIndex src, OpIndex dst, SlotId slot);

// ✗ WRONG — caller can swap src/dst/slot silently
void connect(uint32_t src, uint32_t dst, uint32_t slot);
```

### 3. NullSafe — `deref(v) ⇒ v ≠ null`

**Cost of violation:** crash (best case) or silent wrong answer.

**Compiler enforcement:** `-Werror=null-dereference -Werror=nonnull -Werror=nonnull-compare -Wanalyzer-null-dereference -Wanalyzer-possible-null-dereference`, contracts `pre(p != nullptr)` on all pointer params.

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
void process(const TraceEntry* entry)
    pre (entry != nullptr)
{
    // body can assume entry is non-null
}
```

### 4. MemSafe — `free(v) ⇒ ¬live(v)`

**Cost of violation:** use-after-free, double-free, memory corruption, RCE.

**Compiler enforcement:** `-fsanitize=address` in debug, `-Werror=use-after-free=3 -Werror=free-nonheap-object -Werror=dangling-pointer=2 -Werror=mismatched-new-delete -Wanalyzer-use-after-free -Wanalyzer-double-free`. `-fno-exceptions` eliminates the whole class of "destructor during stack unwind" bugs.

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
- Spin on atomic load with `CRUCIBLE_SPIN_PAUSE` (→ `_mm_pause` on x86, `yield` on ARM).
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
- Cipher tiers have explicit eviction: hot (LRU), warm (age), cold (S3 lifecycle policy).

### 8. DetSafe — `same(inputs) ⇒ same(outputs)`

**Cost of violation:** replay breaks, bit-exactness CI reddens, cross-vendor equivalence lost.

**Compiler enforcement:** no `-ffast-math` family. `-ffp-contract=on` (safe: FMA within a statement only). Pinned FTZ via recipe.

**Discipline:**
- DAG fixes execution order (topological sort with hash-based tiebreak).
- Memory plan fixes addresses (pool_base + offset, content-addressed).
- Philox4x32 RNG: counter-based, platform-independent. Zero RNG state anywhere.
- KernelCache keyed on `(content_hash, device_capability)`.
- Reduction topology: pinned binary tree sorted by UUID for BITEXACT recipes.
- No hash-table iteration order dependencies. Sort keys before iterating.
- No pointer-based ordering. Events have `(cycle, kind, sequence_number)`.

---

## III. C++26 Language Features — Opt Matrix

### Opt IN

| Feature | Paper | Usage |
|---|---|---|
| Contracts (`pre`/`post`/`contract_assert`) | P2900R14 | Every boundary function. Hot-path TUs use `contract_evaluation_semantic=ignore` |
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
| Exceptions | `-fno-exceptions` | Setup cost even unthrown; unwind tables in icache; use `std::expected` or `abort()` |
| RTTI | `-fno-rtti` | Vtable bloat; use `kind` enum + `static_cast` |
| Coroutines on hot path | discipline | Heap allocation, unpredictable latency |
| `volatile` for concurrency | P1152R4 deprecated | `volatile` does not order; use `std::atomic` |
| `[=]` capturing `this` | P0806R2 deprecated; `-Werror=deprecated-this-capture` | Lifetime footgun |
| `memory_order::consume` | P3475R2 deprecated; `-Werror=deprecated-declarations` | Compilers promote to acquire anyway |
| VLAs (`int arr[n]`) | `-Werror=vla` | Stack UB |
| C-style casts | `-Werror=old-style-cast` | Silent UB conversions |
| `reinterpret_cast` | code review grep-ban | Use `std::bit_cast<T>` |
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
| `std::add_sat` / `mul_sat` / `sub_sat` (C++26) | Saturation arithmetic at size-math sites |
| `std::breakpoint()` (C++26) | Hardware breakpoint for debug asserts |
| `std::unreachable()` (C++23) | After exhaustive switch to eliminate default branch |
| `std::countr_zero` / `popcount` (C++20) | Bit manipulation primitives |
| `std::span` (C++20) | Pointer+count replacement |
| `std::jthread` (C++20) | Auto-joining thread, no destructor-terminate |
| `<simd>`: `std::simd::vec`, `partial_load`, `reduce_min/max`, `simd::chunk` (C++26) | Default for new SIMD; library does per-ISA dispatch internally. DetSafe: integer reductions only — FP reductions forbidden (ISA-dependent rounding) |
| `std::atomic<T>::fetch_max` / `fetch_min` (C++26) | Monotonic update without CAS retry loop; replaces the `Monotonic<T>::bump` CAS pattern |
| `<debugging>` — `breakpoint_if_debugging`, `is_debugger_present` (C++26) | Pause when debugger attached, continue otherwise; tighten `CRUCIBLE_INVARIANT` |
| `std::latch` / `std::barrier` / `std::counting_semaphore` (C++20) | Pool throttling, one-shot init, fan-in waits — replace bespoke atomic+spin where ≥100 ns latency is acceptable |
| `std::is_within_lifetime` (C++26) | Debug-time UAF detection in `Linear<T>` / `ScopedView<>` |
| `std::source_location` (C++20) | Replace `__FILE__`/`__LINE__` in trace/assert/contract-violation paths |
| `std::is_sufficiently_aligned`, `std::aligned_accessor` (C++26) | Typed alternatives to `__builtin_assume_aligned` |
| `std::atomic_ref::address()` (C++26) | Bench/diagnostic — verify the atomic points where the planner said it does |
| `std::philox_engine` (C++26) | Standard counter-based RNG; cross-reference Crucible's `Philox.h` for bit-equivalence |
| `std::is_layout_compatible_with`, `std::is_pointer_interconvertible_with_class` (C++20) | Semantic companion to `static_assert(sizeof(T) == N)` for layout-strict structs |

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
| `std::rcu` / `<hazard_pointer>` (C++26) | We publish via `AtomicSnapshot<T>` + `atomic_ref` — finer control, DetSafe-documented; stdlib variants add overhead we don't need |
| `std::simd` FP reductions (`reduce_*` on float/double) | ISA-dependent rounding; breaks DetSafe bit-equality across platforms. Integer reductions are safe |
| `std::linalg` (C++26) | HS9 bans vendor BLAS; `<linalg>` dispatches through one anyway |
| `std::copyable_function` (C++26) | Heap allocation risk on capture-heavy lambdas; prefer `function_ref` (borrow) + explicit owned-pointer when ownership is needed |

---

## V. Compiler Flags

### Common (every build)

```
-std=c++26                           strict C++26, no GNU dialect drift
-fcontracts                          P2900 contracts
-freflection                         P2996 reflection
-fno-exceptions                      eliminate unwind tables
-fno-rtti                            eliminate vtables of typeinfo
-fno-strict-overflow                 don't optimize assuming signed overflow impossible
-fno-delete-null-pointer-checks      don't optimize away null checks
-fno-math-errno                      math functions don't set errno (faster, vec-friendly)
-ffp-contract=on                     FMA within a statement (safe for BITEXACT)
-ftrivial-auto-var-init=zero         P2795R5 — zero-init stack, kills InitSafe class
-fstack-protector-strong             stack canaries
-fstack-clash-protection             stack clash mitigation (~0.1% cost)
-fharden-control-flow-redundancy     GCC CFG hardening
-fcf-protection=full                 Intel CET / ARM BTI+PAC
-fno-omit-frame-pointer              readable traces (<1% cost)
-fno-plt                             direct calls
-fno-semantic-interposition          allow cross-TU inlining
-fvisibility=hidden                  hidden symbols = more inlining
-fvisibility-inlines-hidden          same for inline
-ffunction-sections                  one function per section (for --gc-sections)
-fdata-sections                      one var per section
-fno-common                          no tentative definitions
-fstrict-flex-arrays=3               strict flex array rules
-fsized-deallocation                 sized `delete` overloads
-D_FORTIFY_SOURCE=3                  glibc bounds checks
-D_GLIBCXX_ASSERTIONS                libstdc++ cheap asserts (release-safe)
```

### Debug preset

```
Common flags +
-Og -g                                debuggable but fortify/analyzer friendly
-fcontract-evaluation-semantic=enforce   check + terminate
-fsanitize=address                    ASan + LeakSan
-fsanitize=undefined,bounds-strict    UBSan + strict bounds
-fsanitize=shift-exponent,pointer-overflow  extra UBSan
-fno-sanitize-recover=all             abort on first violation
-D_GLIBCXX_DEBUG                      heavy container checks
```

### Release preset

```
Common flags +
-O3
-march=native -mtune=native
-DNDEBUG
-g                                    keep frame info for profiling
-flto=auto                            whole-program LTO, ~10-20% typical win
-fcontract-evaluation-semantic=observe  log but continue (or ignore on hot TUs)
-ftree-vectorize                      on by default at -O3
-fvect-cost-model=unlimited           aggressive auto-vec
-mprefer-vector-width=512             AVX-512 where HW supports
-fipa-pta                             interprocedural pointer analysis
-fgraphite-identity                   polyhedral loop framework
-floop-nest-optimize                  Graphite loop optimizer
-fno-trapping-math                    assume FP doesn't trap (vec-friendly)
+ PGO artifacts from bench workload
```

### Verify preset

Release flags + Z3 SMT solver + `-fcontract-evaluation-semantic=enforce` + `-fanalyzer`.

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
-fwrapv                           ~1% global perf cost; use `std::*_sat` at sites
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
- `-Wpadded` (off — we accept padding where it occurs)
- `-Wsuggest-*` (off — suggestions, not errors)

---

## VII. Attributes and Macros

`Platform.h` provides these. Use them deliberately.

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
// x86: _mm_pause() — 10-40ns via MESI invalidation
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
2. **`std::experimental::simd`** — portable when available.
3. **Intrinsics** (`<immintrin.h>`) — for AVX-512 / AVX2 when auto-vec misses. Wrap in named helpers.

Crucible's vectorizable hot paths: Philox RNG, hash mixing, TensorMeta extraction, reduction kernels.

### Link-Time Optimization (LTO)

`-flto=auto` — parallel LTO: whole-program inlining, dead code elimination, cross-TU constant propagation. Enabled unconditionally in release.

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

### Latency budgets per operation class

Every hot operation has a target p99 latency. Functions that exceed their budget by more than 2× are rewritten, not patched.

| Operation | p99 target | Notes |
|---|---|---|
| Shadow handle dispatch | 2-5 ns | `[[gnu::flatten]]`, no function call |
| TraceRing push | 5-10 ns | SPSC + `_mm_pause` + `alignas(64)` head/tail |
| Arena bump allocation | ≤2 ns | No branch, no lock |
| MetaLog append | 5-10 ns | SPSC, write-combined |
| Cross-core signal wait | 30-40 ns intra-socket | MESI floor; 100 ns cross-socket |
| Swiss-table lookup (hit) | 15-30 ns | Open addressing + SIMD probe |
| Contract check at boundary | 1-3 ns | `semantic=observe`; 0 on hot path with `ignore` |
| ExecutionPlan submit (warm) | 80-120 ns | Cache hit + doorbell |
| ExecutionPlan submit (cold, ≤5 patches) | 120-200 ns | Plan lookup + patch writes + SFENCE + doorbell |
| Syscall | 100-500 ns | Banned on hot path |
| `malloc` | 100-500 ns | Banned on hot path |

The budget table is a hard contract. A PR that regresses any hot-path budget by >10% requires an explicit "performance tradeoff" justification in the commit message, reviewed by at least one other maintainer.

### Hot functions fit in one cache line

Target: every hot-path function compiles to ≤64 bytes of machine code (one I-cache line fetch). Audit via `objdump -d`. Commit a snapshot of hot-function disassembly under `build/asm-snapshot/` and review its diff on every PR like source code.

If a hot function exceeds 64 bytes of machine code, either (a) the design is wrong (split into hot + cold), (b) a dependency is bloated (eliminate), or (c) the budget was optimistic (document the new budget and notify).

### Cold-path outlining is mandatory

Every `[[unlikely]]` branch body that is more than one return statement or one early-abort is **outlined** to a `[[gnu::cold, gnu::noinline]]` helper function. Rationale: `[[unlikely]]` only steers the branch predictor; it does not prevent the cold body's code from occupying an I-cache line adjacent to the hot path.

Rule: if a `[[unlikely]]` block has more than 8 lines of non-trivial work, outline it. The cold helper sits in `.text.unlikely` (a separate section) and never touches the L1 I-cache during normal execution.

### PGO + AutoFDO for release

Release builds are profile-guided; non-PGO release is a dev build, not production. Typical gain on branchy code: **5-15%**.

```bash
# 1. Instrumented build
cmake --preset release -DCRUCIBLE_PGO=generate && cmake --build --preset release

# 2. Run the bench suite under production workload
./build/bench/bench_dispatch && ./build/bench/bench_trace_ring

# 3. Rebuild with profile
cmake --preset release -DCRUCIBLE_PGO=use && cmake --build --preset release
```

Alternative (continuous profiling from production runs): **AutoFDO** via `-fauto-profile=<profile.afdo>` fed from `perf record`. Same wins, no instrumented build.

### Per-deployment microarchitecture targeting

- **Local development**: `-march=native -mtune=native`. Exact features of the build machine.
- **Distribution / production**: the specific minimum microarch of the deployment fleet, declared in the build manifest. No project-wide default — the choice is deployment-local and documented alongside the fleet's hardware spec.
- **Multi-versioned dispatch**: use `[[gnu::target_clones(...)]]` for routines that have microarchitecture-specific fast paths (e.g., AVX2 vs AVX-512 vs NEON). First call pays one indirect jump; subsequent calls are direct.

No single `-march=` default ships with Crucible. The build owner picks it for their fleet.

---

## IX. Concurrency Patterns

### The only two threads

```
Foreground (hot):  records ops at ~5 ns each via TraceRing
Background (warm): drains ring, builds TraceGraph, memory plan, compiles
```

No third thread except OS / OS-adjacent (systemd, signal handlers). Multiple background workers allowed inside Mimic for parallel kernel compilation.

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
| KernelCache slots | bg | fg | CAS on atomic slot, fg reads acquire |
| RegionNode::compiled | bg | fg | atomic pointer swap (release/acquire) |
| Cipher warm writes | bg | (read by any peer) | Raft-committed |

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

For atomic CAS on an element of a plain array (e.g. SwissCtrl bytes, KernelCache slots) without paying `std::atomic<T>` per-element overhead:

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
// Boundary: contract-checked
std::expected<PlanId, Error> submit_plan(PlanId id, std::span<const PatchValue> patches)
    pre  (!id.is_none())
    pre  (std::all_of(patches.begin(), patches.end(), valid_patch))
    post (r) (r.has_value() || !r->is_none())
{
    return runtime::submit_plan_inner(id, patches);
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

No exceptions (compiled out via `-fno-exceptions`). Three tiers of error response:

| Class | Mechanism | Runtime cost | Example |
|---|---|---|---|
| **Impossible** (contract violation) | `pre` / `post` / `contract_assert` | 0 ns on hot path (semantic=ignore), check in debug | Null pointer, OOB index, invariant violation |
| **Expected-but-rare** | `std::expected<T, E>` return | ~1 ns (branch on `.has_value()`) | Parse error, shape out of bucket, peer timeout |
| **Catastrophic** | `crucible_abort(msg)` | — | OOM, hardware fault, corrupt state, FLR failure |

### Contract semantics per TU

Hot-path TUs compile contracts with `ignore` semantic. Boundary TUs use `enforce`. Switch per file:

```cpp
// At top of a hot-path .cpp:
#pragma GCC contract_evaluation_semantic ignore

// Boundary .cpp:
#pragma GCC contract_evaluation_semantic enforce
```

CI builds everything with `enforce`. Release builds default `observe` (log + continue) except hot TUs explicitly marked `ignore`.

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
compile_kernel(fx::Bg bg, Arena& arena,
               const KernelNode& k, const TargetCaps& caps)
    pre (k.recipe != nullptr)
    pre (k.tile != nullptr)
{
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

### Assertion macro triad

```cpp
// ── CRUCIBLE_ASSERT ────────────────────────────────────────────
// Always-on boundary precondition. Cheap check, runs in release.
// Maps to contracts; respects contract-evaluation-semantic.
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
#ifdef NDEBUG
  #define CRUCIBLE_INVARIANT(cond) [[assume(cond)]]
#else
  #define CRUCIBLE_INVARIANT(cond) do {                           \
      if (!(cond)) [[unlikely]] {                                  \
          fprintf(stderr, "invariant failed: %s (%s:%d)\n",        \
                  #cond, __FILE__, __LINE__);                      \
          std::breakpoint();                                       \
          std::abort();                                            \
      }                                                            \
  } while (0)
#endif
```

**When to use which:**
- `CRUCIBLE_ASSERT` — public API entry. Contracts handle it.
- `CRUCIBLE_DEBUG_ASSERT` — SPSC ring bounds, arena bump sanity, RNG counter — hot path, can't afford a branch.
- `CRUCIBLE_INVARIANT` — loop trip counts, alignment, range bounds. The optimizer uses it.

### Abort path

```cpp
[[noreturn]] CRUCIBLE_COLD
void crucible_abort(const char* msg) noexcept {
    fprintf(stderr, "crucible: fatal: %s\n", msg);
    cipher::emergency_flush();       // try to salvage hot-tier state
    std::abort();                    // SIGABRT → core dump if enabled
}
```

Every abort path is `[[gnu::cold]]` + `noinline` so it lives in a cold section and doesn't pollute hot icache.

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
| `bit_exact_replay_invariant` | DetSafe | Every PR |
| `cross_vendor_step_invariant` | DetSafe | Release gate (multi-backend) |
| `fleet_reshard_replay` | DetSafe + BorrowSafe | Release gate |
| `bit_exact_recovery_invariant` | DetSafe + MemSafe | Release gate |
| `checkpoint_format_stability` | DetSafe + LeakSafe | Every PR |
| `tsan_spsc_ring_*` | ThreadSafe + BorrowSafe | Every PR (tsan preset) |
| `asan_arena_lifetime_*` | MemSafe + LeakSafe | Every PR (default preset) |
| `ubsan_numeric_*` | TypeSafe + InitSafe | Every PR (default preset) |
| `stress_repeat_50x` | ThreadSafe (race detector) | Nightly |

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
6. **Every contract has a test that violates it.** Use `EXPECT_DEATH(fn(bad_args), ".*contract.*")` to prove contracts fire.

### Sanitizer preset matrix

```bash
# ASan + UBSan + analyzer: MemSafe, NullSafe, TypeSafe, InitSafe
cmake --preset default && cmake --build --preset default && ctest --preset default

# TSan: ThreadSafe, BorrowSafe
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan

# Stress (repeat-until-fail ×50): ThreadSafe race detection
ctest --preset stress

# Release: perf-bound tests, catches release-only codegen bugs
cmake --preset release && cmake --build --preset release && ctest --preset release

# Verify: all axioms via Z3 formal proofs
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

`bench/` and `test/` are separate trees. Bench asserts measure **numerical targets** (≤ 10 ns/op, ≤ 500 KB binary size). A test that accidentally measures latency is bad design — split it. Benchmarks can be noisy; tests cannot.

### Coverage targets

- Every public `.h` has a test.
- Every `pre` / `post` has a test that exercises both success AND violation.
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
| Optional x86 uplift | AVX-512, AMX | Opt-in per target via `target_clones`; never assumed |
| Min ARM baseline | **ARMv8.2-A + NEON** | Graviton 2+, Apple M1+ |
| Float representation | **IEEE 754** | `-fno-fast-math` enforces |
| Stack size | **8 MB** (Linux default) | Large arrays → arena, not stack |
| Canonical VA bits | **48** (x86-64 without LA57) | Pointer-tagging schemes assume this |
| Thread API | **pthreads** (via `std::jthread`) | Linux only |
| Syscall ABI | **Linux x86_64 / aarch64** | No Windows, no macOS in production |

### Platform checks at build time

```cpp
// In Platform.h or a dedicated Assumptions.h
static_assert(sizeof(void*) == 8, "64-bit required");
static_assert(std::endian::native == std::endian::little, "little-endian required");

#if defined(__aarch64__) && defined(__APPLE__)
  #error "Apple Silicon has 128-byte cache lines — alignas(64) is insufficient. Audit before enabling."
#endif

#if !defined(__x86_64__) && !defined(__aarch64__)
  #error "Crucible targets x86-64 and aarch64 only."
#endif
```

### When the assumptions change

- **New architecture** (RISC-V, Power): audit every `alignas(64)`, every `_mm_pause`, every endian-sensitive `bit_cast`.
- **Apple Silicon target**: cache line is 128 B. All `alignas(64)` becomes `alignas(CRUCIBLE_CACHE_LINE)` where the macro resolves per platform.
- **ARM 16 KB pages** (Apple, some Android): audit huge-page / `mmap` / `MADV_HUGEPAGE` code.
- **32-bit or 128-bit target**: not supported. Reject.

### Determinism across platforms

Under `BITEXACT_STRICT`, the same IR + same seed produces byte-identical output on any supported platform. This works because:

- IEEE 754 + `RN` rounding + `-fno-fast-math` = deterministic FP
- Philox4x32 is platform-independent (counter-based, bit-exact spec)
- Memory plan offsets are content-addressed (no alloc-order dependency)
- Canonical reduction topology (UUID-sorted binary tree)
- `std::bit_cast` for serialization — no endian-dependent `reinterpret_cast`

CI verifies with `cross_vendor_step_invariant`. A new platform must pass this test before shipping.

---

## XV. Headers, Includes, and Modules

### Header discipline

1. **`#pragma once`** on every header. No include guards.
2. **Self-contained.** Every header compiles standalone. Add required includes directly; never rely on transitive pull-in.
3. **IWYU** (include-what-you-use). If a `.cpp` uses `std::span`, include `<span>` — not via some project header that happens to pull it.
4. **No circular includes.** Refactor: one side gets a forward declaration, full include in the `.cpp` only.
5. **Forward declare in headers whenever possible.** Full definitions only when needed (inline methods, templates, `sizeof`).
6. **No precompiled headers.** PCH hides dependency bugs and complicates CMake. If build is slow, audit headers.

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

- All code in `namespace crucible` or nested (`crucible::mimic::nv`, `crucible::forge::phase_d`).
- **No** `using namespace std;` at file or namespace scope, ever. Inside function body OK when the benefit is clear.
- Anonymous namespaces for TU-local helpers. Not the `static` keyword (deprecated for this purpose).
- Don't use `using X::foo;` to leak implementation details from a nested namespace to a parent.

### C++20 Modules — deferred

GCC 16 has ABI-stable modules but we defer: CMake/ninja integration still maturing, debug info less mature than headers, and `-flto=auto` on header-only hot paths already delivers cross-TU inlining. Revisit when clean-rebuild exceeds ~30 s or modules become part of the public API. Until then: `#include`-based, header-only hot, split cold.

### One-definition rule (ODR) discipline

- `inline` on every function in a header (including single-line getters).
- `inline` on every `constexpr` variable in a header.
- Template specializations: inline + in a header.
- Anonymous namespace for TU-local: never in a header.

---

## XVI. Safety Wrappers

Library types in `include/crucible/safety/` that mechanize the axioms from §II at compile time. Every wrapper is a phantom-type newtype with **zero runtime cost** — `sizeof(Wrapper<T>) == sizeof(T)`, same machine code as the bare primitive under `-O3`. Generated assembly is indistinguishable from the unwrapped equivalent.

### Header catalog

| Header | Axioms it enforces | Role |
|---|---|---|
| `Linear.h` | MemSafe, LeakSafe, BorrowSafe | Move-only `Linear<T>`. `.consume() &&` takes ownership; `.peek() const&` borrows. Construction is `[[nodiscard]]`. |
| `Refined.h` | InitSafe, NullSafe, TypeSafe | `Refined<Pred, T>` — predicate checked by contract at construction; function bodies treat the invariant as `[[assume]]` downstream. |
| `Secret.h` | DetSafe + information-flow discipline | Classified-by-default `Secret<T>`. Escapes only via `declassify<Policy>()` with a grep-able `secret_policy::*` tag. |
| `Tagged.h` | TypeSafe | Phantom tags for provenance (`source::FromUser`, `source::FromDb`, `source::Internal`) and trust (`trust::Verified`, `trust::Unverified`). Mismatch at call sites = compile error. |
| `Session.h` | BorrowSafe | Type-state protocol channels. Each `.send()` / `.recv()` returns a new type carrying the remaining protocol. Wrong order or missing step = compile error. State lives in the type; zero runtime cost. |
| `Checked.h` | TypeSafe, DetSafe | `checked_add` / `wrapping_add` / `trapping_add` over `__builtin_*_overflow`. `std::add_sat` / `std::mul_sat` / `std::sub_sat` pass-through for saturation. |
| `Mutation.h` | MemSafe, DetSafe | `AppendOnly<T>` — no erase/resize. `Monotonic<T, Cmp>` — advance-only with contract guard on the step. |
| `ConstantTime.h` | DetSafe (side-channel resistance) | `ct::select`, `ct::eq`, branch-free primitives for crypto paths and Cipher key handling. |

Each header is ≤150 lines, header-only, depending only on `<type_traits>` and `Platform.h`.

### Usage rules

1. **Public API params wrap raw primitives.** `fn(Refined<positive, int> n)` — never `fn(int n)`. Bodies then trust the invariant without re-validating.
2. **Every resource type wraps in `Linear<T>`** — file handles, mmap regions, TraceRing, channel endpoints, arena-owned objects with drop semantics.
3. **Every load-bearing predicate gets a named alias** — `PositiveInt`, `NonNullTraceEntry`, `ValidSlotId`, `NonEmptySpan<T>`. Not anonymous refinements at call sites.
4. **Every classified value wraps in `Secret<T>`** — Philox keys, Cipher encryption keys, private weights, credentials. Declassification requires a `secret_policy::*` tag.
5. **Every trust-boundary crossing uses `Tagged<T, source::*>`** — deserialized input, network payload, FFI return. Sanitized-only APIs demand `source::Internal`.
6. **Every fixed-order protocol uses `Session<...>`** — handshakes, init sequences, channel lifecycles, plan-chain acquisition.
7. **Every append-only or monotonic structure wraps** in `AppendOnly<>` / `Monotonic<T, Cmp>` — event logs, generation counters, version numbers, Cipher warm writes.
8. **Every crypto path uses `ct::*` primitives** for comparisons and selections. Non-CT code in a `with Crypto` context is a review reject.

### Compiler enforcement

- `-Werror=conversion` + the wrapper types together prevent accidental unwrapping across boundaries.
- `-Werror=use-after-move` + `Linear<>`'s deleted copy constructor catches double-consume at compile time.
- `[[nodiscard]]` on every wrapper type's constructor forces the caller to capture the return value.
- Contracts on `Refined<>` and `Monotonic<>` constructors fire at construction sites under `semantic=enforce` (debug, CI, boundary TUs) and under `semantic=ignore` on hot-path TUs they compile to `[[assume]]` hints, optimizing downstream code as if the invariant always holds.
- Deleted copy + defaulted move on `Linear<>` / `Secret<>` / `Session<>` means the compiler rejects accidental duplication.
- Contract violations abort via `std::terminate` (P1494R5), never invoke undefined behavior.

### Review enforcement

Rules for code review and grep-guards:

- `declassify<` without a `secret_policy::*` policy tag → reject.
- `const_cast` → reject (banned per §III).
- `reinterpret_cast` → reject; use `std::bit_cast`.
- `std::chrono::system_clock` → reject (use `steady_clock` or `rdtsc`).
- A new public API taking raw `int`, `size_t`, `void*`, or `T*` without a wrapper → questioned on review; almost always rewritten.
- A new resource-carrying type without `Linear<>` → questioned; must have justification.
- Any `[[unlikely]]` body of more than 8 non-trivial lines without being outlined into a `CRUCIBLE_COLD` helper → reject.

### GCC 16 contracts — implementation gotchas

Real-world issues encountered implementing the wrappers.  Document them here so the next person doesn't rediscover them.

**Contract clause order on member functions.**  On a constructor or member function, `pre` / `post` must appear *after* `noexcept` and before the member initializer list (or body).  Newlines between clauses don't cost compilation, but `pre` separated from the colon by other tokens will not parse.

```cpp
// ✓ CORRECT
constexpr R(int x) noexcept pre(x > 0) : v{x} {}

// ✗ WRONG — pre before noexcept
constexpr R(int x) pre(x > 0) noexcept : v{x} {}

// ✗ WRONG — pre separated from colon by a different clause order
constexpr R(int x)
    pre(x > 0)         // parser error: expected ‘;’
    noexcept
    : v{x} {}
```

**`-fcontracts` and `-freflection` require `-std=c++26`.**  CMake's compiler-probe step runs before the project's `CMAKE_CXX_STANDARD` takes effect, so putting these flags in `CMAKE_CXX_FLAGS` via the preset breaks configuration.  Instead, set them at target level via `target_compile_options(crucible INTERFACE -freflection -fcontracts)` after `project()` has declared the standard.

**`handle_contract_violation` must be defined by the program.**  GCC 16 / libstdc++ 16 does not ship a default handler.  Every program that enables contracts must provide one; otherwise the link fails with `undefined reference to handle_contract_violation(std::contracts::contract_violation const&)`.  The project default belongs in a shared `CrucibleContractHandler.cpp` wired to `crucible_abort()`.

```cpp
#include <contracts>
void handle_contract_violation(const std::contracts::contract_violation& v) {
    fprintf(stderr, "contract: %s\n", v.comment());
    std::abort();
}
```

**Postconditions with value parameters.**  P2900R14 forbids using a by-value parameter in a postcondition unless it is `const`.  `fn(int n) post(r: r == n*2)` fails with "value parameter used in postcondition must be const"; write `fn(int const n) post(r: r == n*2)` instead.  Fix is trivial once you know; confusing if you don't.

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

**Runtime libstdc++ resolution.**  Binaries compiled with the local GCC 16 tree link against that tree's `libstdc++.so.6` which is newer than the system's.  Without an rpath, they fail at runtime.  The `default` preset's `CMAKE_EXE_LINKER_FLAGS` / `SHARED_LINKER_FLAGS` / `MODULE_LINKER_FLAGS` all carry `-Wl,-rpath,$(gcc16 prefix)/usr/lib64`; verify with `readelf -d build/test/test_X | grep RUNPATH`.

### What the wrappers do not cover

- **Flow-sensitive refinement propagation.** GCC does not prove a `Refined<>` invariant holds across multiple function boundaries. The wrapper checks at construction; the body inside the function trusts the invariant. Chaining requires either re-wrapping at each boundary or an SMT-discharged proof via the `verify` preset (§I).
- **Alias analysis.** `Linear<>` catches double-consume on a single value but not two pointers to the same underlying object created through unsafe channels. Review + `-fsanitize=address` + the axioms from §II are the line of defense.
- **Compile-time information flow.** Branching on a `Secret<>` value is not rejected at compile time. `ct::*` primitives and the constant-time discipline (§III opt-out of `memory_order::consume`, crypto paths using `ct::select`) are opt-in.

For those properties, the `verify` preset runs SMT (Z3) over the annotated code; see §I.

---

## XVII. Identifier and Readability Discipline

Code tells a story. Reading a function should read like prose — a noun subject, a verb action, an adjective condition. Every identifier reads as a sentence fragment that narrates what the thing IS or DOES. The primary reader of this codebase is an agentic LLM — so names carry semantic weight equal to types. Under `-fno-rtti` + `-fno-exceptions` an identifier's spelling is often the only remaining signal an automated tool has about semantics; ambiguous names degrade grep, code review, and future refactoring equally.

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
- **Banned**: single-character identifiers (`g`, `t`, `e`, `s`, `x`, `a`, `b`, `n`, `r`) in any scope except numeric `for` loop induction over a range.
- **Banned**: two-character identifiers (`ty`, `ex`, `fn`, `st`, `pt`, `tc`, `ok`, `nf`). Abbreviations are not names.
- **Discouraged**: identifiers ≤ 3 characters. Prefer `scope` over `sc`, `param` over `p`, `binder` over `b`, `result` over `r`, `grade` over `g`, `index` over `i`.

### Allowed exceptions (canonical technical terminology)

Five categories of short names are exempt because they ARE the standard vocabulary in their domain — renaming them would make the code less recognizable, not more:

- **Crucible ontology primitives**: `Vigil`, `Keeper`, `Relay`, `Cipher`, `Canopy`, `Meridian`, `Augur`, `Vessel` are full words and fine at any length. Their short aliases in hot paths are not — use the full name.
- **Hot-path idiomatic short names** canonical in Crucible: `op` (Op), `args` (const Expr* const*), `nargs` (uint8_t), `ndim` (uint8_t), `dtype` (ScalarType), `arena` (fx::Alloc), `ctx` (CrucibleContext), `bg` (fx::Bg token), `fg` (fx::Fg token), `ms` (MetaIndex strong ID), `ring` (TraceRing&). Established in TraceRing.h / ExprPool.h / MerkleDag.h; rename would be churn.
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

Lean code in `lean/Crucible/` and C++ test names obey the same rule: compose a question: `isConsumed_impliesGradeZero`, `bitExact_ofReplay`, `wellFormed_ofChecked`. Never `lemma1`, `wf_thm`, `tc_test`. Test-suite names that describe the invariant should lead with the invariant name, not the test index.

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

---

## XIX. When to Update This Guide

Update rules here when:

1. A new UB class is discovered in production or CI → add to footgun catalog.
2. A new GCC/libstdc++ feature lands that measurably helps an axiom → add to opt-in.
3. A measurement invalidates a "perf wisdom" here → replace with the measured version.
4. A rule is consistently violated without consequence → investigate whether the rule is still necessary.

Every change to this guide is a semi-major commit with rationale. This guide is the contract between engineer and codebase.

---

## XX. The Cost Hierarchy

When in doubt, the cost of failure ordering is:

```
Correctness  >  Determinism  >  Security  >  Latency  >  Throughput  >  Code size
```

Never trade correctness for latency. Never trade determinism for throughput. Security is non-negotiable (replay determinism IS a security property). Latency matters because of the ~5 ns/op budget — but only after the first three.

---

ZERO COPY. ZERO ALLOC ON HOT PATH. EVERY INSTRUCTION JUSTIFIED.
L1d = 48 KB. L2 = 2 MB. Squat upfront, point into it, write into it.

IF VARIANCE > 5% IN BENCHES — IGNORE RESULTS, THE LAPTOP IS THROTTLING.

IF A RULE HERE CONFLICTS WITH REALITY — MEASURE, FIX REALITY, OR FIX THE RULE.
NEVER WRITE CODE THAT CONTRADICTS BOTH.
