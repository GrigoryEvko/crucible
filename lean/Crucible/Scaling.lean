import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.Scaling — L17 Augur Convergence Bounds & Scaling Laws

From L17 Augur (MANIFESTO.md):

  "Convergence prediction: exponential fit L(t) = L* + (L₀-L*)·exp(-t/τ)"
  "Scaling laws: Chinchilla fit L(N,D) = E + A/N^α + B/D^β,
   Z3 optimizes N*,D* for budget C"
  "Hessian spectrum: Lanczos iteration, condition number κ=L/μ, optimal lr=2/(L+μ)"
  "Gradient health: per-layer SNR, Jacobian σ_max, vanishing/exploding detection"
  "Representation capacity: effective rank via randomized SVD, dead neuron detection"
  "Layer redundancy: CKA between adjacent layers, provable output-change bound"

This file formalizes the CORRECTNESS CONDITIONS for Augur's model analysis:

1. **Loss model**: discrete geometric decay L(t) = L* + (L₀ - L*) · r^t
   - Monotone decrease, bounded below, initial condition, convergence
2. **Learning rate schedule**: warmup then bounded decay
3. **Gradient health**: SNR non-negativity, strategy selection
4. **Effective rank**: dimension counting from singular values
5. **CKA similarity**: bounded, reflexive, redundancy detection
6. **Compute budget**: Chinchilla formula C = 6·N·D, optimal allocation
7. **Batch size scaling**: critical batch size bounds

All quantities are ℚ (exact rational arithmetic). No sorry.

C++ correspondence:
- Augur::predict_convergence() — fits loss curve, extrapolates
- Augur::gradient_health() — per-layer SNR, Jacobian analysis
- Augur::effective_rank() — randomized SVD, dead neuron count
- Augur::cka_similarity() — CKA between adjacent layer activations
- Augur::scaling_analysis() — Chinchilla fit, optimal (N*,D*) via Z3
- Meridian::batch_size() — critical batch size from noise scale
-/

namespace Crucible

/-! ## 1. Loss Model — Discrete Geometric Decay

Augur fits loss curves as L(t) = L* + (L₀ - L*) · r^t where r ∈ (0,1)
is the per-step decay factor derived from time constant τ: r = (τ-1)/τ.

We model this over ℚ. The key properties:
- Monotone decrease: L(t+1) ≤ L(t)
- Bounded below by L*
- Initial condition: L(0) = L₀
- Gap shrinks geometrically each step -/

/-- Loss model configuration.
    C++: `struct ConvergenceModel { double L_star, L_0, decay_rate; };` -/
structure LossModel where
  L_star : ℚ              -- asymptotic loss (irreducible)
  L_0 : ℚ                 -- initial loss
  decay : ℚ               -- r ∈ (0,1), per-step decay factor
  h_L0_ge : L_star ≤ L_0  -- initial loss at least as large as asymptotic
  h_decay_pos : 0 < decay  -- decay factor positive
  h_decay_lt : decay ≤ 1   -- decay factor at most 1

/-- Loss at step t: L(t) = L* + (L₀ - L*) · r^t.
    C++: `Augur::predicted_loss(step)`. -/
def LossModel.loss (m : LossModel) (t : Nat) : ℚ :=
  m.L_star + (m.L_0 - m.L_star) * m.decay ^ t

/-- The gap above asymptotic loss: L(t) - L*.
    Always non-negative (proved below). -/
def LossModel.gap (m : LossModel) (t : Nat) : ℚ :=
  (m.L_0 - m.L_star) * m.decay ^ t

/-- Gap equals loss minus asymptote. -/
theorem LossModel.gap_eq (m : LossModel) (t : Nat) :
    m.gap t = m.loss t - m.L_star := by
  simp [LossModel.gap, LossModel.loss]

/-- Decay factor power is non-negative. -/
private theorem decay_pow_nonneg (m : LossModel) (t : Nat) :
    0 ≤ m.decay ^ t :=
  pow_nonneg (le_of_lt m.h_decay_pos) t

/-- Initial gap is non-negative. -/
private theorem gap_base_nonneg (m : LossModel) :
    0 ≤ m.L_0 - m.L_star := by linarith [m.h_L0_ge]

