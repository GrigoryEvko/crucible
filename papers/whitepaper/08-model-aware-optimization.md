# 8. Model-Aware Optimization

This section describes optimizations that Crucible enables through its DAG representation and runtime observability. These are described as designs, drawing on established techniques from the ML literature. Crucible's contribution is not the individual techniques but the infrastructure that integrates them: each optimization is a DAG branch (Section 3.4), verified before activation, and rollbackable via atomic swap.

## 8.1 Token-Level Adaptation

Fixed tokenization assigns uniform compute to all input regions regardless of information density. Crucible enables per-input, per-layer adaptation:

**Token merging.** Pairwise cosine similarity between adjacent token representations after layer N; tokens above a similarity threshold are merged (averaged). This reduces sequence length and thus attention cost (which scales quadratically). The merge threshold adapts per input and per layer --- an image of a uniform surface merges aggressively; a complex diagram merges minimally. Based on [Bolya et al. 2023].

**Early exit.** Per-token convergence monitoring: measure the norm of the difference between a token's representation at layer N and layer N-1. Tokens whose representations have stabilized skip remaining layers. Tokens are bucketed into convergence groups for batched execution efficiency.

**Adaptive patching (images).** Quadtree decomposition by information content (gradient magnitude, frequency, entropy). Low-information regions receive fewer, larger patches; high-information regions receive more, smaller patches.

Each adaptation is compiled as a BranchNode in the DAG, with the non-adapted path as the fallback arm.

## 8.2 Layer-Level Analysis

Crucible groups operations by scope hash and analyzes each layer independently using Augur's diagnostics (Section 6.4).

**Attention head classification.** From recorded attention matrices: positional heads (diagonal band, content-independent) can be replaced by depthwise convolution at O(n·k) instead of O(n²); global heads (attending to fixed landmarks) can be replaced by gather + broadcast at O(n); averaging heads (near-uniform attention) can be replaced by mean pooling at O(n); dead heads (near-zero output) can be removed. Content-routing heads that exhibit sparse, input-dependent patterns are retained or replaced with hash-based routing.

**Local learning signals.** Per-layer loss functions (predictive coding, contrastive, reconstruction) inserted as DAG modifications. These provide gradient signal with depth-1 backpropagation paths, avoiding vanishing gradients in early layers.

**Per-layer gradient strategy.** From measured gradient SNR, Jacobian rank, and gradient norms: layers with high SNR receive standard backpropagation; layers with moderate SNR receive natural gradient (K-FAC [Martens and Grosse 2015]); layers with near-zero SNR receive synthetic gradients; converged layers are frozen entirely.

## 8.3 Architecture Mutation

The DAG IS the architecture. Modifying the DAG is architecture search. Every modification is a verified, rollbackable branch.

**Layer growing.** Loss plateau detected by Augur + capacity analysis → insert a new layer at the position with highest gradient magnitude. Initialize via identity mapping, distillation, or random. The new architecture is a BranchNode: old architecture (arm A) vs new (arm B). Run both on validation data. If B ≥ A: atomic swap. If B < A: discard.

**Layer pruning.** CKA > 0.95 between adjacent layers → one computes a nearly identical function. Create a branch skipping the redundant layer. Verify on validation data. Commit or discard.

**Width mutation.** Effective rank consistently lower than hidden dimension → reduce width via SVD projection + adapter layers. The Vigil shrinks where it has excess capacity.

**Progressive growing.** Start with a small architecture. Grow on plateaus, widen when effective rank saturates, prune when layers become redundant. The architecture trajectory is determined by the data and the loss landscape, not by human guess.

## 8.4 Training Optimization

The entire training loop (forward + backward + optimizer) is in the DAG and therefore observable and modifiable.

**Meta-gradients.** The learning rate influences the next parameter update, which influences validation loss. Computing ∂(validation_loss)/∂(learning_rate) via one additional backward pass allows the learning rate (and weight decay, momentum parameters, etc.) to tune themselves by gradient descent on validation loss. No grid search, random search, or Bayesian optimization.

**Per-layer learning rate from curvature.** Hessian diagonal (from Augur's periodic Hessian-vector products) gives per-parameter curvature. Optimal learning rate is proportional to the inverse curvature.

**Curriculum learning.** Per-sample loss is observable during recording. Samples can be ordered by difficulty: easy-to-hard, hard-first, or random. The ordering itself is a DAG-level decision that can be evaluated empirically and adapted.

**Automatic mixed precision.** Run each operation in multiple precisions (FP32, BF16, FP8), measure per-operation quality impact, select the cheapest precision maintaining quality within tolerance. Per-operation, per-training-stage, not a static allowlist.

## 8.5 Verification and Activation

Every optimization in this section follows the same protocol:

1. Augur's diagnostics suggest an optimization and predict its expected improvement.
2. The optimization is implemented as a DAG branch (BranchNode or DAG modification).
3. FX verifies safety properties of the new branch where applicable (memory bounds, kernel access safety).
4. The new branch is evaluated on validation data alongside the current branch.
5. If quality is maintained and improvement is confirmed: the Keeper activates the new branch via atomic swap.
6. If quality degrades: the branch is discarded. The Vigil continues with the previous plan.

This provides built-in A/B testing for every optimization, with instant rollback. No optimization is irreversible.
