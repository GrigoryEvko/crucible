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