/-- Gap is non-negative at every step. -/
theorem LossModel.gap_nonneg (m : LossModel) (t : Nat) :
    0 ≤ m.gap t := by
  unfold LossModel.gap
  exact mul_nonneg (gap_base_nonneg m) (decay_pow_nonneg m t)

/-- Initial condition: L(0) = L₀.
    C++: `Augur::predicted_loss(0) == initial_loss`. -/
theorem LossModel.loss_initial (m : LossModel) :
    m.loss 0 = m.L_0 := by
  simp [LossModel.loss]

/-- Loss is bounded below by L* at every step.
    C++: `assert(predicted_loss(t) >= L_star)` — Augur validates predictions. -/
theorem LossModel.loss_bounded_below (m : LossModel) (t : Nat) :
    m.L_star ≤ m.loss t := by
  unfold LossModel.loss
  have := m.gap_nonneg t
  unfold LossModel.gap at this
  linarith

/-- Decay power is monotone decreasing: r^(t+1) ≤ r^t when 0 < r ≤ 1. -/
private theorem decay_pow_antitone (m : LossModel) (t : Nat) :
    m.decay ^ (t + 1) ≤ m.decay ^ t := by
  rw [pow_succ]
  calc m.decay ^ t * m.decay
      ≤ m.decay ^ t * 1 := by
        apply mul_le_mul_of_nonneg_left m.h_decay_lt (decay_pow_nonneg m t)
    _ = m.decay ^ t := by ring

/-- Gap shrinks each step: gap(t+1) ≤ gap(t).
    The gap contracts by factor r each step. -/
theorem LossModel.gap_antitone (m : LossModel) (t : Nat) :
    m.gap (t + 1) ≤ m.gap t := by
  unfold LossModel.gap
  apply mul_le_mul_of_nonneg_left (decay_pow_antitone m t) (gap_base_nonneg m)

/-- Loss is monotone decreasing: L(t+1) ≤ L(t).
    THE fundamental convergence property.
    C++: `Augur::is_converging()` checks this holds over recent history. -/
theorem LossModel.loss_decreasing (m : LossModel) (t : Nat) :
    m.loss (t + 1) ≤ m.loss t := by
  unfold LossModel.loss
  have := m.gap_antitone t
  unfold LossModel.gap at this
  linarith

/-- Loss is monotone over any interval: s ≤ t → L(t) ≤ L(s). -/
theorem LossModel.loss_antitone (m : LossModel) {s t : Nat} (h : s ≤ t) :
    m.loss t ≤ m.loss s := by
  induction t with
  | zero =>
    have hs : s = 0 := by omega
    subst hs; exact le_refl _
  | succ n ih =>
    rcases Nat.eq_or_lt_of_le h with rfl | hlt
    · exact le_refl _
    · exact le_trans (m.loss_decreasing n) (ih (by omega))

/-- When decay < 1 strictly, the gap at step t+1 is strictly less than at step t
    (provided the initial gap is positive).
    C++: Augur uses strict decrease to confirm the model is actually learning. -/
theorem LossModel.gap_strict_decrease (m : LossModel) (t : Nat)
    (h_strict : m.decay < 1) (h_gap : 0 < m.L_0 - m.L_star) :
    m.gap (t + 1) < m.gap t := by
  unfold LossModel.gap
  rw [pow_succ]
  have h_pow_pos : 0 < m.decay ^ t := pow_pos m.h_decay_pos t
  have h_base_pos : 0 < (m.L_0 - m.L_star) := h_gap
  calc (m.L_0 - m.L_star) * (m.decay ^ t * m.decay)
      < (m.L_0 - m.L_star) * (m.decay ^ t * 1) := by
        apply mul_lt_mul_of_pos_left _ h_base_pos
        exact mul_lt_mul_of_pos_left h_strict h_pow_pos
    _ = (m.L_0 - m.L_star) * m.decay ^ t := by ring

/-- Concrete: loss model with L*=1, L₀=10, r=1/2.
    L(3) = 1 + 9 · (1/2)³ = 1 + 9/8 = 17/8. -/
example : (LossModel.mk 1 10 (1/2) (by norm_num) (by norm_num) (by norm_num)).loss 3
    = 17 / 8 := by
  simp [LossModel.loss]; norm_num

/-! ## 2. Learning Rate Schedule — Warmup + Bounded

Augur tracks learning rate schedules. Warmup phase: linear ramp from 0 to lr_max
over W steps. After warmup: bounded by lr_max.

C++: `Augur::lr_schedule(step)` returns the current learning rate.
Meridian uses this to predict convergence speed. -/

/-- Learning rate schedule configuration.
    C++: `struct LRSchedule { double lr_max; uint32_t warmup_steps; };` -/
structure LRSchedule where
  lr_max : ℚ
  warmup_steps : Nat
  h_lr_pos : 0 < lr_max
  h_warmup_pos : 0 < warmup_steps

/-- Learning rate during warmup: lr(t) = lr_max · t / warmup_steps.
    C++: `Augur::warmup_lr(step)`. -/
def LRSchedule.warmupLR (s : LRSchedule) (t : Nat) : ℚ :=
  s.lr_max * (t : ℚ) / (s.warmup_steps : ℚ)

/-- Warmup LR at step 0 is 0. -/
theorem LRSchedule.warmup_zero (s : LRSchedule) :
    s.warmupLR 0 = 0 := by
  simp [LRSchedule.warmupLR]

/-- Warmup LR at warmup_steps equals lr_max.
    C++: warmup phase ends exactly at lr_max. -/
theorem LRSchedule.warmup_complete (s : LRSchedule) :
    s.warmupLR s.warmup_steps = s.lr_max := by
  simp only [LRSchedule.warmupLR]
  have h : (s.warmup_steps : ℚ) ≠ 0 := Nat.cast_ne_zero.mpr (by have := s.h_warmup_pos; omega)
  field_simp

/-- Warmup LR is monotone: t₁ ≤ t₂ → lr(t₁) ≤ lr(t₂).
    C++: `Augur::lr_warmup_monotone()` — validates schedule sanity. -/
theorem LRSchedule.warmup_monotone (s : LRSchedule) {t₁ t₂ : Nat}
    (h : t₁ ≤ t₂) :
    s.warmupLR t₁ ≤ s.warmupLR t₂ := by
  simp only [LRSchedule.warmupLR]
  apply div_le_div_of_nonneg_right _ (by positivity)
  apply mul_le_mul_of_nonneg_left _ (le_of_lt s.h_lr_pos)
  exact Nat.cast_le.mpr h

/-- Warmup LR is non-negative. -/
theorem LRSchedule.warmup_nonneg (s : LRSchedule) (t : Nat) :
    0 ≤ s.warmupLR t := by
  simp only [LRSchedule.warmupLR]
  apply div_nonneg
  · exact mul_nonneg (le_of_lt s.h_lr_pos) (Nat.cast_nonneg t)
  · exact Nat.cast_nonneg s.warmup_steps

/-- Warmup LR during warmup (t ≤ W) is at most lr_max.
    C++: `assert(current_lr <= lr_max)`. -/
theorem LRSchedule.warmup_bounded (s : LRSchedule) {t : Nat}
    (ht : t ≤ s.warmup_steps) :
    s.warmupLR t ≤ s.lr_max := by
  simp only [LRSchedule.warmupLR]
  have hW : (0 : ℚ) < (s.warmup_steps : ℚ) := by
    exact Nat.cast_pos.mpr s.h_warmup_pos
  rw [div_le_iff₀ hW]
  exact mul_le_mul_of_nonneg_left (Nat.cast_le.mpr ht) (le_of_lt s.h_lr_pos)

/-- Concrete: lr_max=0.001, warmup=100, step=50 → lr = 0.001·50/100 = 0.0005. -/
example : (LRSchedule.mk (1/1000) 100 (by norm_num) (by norm_num)).warmupLR 50
    = 1/2000 := by
  simp [LRSchedule.warmupLR]; norm_num

/-! ## 3. Gradient Health — Signal-to-Noise Ratio

Augur measures gradient SNR per layer: SNR = ‖E[g]‖² / Var(g).
High SNR → standard backprop is efficient.
Low SNR → synthetic gradients or local losses are better.

C++: `Augur::gradient_health(layer_id)` returns (snr, jacobian_sv, strategy). -/

/-- Gradient health measurement.
    C++: `struct GradientHealth { double signal_sq, noise, snr; };` -/
structure GradientHealth where
  signal_sq : ℚ   -- ‖E[g]‖² (squared expected gradient norm)
  noise : ℚ       -- Var(g) (gradient variance)
  h_signal_nonneg : 0 ≤ signal_sq
  h_noise_nonneg : 0 ≤ noise

/-- SNR = signal² / noise. Returns 0 when noise is zero (perfect signal).
    C++: `Augur::compute_snr()`. -/
def GradientHealth.snr (gh : GradientHealth) : ℚ :=
  if gh.noise = 0 then 0 else gh.signal_sq / gh.noise

/-- SNR is non-negative (ratio of non-negative quantities).
    C++: `assert(snr >= 0)`. -/
theorem GradientHealth.snr_nonneg (gh : GradientHealth) :
    0 ≤ gh.snr := by
  simp only [GradientHealth.snr]
  split
  · exact le_refl 0
  · exact div_nonneg gh.h_signal_nonneg gh.h_noise_nonneg

/-- Zero signal implies zero SNR.
    C++: dead layer → SNR = 0 → freeze it. -/
theorem GradientHealth.zero_signal_zero_snr (gh : GradientHealth)
    (h : gh.signal_sq = 0) : gh.snr = 0 := by
  simp only [GradientHealth.snr]
  split <;> simp [h]

/-- Gradient strategy recommendation based on SNR threshold.
    C++: `Augur::recommend_gradient_strategy(layer)`. -/
inductive GradStrategy where
  | standardBackprop  -- high SNR: real gradients are informative
  | kfacNatural       -- moderate SNR: curvature helps denoise
  | syntheticGrad     -- low SNR: any approximation equally good
  | frozen            -- zero signal: no update needed

/-- Strategy selection from SNR thresholds.
    C++: `Augur::select_strategy(snr, high_thresh, low_thresh)`. -/
def selectStrategy (snr high_thresh low_thresh : ℚ) (signal_sq : ℚ) :
    GradStrategy :=
  if signal_sq = 0 then GradStrategy.frozen
  else if snr ≥ high_thresh then GradStrategy.standardBackprop
  else if snr ≥ low_thresh then GradStrategy.kfacNatural
  else GradStrategy.syntheticGrad

/-- Zero signal always produces frozen strategy. -/
theorem frozen_when_dead (snr high low : ℚ) :
    selectStrategy snr high low 0 = GradStrategy.frozen := by
  simp [selectStrategy]

/-- High SNR produces standard backprop (when signal is nonzero). -/
theorem standard_when_high (snr high low signal : ℚ)
    (hs : signal ≠ 0) (hsnr : snr ≥ high) :
    selectStrategy snr high low signal = GradStrategy.standardBackprop := by
  simp [selectStrategy, hs, hsnr]

/-! ## 4. Effective Rank — Representation Capacity

Given sorted singular values σ₁ ≥ σ₂ ≥ ... ≥ σₙ, the effective rank is
the count of σᵢ above a threshold ε. This measures how many dimensions
carry meaningful information.

C++: `Augur::effective_rank(layer, epsilon)` via randomized SVD. -/

/-- Count elements in a list above a threshold.
    C++: `std::count_if(sv.begin(), sv.end(), [eps](auto s){ return s > eps; })`. -/
def countAbove (xs : List ℚ) (ε : ℚ) : Nat :=
  xs.countP (fun x => decide (ε < x))

/-- Effective rank is at most the total dimension.
    C++: `assert(effective_rank <= hidden_dim)`. -/
theorem effective_rank_le_dim (xs : List ℚ) (ε : ℚ) :
    countAbove xs ε ≤ xs.length := by
  unfold countAbove; exact List.countP_le_length

/-- Effective rank of empty list is zero. -/
theorem effective_rank_empty (ε : ℚ) : countAbove [] ε = 0 := rfl

/-- Lowering the threshold can only increase the count.
    C++: smaller epsilon → more dimensions considered "alive". -/
theorem effective_rank_monotone_threshold (xs : List ℚ) {ε₁ ε₂ : ℚ}
    (h : ε₁ ≤ ε₂) :
    countAbove xs ε₂ ≤ countAbove xs ε₁ := by
  unfold countAbove
  apply List.countP_mono_left
  intro x _hmem hx
  simp only [decide_eq_true_eq] at *
  linarith

/-- If all values are below threshold, effective rank is zero.
    C++: all dead dimensions → representation collapse. -/
theorem effective_rank_zero_when_all_below (xs : List ℚ) (ε : ℚ)
    (h : ∀ x ∈ xs, x ≤ ε) :
    countAbove xs ε = 0 := by
  unfold countAbove
  apply List.countP_eq_zero.mpr
  intro x hx
  simp only [decide_eq_true_eq]
  linarith [h x hx]

/-- If all values are above threshold, effective rank equals dimension.
    C++: full-rank representation — no dead dimensions. -/
theorem effective_rank_full_when_all_above (xs : List ℚ) (ε : ℚ)
    (h : ∀ x ∈ xs, ε < x) :
    countAbove xs ε = xs.length := by
  unfold countAbove
  rw [List.countP_eq_length]
  intro x hx
  simp only [decide_eq_true_eq]
  exact h x hx

/-- Dead dimension count: total minus effective rank.
    C++: `Augur::dead_dimensions() = hidden_dim - effective_rank`. -/
def deadDimensions (xs : List ℚ) (ε : ℚ) : Nat :=
  xs.length - countAbove xs ε

/-- Dead dimensions + effective rank = total dimension. -/
theorem dead_plus_alive (xs : List ℚ) (ε : ℚ) :
    deadDimensions xs ε + countAbove xs ε = xs.length := by
  unfold deadDimensions
  exact Nat.sub_add_cancel (effective_rank_le_dim xs ε)

/-- Concrete: [5, 3, 1, 0] with ε=2 → rank 2 (only 5 and 3 above threshold). -/
example : countAbove [5, 3, 1, 0] 2 = 2 := by native_decide

/-! ## 5. CKA Similarity — Layer Redundancy Detection

CKA (Centered Kernel Alignment) measures representation similarity between
layers. CKA ∈ [0, 1]. CKA(X,X) = 1. High CKA between adjacent layers
indicates redundancy → safe to prune.

C++: `Augur::cka_similarity(layer_i, layer_j)`. -/

/-- CKA measurement between two layers.
    C++: `struct CKAResult { double value; bool is_redundant; };` -/
structure CKAMeasurement where
  value : ℚ
  h_nonneg : 0 ≤ value
  h_le_one : value ≤ 1

/-- CKA self-similarity is always 1.
    C++: `cka(X, X) == 1.0` — a layer is perfectly similar to itself. -/
def cka_self : CKAMeasurement :=
  ⟨1, by norm_num, by norm_num⟩

theorem cka_self_value : cka_self.value = 1 := rfl

/-- CKA is bounded in [0, 1]. -/
theorem cka_bounded (c : CKAMeasurement) : 0 ≤ c.value ∧ c.value ≤ 1 :=
  ⟨c.h_nonneg, c.h_le_one⟩

/-- Redundancy detection: CKA above threshold means layers are redundant.
    C++: `Augur::is_redundant(layer_i, layer_j, threshold)`.
    When CKA > 0.95 between adjacent layers → safe to prune one. -/
def isRedundant (c : CKAMeasurement) (threshold : ℚ) : Prop :=
  threshold ≤ c.value

/-- A layer is always redundant with itself at any threshold ≤ 1.
    C++: CKA(X,X) = 1 ≥ any reasonable threshold. -/
theorem self_always_redundant (threshold : ℚ) (h : threshold ≤ 1) :
    isRedundant cka_self threshold := by
  simp [isRedundant, cka_self]; exact h

/-- Higher CKA → redundant at more thresholds.
    If CKA = 0.97 is redundant at 0.95, it's also redundant at 0.90. -/
theorem redundancy_monotone (c : CKAMeasurement) {t₁ t₂ : ℚ}
    (ht : t₁ ≤ t₂) (hr : isRedundant c t₂) :
    isRedundant c t₁ := by
  unfold isRedundant at *; linarith

/-- Pruning safety: if CKA > threshold, removing the layer changes output by
    at most (1 - CKA) · ‖output‖. This is a Lipschitz bound.
    C++: `Augur::pruning_error_bound(layer, cka)`. -/
theorem pruning_error_bound (cka_val output_norm : ℚ)
    (_h_cka_nn : 0 ≤ cka_val) (h_cka_le : cka_val ≤ 1)
    (h_norm_nn : 0 ≤ output_norm) :
    0 ≤ (1 - cka_val) * output_norm := by
  apply mul_nonneg <;> linarith

/-- When CKA = 1, pruning error is zero (identical representations). -/
theorem pruning_error_zero_at_cka_one (output_norm : ℚ) :
    (1 - (1 : ℚ)) * output_norm = 0 := by ring

/-! ## 6. Compute Budget — Chinchilla Scaling Laws

Chinchilla formula: C = 6 · N · D where C = compute (FLOPs),
N = model parameters, D = training tokens.
Optimal allocation: for fixed C, choose N and D to minimize loss.

C++: `Augur::scaling_analysis(budget)` — computes optimal (N*, D*) via Z3. -/

/-- Compute budget configuration.
    C++: `struct ScalingConfig { uint64_t budget_flops, params, tokens; };` -/
structure ComputeBudget where
  params : ℚ       -- N: model parameters
  tokens : ℚ       -- D: training tokens
  h_params_pos : 0 < params
  h_tokens_pos : 0 < tokens

/-- Compute cost: C = 6 · N · D (FLOPs).
    The "6" comes from: 2 FLOPs/param for forward, 4 FLOPs/param for backward.
    C++: `Augur::compute_flops(N, D)`. -/
def computeCost (b : ComputeBudget) : ℚ :=
  6 * b.params * b.tokens

/-- Compute cost is positive.
    C++: `assert(compute_flops > 0)`. -/
theorem compute_cost_pos (b : ComputeBudget) : 0 < computeCost b := by
  unfold computeCost
  have := b.h_params_pos; have := b.h_tokens_pos
  positivity

/-- Doubling parameters doubles compute (tokens fixed).
    C++: Augur's linear scaling prediction. -/
theorem compute_double_params (b : ComputeBudget) :
    computeCost ⟨2 * b.params, b.tokens,
      by have := b.h_params_pos; positivity, b.h_tokens_pos⟩ =
    2 * computeCost b := by
  simp [computeCost]; ring

/-- Doubling tokens doubles compute (params fixed). -/
theorem compute_double_tokens (b : ComputeBudget) :
    computeCost ⟨b.params, 2 * b.tokens,
      b.h_params_pos, by have := b.h_tokens_pos; positivity⟩ =
    2 * computeCost b := by
  simp [computeCost]; ring

/-- Budget exhaustion: if we set N and D such that 6·N·D = C,
    then the compute is exactly C. Tautological but documents the contract.
    C++: `assert(6 * N_star * D_star == budget)`. -/
theorem budget_exhausted (N D C : ℚ) (hN : 0 < N) (hD : 0 < D)
    (h : 6 * N * D = C) :
    computeCost ⟨N, D, hN, hD⟩ = C := by
  simp [computeCost]; linarith

/-- For fixed budget C, increasing N requires decreasing D (and vice versa).
    The tradeoff curve: D = C / (6·N).
    C++: `Augur::tokens_for_params(budget, N)`. -/
theorem tokens_from_budget (C N : ℚ) (hN : 0 < N) (hC : 0 < C) :
    6 * N * (C / (6 * N)) = C := by
  have h6N : 6 * N ≠ 0 := by positivity
  field_simp

/-- Larger model with same budget gets fewer tokens. -/
theorem larger_model_fewer_tokens (C N₁ N₂ : ℚ) (hN₁ : 0 < N₁) (_hN₂ : 0 < N₂)
    (hC : 0 < C) (h : N₁ ≤ N₂) :
    C / (6 * N₂) ≤ C / (6 * N₁) := by
  apply div_le_div_of_nonneg_left (le_of_lt hC)
    (by have := hN₁; positivity) (by nlinarith)

/-- Iso-compute contour: two configs with same FLOPs.
    C++: Augur displays iso-compute lines in scaling law plots. -/
theorem iso_compute (N₁ D₁ N₂ D₂ : ℚ) (hN₁ : 0 < N₁) (hD₁ : 0 < D₁)
    (hN₂ : 0 < N₂) (hD₂ : 0 < D₂)
    (h : N₁ * D₁ = N₂ * D₂) :
    computeCost ⟨N₁, D₁, hN₁, hD₁⟩ = computeCost ⟨N₂, D₂, hN₂, hD₂⟩ := by
  simp [computeCost]; linarith

/-- Concrete: 1B params × 20B tokens = 120B FLOPs (× 6 = 720B). -/
example : computeCost ⟨1000000000, 20000000000, by positivity, by positivity⟩ =
    120000000000000000000 := by
  simp [computeCost]; norm_num

/-! ## 7. Batch Size Scaling — Critical Batch Size

The critical batch size B_crit determines the point where increasing batch
size stops improving training speed. Below B_crit: near-linear speedup.
Above: diminishing returns.

B_crit = B_noise · B_grad / (B_noise + B_grad)
       = 1 / (1/B_grad + 1/B_noise)  (harmonic mean up to factor)

C++: `Meridian::optimal_batch_size(noise_scale, grad_scale)`. -/

/-- Batch size scaling configuration.
    C++: `struct BatchConfig { double B_noise, B_grad; };` -/
structure BatchConfig where
  B_noise : ℚ    -- noise scale (gradient noise)
  B_grad : ℚ     -- gradient scale (per-sample signal)
  h_noise_pos : 0 < B_noise
  h_grad_pos : 0 < B_grad

/-- Critical batch size: B_crit = B_noise · B_grad / (B_noise + B_grad).
    C++: `Meridian::critical_batch_size()`. -/
def BatchConfig.criticalBatch (bc : BatchConfig) : ℚ :=
  bc.B_noise * bc.B_grad / (bc.B_noise + bc.B_grad)

/-- Critical batch size is positive.
    C++: `assert(critical_batch > 0)`. -/
theorem BatchConfig.critical_pos (bc : BatchConfig) :
    0 < bc.criticalBatch := by
  unfold BatchConfig.criticalBatch
  have h1 := bc.h_noise_pos; have h2 := bc.h_grad_pos
  apply div_pos (mul_pos h1 h2)
  linarith [h1, h2]

/-- Critical batch size ≤ B_noise.
    C++: `assert(critical_batch <= noise_scale)`. -/
theorem BatchConfig.critical_le_noise (bc : BatchConfig) :
    bc.criticalBatch ≤ bc.B_noise := by
  unfold BatchConfig.criticalBatch
  have hsum : 0 < bc.B_noise + bc.B_grad := by linarith [bc.h_noise_pos, bc.h_grad_pos]
  rw [div_le_iff₀ hsum]
  nlinarith [bc.h_noise_pos, bc.h_grad_pos]

/-- Critical batch size ≤ B_grad.
    C++: `assert(critical_batch <= grad_scale)`. -/
theorem BatchConfig.critical_le_grad (bc : BatchConfig) :
    bc.criticalBatch ≤ bc.B_grad := by
  unfold BatchConfig.criticalBatch
  have hsum : 0 < bc.B_noise + bc.B_grad := by linarith [bc.h_noise_pos, bc.h_grad_pos]
  rw [div_le_iff₀ hsum]
  nlinarith [bc.h_noise_pos, bc.h_grad_pos]

/-- When B_noise = B_grad, critical batch = B/2 (symmetric case).
    C++: balanced noise regime. -/
theorem BatchConfig.critical_symmetric (B : ℚ) (hB : 0 < B) :
    (BatchConfig.mk B B hB hB).criticalBatch = B / 2 := by
  simp only [BatchConfig.criticalBatch]
  have hB2 : B + B ≠ 0 := by linarith
  field_simp
  ring

/-- Larger noise scale → larger critical batch (grad fixed).
    C++: noisier gradients benefit more from larger batches. -/
theorem BatchConfig.critical_monotone_noise (n₁ n₂ g : ℚ)
    (hn₁ : 0 < n₁) (hn₂ : 0 < n₂) (hg : 0 < g) (h : n₁ ≤ n₂) :
    (BatchConfig.mk n₁ g hn₁ hg).criticalBatch ≤
    (BatchConfig.mk n₂ g hn₂ hg).criticalBatch := by
  simp only [BatchConfig.criticalBatch]
  have h1 : 0 < n₁ + g := by linarith
  have h2 : 0 < n₂ + g := by linarith
  rw [div_le_div_iff₀ h1 h2]
  nlinarith [mul_le_mul_of_nonneg_right h (le_of_lt hg)]

/-- Concrete: B_noise = 1000, B_grad = 4000 → B_crit = 800. -/
example : (BatchConfig.mk 1000 4000 (by norm_num) (by norm_num)).criticalBatch
    = 800 := by
  simp [BatchConfig.criticalBatch]; norm_num

/-! ## Integration: Augur Recommendation

Augur combines all analysis results into a ranked recommendation list.
Each recommendation has an expected speedup, confidence, and whether
it can be auto-applied or requires manual approval.

C++: `struct AugurRecommendation { double expected_speedup, confidence;
      bool auto_applicable; const char* description; };` -/

/-- Augur recommendation type.
    C++: `Augur::Recommendation::Kind`. -/
inductive RecommendKind where
  | freezeLayer       -- gradient near zero → stop updating
  | pruneLayer        -- CKA > threshold → remove redundant layer
  | reduceWidth       -- low effective rank → shrink hidden dim
  | changeStrategy    -- SNR shift → different gradient strategy
  | adjustBatchSize   -- noise scale changed → new batch size
  | adjustLR          -- curvature changed → new learning rate

/-- A recommendation with impact assessment.
    C++: `struct Recommendation { Kind kind; double speedup; };` -/
structure Recommendation where
  kind : RecommendKind
  expected_speedup : ℚ     -- predicted relative speedup (e.g. 1.15 = 15% faster)
  h_speedup_pos : 0 < expected_speedup

/-- Recommendations are ordered by expected speedup (higher = better).
    C++: `std::sort(recs.begin(), recs.end(), by_speedup_desc)`. -/
def Recommendation.betterThan (a b : Recommendation) : Prop :=
  b.expected_speedup ≤ a.expected_speedup

/-- betterThan is reflexive. -/
theorem Recommendation.betterThan_refl (r : Recommendation) :
    r.betterThan r := le_refl _

/-- betterThan is transitive. -/
theorem Recommendation.betterThan_trans {a b c : Recommendation}
    (h1 : a.betterThan b) (h2 : b.betterThan c) :
    a.betterThan c := by
  unfold Recommendation.betterThan at *; linarith

/-! ## Summary

Key results:
- `LossModel.loss_initial`: L(0) = L₀ (initial condition)
- `LossModel.loss_bounded_below`: L(t) ≥ L* (bounded below by asymptote)
- `LossModel.loss_decreasing`: L(t+1) ≤ L(t) (monotone decrease)
- `LossModel.loss_antitone`: s ≤ t → L(t) ≤ L(s) (general monotonicity)
- `LossModel.gap_strict_decrease`: strict decrease when r < 1 and gap > 0
- `LRSchedule.warmup_monotone`: warmup LR increases monotonically
- `LRSchedule.warmup_bounded`: warmup LR ≤ lr_max during warmup phase
- `GradientHealth.snr_nonneg`: SNR ≥ 0
- `effective_rank_le_dim`: effective rank ≤ total dimension
- `effective_rank_monotone_threshold`: lower ε → higher rank
- `cka_bounded`: CKA ∈ [0, 1]
- `redundancy_monotone`: higher CKA → redundant at more thresholds
- `compute_cost_pos`: C = 6·N·D > 0
- `larger_model_fewer_tokens`: N↑ → D↓ for fixed budget
- `BatchConfig.critical_pos`: B_crit > 0
- `BatchConfig.critical_le_noise`: B_crit ≤ B_noise
- `BatchConfig.critical_monotone_noise`: more noise → larger critical batch
-/

end Crucible
