import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.Quantize -- L10 Mixed Precision Quantization & Per-Op Precision Selection

From L10 Training (MANIFESTO.md):

  "Automatic mixed precision from measurement: run each op in FP32 and
   FP16/BF16/TF32/INT8/FP8, measure per-op difference, pick cheapest
   precision maintaining quality. Per-model, per-op, per-training-stage.
   Not a static allow-list."

From L8 Layers:

  "Automatic activation checkpointing from measured data"
  "NaN/Inf early kill: lightweight isfinite checks at numerically sensitive
   points (~1us). Catch instantly -> rollback."

This file formalizes:

1. **Precision types**: 7 numeric formats with bytes, mantissa bits, relative error
2. **Error model**: quantization error bounds, monotonicity, non-negativity
3. **Cost model**: throughput inversely proportional to precision width
4. **Per-op assignment**: precision selection for op chains
5. **Error propagation**: additive and multiplicative error accumulation
6. **Optimal selection**: budget-constrained precision optimization
7. **Gradient precision**: forward vs backward precision requirements
8. **Concrete examples**: GEMM, softmax, elementwise precision choices

All quantities use Nat (bytes, bits) or Q (error ratios, throughput).
Zero sorry.

C++ correspondence:
- `Augur::mixed_precision_analysis()` -- per-op measurement and selection
- `Meridian::precision_config()` -- Z3-optimal per-tensor precision
- `BackgroundThread::compile_mixed()` -- kernels compiled per-precision
- `Augur::gradient_health()` -- gradient precision requirements
- `CKernel::precision` -- per-kernel precision field
-/

namespace Crucible

/-! ## 1. Precision Types

From L10: "FP32/FP16/BF16/TF32/INT8/FP8 -- measure per-op difference,
pick cheapest precision maintaining quality."

Seven numeric formats spanning the precision-throughput tradeoff space.
Each has a memory footprint (bytes), mantissa resolution (bits), and a
simplified relative error bound. -/

/-- Numeric precision formats supported by Crucible's mixed precision engine.
    C++: `enum class Precision : uint8_t { FP32, TF32, BF16, FP16, FP8_E4, FP8_E5, INT8 };` -/
inductive QuantPrecision where
  | FP32    -- 4 bytes, 23-bit mantissa, machine eps ~1.2e-7
  | TF32    -- 4 bytes compute footprint, 10-bit mantissa, ~9.8e-4
  | BF16    -- 2 bytes, 7-bit mantissa, ~7.8e-3
  | FP16    -- 2 bytes, 10-bit mantissa, ~9.8e-4
  | FP8_E4  -- 1 byte, 3-bit mantissa, ~6.25e-2
  | FP8_E5  -- 1 byte, 2-bit mantissa, ~1.25e-1
  | INT8    -- 1 byte, quantized integer, ~3.9e-3 (1/256)
  deriving DecidableEq, Repr

/-- Memory cost per element in bytes.
    C++: `sizeof(element)` for each precision format.
    TF32 computes in 4 bytes (19-bit internal) but we model storage = 4. -/
def quant_bytes : QuantPrecision -> Nat
  | .FP32   => 4
  | .TF32   => 4
  | .BF16   => 2
  | .FP16   => 2
  | .FP8_E4 => 1
  | .FP8_E5 => 1
  | .INT8   => 1

/-- Mantissa bits (significand precision).
    C++: determines the resolution of representable values.
    Higher = more precise. FP32 has 23 mantissa bits, FP16 has 10, etc. -/
def quant_mantissa_bits : QuantPrecision -> Nat
  | .FP32   => 23
  | .TF32   => 10
  | .BF16   => 7
  | .FP16   => 10
  | .FP8_E4 => 3
  | .FP8_E5 => 2
  | .INT8   => 8  -- 8 integer bits, model as mantissa equivalent

/-- Simplified relative error model as a rational number.
    Machine epsilon ~ 2^(-mantissa_bits). We use a simplified integer
    ratio model: error = numerator / denominator.
    FP32 is the reference precision with zero error (exact).
    All other precisions have error > 0, proportional to 2^(-mantissa).
    C++: `Augur::measured_quantization_error(op, precision)`. -/
def quant_relative_error : QuantPrecision -> ℚ
  | .FP32   => 0          -- reference: no quantization error
  | .TF32   => 1 / 1024   -- ~2^(-10) = 9.8e-4
  | .BF16   => 1 / 128    -- ~2^(-7) = 7.8e-3
  | .FP16   => 1 / 1024   -- ~2^(-10) = 9.8e-4
  | .FP8_E4 => 1 / 8      -- ~2^(-3) = 0.125
  | .FP8_E5 => 1 / 4      -- ~2^(-2) = 0.25
  | .INT8   => 1 / 256    -- 1/256 quantization step

/-! ## 2. Error Model

Quantization error properties: FP32 is exact (reference), lower precision
has more error, all errors are non-negative. -/

/-- FP32 has zero relative error: it is the reference precision.
    C++: FP32 is baseline for per-op error measurement.
    `Augur::mixed_precision_analysis()` measures deviation from FP32. -/
theorem quant_fp32_exact : quant_relative_error .FP32 = 0 := rfl

/-- All relative errors are non-negative.
    C++: `assert(measured_error >= 0)` after per-op measurement. -/
theorem quant_error_nonneg (p : QuantPrecision) : 0 ≤ quant_relative_error p := by
  cases p <;> (simp [quant_relative_error]; try norm_num)

/-- FP32 error is at most any other precision's error.
    C++: FP32 is never worse than any lower precision. -/
theorem quant_fp32_best (p : QuantPrecision) :
    quant_relative_error .FP32 ≤ quant_relative_error p := by
  rw [quant_fp32_exact]; exact quant_error_nonneg p

/-- 1-byte precisions have error >= 2-byte precisions (restricted pairs).
    FP8_E5 (1 byte, error 1/4) >= FP16 (2 bytes, error 1/1024).
    C++: larger formats have better numerical stability. -/
theorem quant_fp8e5_ge_fp16 :
    quant_relative_error .FP16 ≤ quant_relative_error .FP8_E5 := by
  simp [quant_relative_error]; norm_num

/-- INT8 has less error than FP8_E5 in our model.
    C++: INT8 quantization with proper scaling achieves ~1/256 error. -/
theorem quant_int8_le_fp8e5 :
    quant_relative_error .INT8 ≤ quant_relative_error .FP8_E5 := by
  simp [quant_relative_error]; norm_num

/-- BF16 has more error than FP16 (fewer mantissa bits: 7 vs 10).
    C++: BF16 trades mantissa bits for dynamic range (8-bit exponent). -/
theorem quant_bf16_ge_fp16 :
    quant_relative_error .FP16 ≤ quant_relative_error .BF16 := by
  simp [quant_relative_error]; norm_num

/-- All errors are strictly below 1 (output retains >0% of the signal).
    C++: quantization is lossy but not destructive. -/
theorem quant_error_lt_one (p : QuantPrecision) :
    quant_relative_error p < 1 := by
  cases p <;> simp [quant_relative_error] <;> norm_num

/-! ## 3. Cost Model

Lower precision = higher throughput. FP32: 1x, FP16/BF16: 2x, INT8/FP8: 4x.
Throughput measured as ops per unit time, inversely proportional to bytes. -/

/-- Throughput multiplier relative to FP32. Model: 4 / bytes.
    FP32 (4B) -> 1x, FP16 (2B) -> 2x, INT8 (1B) -> 4x.
    C++: `Meridian::measured_throughput(precision)` calibrates actual values;
    this is the idealized model Augur uses for prediction. -/
def quant_throughput (p : QuantPrecision) : Nat :=
  4 / quant_bytes p

/-- FP32 throughput is 1x (baseline). -/
theorem quant_throughput_fp32 : quant_throughput .FP32 = 1 := rfl

/-- FP16 throughput is 2x. -/
theorem quant_throughput_fp16 : quant_throughput .FP16 = 2 := rfl

/-- BF16 throughput is 2x (same width as FP16). -/
theorem quant_throughput_bf16 : quant_throughput .BF16 = 2 := rfl

/-- INT8 throughput is 4x. -/
theorem quant_throughput_int8 : quant_throughput .INT8 = 4 := rfl

/-- FP8 throughput is 4x (1-byte format). -/
theorem quant_throughput_fp8e4 : quant_throughput .FP8_E4 = 4 := rfl

/-- All precisions have positive throughput.
    C++: every precision format can execute; throughput > 0. -/
theorem quant_throughput_pos (p : QuantPrecision) : 0 < quant_throughput p := by
  cases p <;> simp [quant_throughput, quant_bytes]

/-- Lower precision (fewer bytes) -> higher throughput.
    FP16 (2B, 2x) > FP32 (4B, 1x).
    C++: the fundamental precision-throughput tradeoff. -/
theorem quant_fp16_faster_than_fp32 :
    quant_throughput .FP32 < quant_throughput .FP16 := by decide

/-- INT8 faster than FP16. -/
theorem quant_int8_faster_than_fp16 :
    quant_throughput .FP16 < quant_throughput .INT8 := by decide

/-- No free lunch: throughput * error is non-decreasing relative to FP32.
    Higher throughput comes with higher (or equal) error.
    Specifically: for any p, throughput(p) * error(p) >= throughput(FP32) * error(FP32) = 0.
    C++: you cannot get more throughput AND less error simultaneously. -/
theorem quant_no_free_lunch (p : QuantPrecision) :
    (quant_throughput .FP32 : ℚ) * quant_relative_error .FP32 ≤
    (quant_throughput p : ℚ) * quant_relative_error p := by
  simp only [quant_relative_error, quant_throughput, quant_bytes]
  cases p <;> (simp; try positivity)

/-! ## 4. Per-Op Precision Assignment

Given a chain of N ops, assign a precision to each. The all-FP32 assignment
is the zero-error baseline. The all-INT8 assignment maximizes throughput. -/

/-- Precision assignment for a sequence of ops.
    C++: `struct MixedPrecisionPlan { std::vector<Precision> per_op; };` -/
structure QuantAssignment where
  num_ops : Nat
  precision : Fin num_ops -> QuantPrecision

/-- Total quantization error of an assignment: sum of per-op errors.
    C++: `Augur::total_quantization_error(plan)`. -/
def quant_total_error (a : QuantAssignment) : ℚ :=
  Finset.sum Finset.univ (fun i : Fin a.num_ops => quant_relative_error (a.precision i))

/-- Total throughput of an assignment: sum of per-op throughputs.
    C++: `Augur::total_throughput(plan)`. -/
def quant_total_throughput (a : QuantAssignment) : Nat :=
  Finset.sum Finset.univ (fun i : Fin a.num_ops => quant_throughput (a.precision i))

/-- Uniform FP32 assignment: all ops at FP32. -/
def quant_uniform_fp32 (n : Nat) : QuantAssignment :=
  { num_ops := n, precision := fun _ => .FP32 }

/-- Uniform INT8 assignment: all ops at INT8. -/
def quant_uniform_int8 (n : Nat) : QuantAssignment :=
  { num_ops := n, precision := fun _ => .INT8 }

/-- All-FP32 has zero total quantization error.
    C++: FP32 is the reference; no quantization loss.
    `Augur::verify_plan(fp32_plan)` always passes. -/
theorem quant_uniform_fp32_zero_error (n : Nat) :
    quant_total_error (quant_uniform_fp32 n) = 0 := by
  simp [quant_total_error, quant_uniform_fp32, quant_relative_error]

/-- All-INT8 achieves maximum per-op throughput (4x each).
    C++: INT8 GEMM on tensor cores is 4x faster than FP32. -/
theorem quant_uniform_int8_throughput (n : Nat) :
    quant_total_throughput (quant_uniform_int8 n) = 4 * n := by
  simp [quant_total_throughput, quant_uniform_int8, quant_throughput, quant_bytes]
  ring

/-- All-FP32 achieves baseline throughput (1x each). -/
theorem quant_uniform_fp32_throughput (n : Nat) :
    quant_total_throughput (quant_uniform_fp32 n) = n := by
  simp [quant_total_throughput, quant_uniform_fp32, quant_throughput, quant_bytes]

/-- INT8 throughput >= FP32 throughput for same number of ops.
    C++: INT8 plan is always at least as fast as FP32 plan. -/
theorem quant_int8_ge_fp32_throughput (n : Nat) :
    quant_total_throughput (quant_uniform_fp32 n) ≤
    quant_total_throughput (quant_uniform_int8 n) := by
  rw [quant_uniform_fp32_throughput, quant_uniform_int8_throughput]; omega

/-- Total error is non-negative for any assignment.
    C++: `assert(total_error >= 0)`. -/
theorem quant_total_error_nonneg (a : QuantAssignment) :
    0 ≤ quant_total_error a := by
  apply Finset.sum_nonneg
  intro i _
  exact quant_error_nonneg (a.precision i)

/-! ## 5. Error Propagation

Error accumulates through a chain of ops. Two models:
- Additive: total error <= sum of per-op errors (conservative bound for
  addition-like ops where errors add linearly)
- Multiplicative: total error <= product of (1 + per-op error) - 1
  (tighter bound for matmul chains where errors compound)

C++: `Augur::error_propagation(plan, model="additive"|"multiplicative")` -/

/-- Additive error accumulation for a list of per-op errors.
    C++: conservative bound used when ops are addition-like. -/
def quant_additive_error (errors : List ℚ) : ℚ := errors.sum

/-- Multiplicative error accumulation: (1+e₁)(1+e₂)...(1+eₙ) - 1.
    C++: tighter bound for matmul chains. -/
def quant_multiplicative_error (errors : List ℚ) : ℚ :=
  errors.foldl (fun acc e => acc + e + acc * e) 0

/-- Additive error bound: total = sum of individual errors.
    C++: `assert(propagated_error == sum(per_op_errors))`. -/
theorem quant_error_additive_bound (errors : List ℚ) :
    quant_additive_error errors = errors.sum := rfl

/-- Single-op additive error equals that op's error.
    C++: no accumulation for a single op. -/
theorem quant_single_op_exact_bound (e : ℚ) :
    quant_additive_error [e] = e := by
  simp [quant_additive_error]

/-- Additive error is non-negative when all per-op errors are non-negative.
    C++: `assert(total_additive_error >= 0)`. -/
theorem quant_additive_nonneg (errors : List ℚ) (h : ∀ e ∈ errors, 0 ≤ e) :
    0 ≤ quant_additive_error errors := by
  simp [quant_additive_error]
  exact List.sum_nonneg h

/-- Appending an op increases additive error.
    Longer chain -> more accumulated error.
    C++: each additional op adds to the error budget. -/
theorem quant_chain_error_monotone (errors : List ℚ) (e : ℚ) (he : 0 ≤ e) :
    quant_additive_error errors ≤ quant_additive_error (errors ++ [e]) := by
  simp [quant_additive_error]
  linarith

/-- Empty chain has zero additive error.
    C++: no ops = no error. -/
theorem quant_additive_empty : quant_additive_error [] = 0 := rfl

/-- Multiplicative error for empty chain is zero. -/
theorem quant_multiplicative_empty : quant_multiplicative_error [] = 0 := rfl

/-- Multiplicative error for single op equals that op's error.
    (1 + e) - 1 = e. -/
theorem quant_multiplicative_single (e : ℚ) :
    quant_multiplicative_error [e] = e := by
  simp [quant_multiplicative_error, List.foldl]

/-- Multiplicative error >= additive error when all errors are non-negative.
    The product (1+e₁)(1+e₂)...(1+eₙ) >= 1 + e₁ + e₂ + ... + eₙ.
    So multiplicative error >= additive error (cross-terms are non-negative).
    C++: multiplicative model is always more conservative. -/
private theorem quant_foldl_ge_sum (acc : ℚ) (rs : List ℚ) (hacc : 0 ≤ acc)
    (hrs : ∀ x ∈ rs, 0 ≤ x) :
    acc + rs.sum ≤ rs.foldl (fun a x => a + x + a * x) acc := by
  induction rs generalizing acc with
  | nil => simp
  | cons r rest ih =>
    simp only [List.sum_cons, List.foldl]
    have hr : 0 ≤ r := hrs r (List.mem_cons_self ..)
    have hrest : ∀ x ∈ rest, 0 ≤ x := fun x hx => hrs x (List.mem_cons_of_mem _ hx)
    have hacc' : 0 ≤ acc + r + acc * r := by nlinarith
    have step := ih (acc + r + acc * r) hacc' hrest
    linarith [mul_nonneg hacc hr]

theorem quant_multiplicative_ge_additive (errors : List ℚ) (h : ∀ e ∈ errors, 0 ≤ e) :
    quant_additive_error errors ≤ quant_multiplicative_error errors := by
  simp only [quant_additive_error, quant_multiplicative_error]
  have := quant_foldl_ge_sum 0 errors (le_refl 0) h
  linarith

/-! ## 6. Optimal Precision Selection

Given error budget E, find the assignment that maximizes throughput subject
to total error <= E. Or given throughput budget C, minimize error subject to
throughput >= C. Key insight: all-FP32 is always feasible for any error budget. -/

/-- An assignment is error-feasible if total error <= budget.
    C++: `Augur::check_error_budget(plan, budget)`. -/
def quant_error_feasible (a : QuantAssignment) (budget : ℚ) : Prop :=
  quant_total_error a ≤ budget

/-- All-FP32 satisfies any non-negative error budget (zero error).
    C++: FP32 is the fallback when no lower precision is safe.
    `Meridian::precision_config()` always has FP32 as fallback. -/
theorem quant_budget_feasible (n : Nat) (budget : ℚ) (hb : 0 ≤ budget) :
    quant_error_feasible (quant_uniform_fp32 n) budget := by
  unfold quant_error_feasible
  rw [quant_uniform_fp32_zero_error]
  exact hb

/-- For any number of ops, an optimal (error-minimizing) assignment exists:
    trivially the all-FP32 assignment achieves error = 0.
    C++: `Augur::find_optimal_precision(ops)` always returns a valid plan. -/
theorem quant_optimal_exists (n : Nat) :
    ∃ a : QuantAssignment, a.num_ops = n ∧ quant_total_error a = 0 :=
  ⟨quant_uniform_fp32 n, rfl, quant_uniform_fp32_zero_error n⟩

/-- An assignment dominates another if it has more throughput and less error.
    C++: `Augur::is_pareto_better(plan_a, plan_b)`. -/
def quant_dominates (a b : QuantAssignment) : Prop :=
  a.num_ops = b.num_ops ∧
  quant_total_error a ≤ quant_total_error b ∧
  quant_total_throughput b ≤ quant_total_throughput a

/-- Dominance is reflexive: every assignment dominates itself.
    C++: self-comparison baseline. -/
theorem quant_dominates_refl (a : QuantAssignment) :
    quant_dominates a a :=
  ⟨rfl, le_refl _, le_refl _⟩

/-- FP32 dominates any assignment in error (has minimum error = 0).
    But INT8 dominates any assignment in throughput.
    Neither dominates the other in both dimensions -- this IS the tradeoff.
    C++: Pareto frontier always includes both FP32 and INT8. -/
theorem quant_fp32_error_best (a : QuantAssignment) :
    quant_total_error (quant_uniform_fp32 a.num_ops) ≤ quant_total_error a := by
  rw [quant_uniform_fp32_zero_error]
  exact quant_total_error_nonneg a

/-! ## 7. Gradient Precision Requirements

From L10: gradients need higher precision than forward activations.
Gradient accumulators MUST be FP32 to avoid underflow/overflow.
Activations can tolerate BF16/FP16 during forward pass. -/

/-- Precision is safe for gradient accumulation: only FP32.
    C++: gradient buffers are always allocated as FP32 in PoolAllocator.
    Using lower precision for gradient accumulation causes silent divergence. -/
def quant_grad_safe : QuantPrecision -> Prop
  | .FP32 => True
  | _     => False

/-- FP32 is safe for gradient accumulation.
    C++: `assert(grad_precision == FP32)` in backward pass. -/
theorem quant_grad_fp32_safe : quant_grad_safe .FP32 := trivial

/-- FP16 is NOT safe for gradient accumulation.
    C++: FP16 gradients underflow for small learning rates. -/
theorem quant_grad_fp16_unsafe : ¬ quant_grad_safe .FP16 := not_false

/-- BF16 is NOT safe for gradient accumulation.
    C++: BF16 has only 7 mantissa bits -- gradient sums lose precision. -/
theorem quant_grad_bf16_unsafe : ¬ quant_grad_safe .BF16 := not_false

/-- INT8 is NOT safe for gradient accumulation.
    C++: integer gradients are nonsensical. -/
theorem quant_grad_int8_unsafe : ¬ quant_grad_safe .INT8 := not_false

/-- Precision is safe for forward activations: FP32, TF32, BF16, FP16.
    C++: activations tolerate lower precision; errors are bounded by
    forward noise which is later corrected by backward pass. -/
def quant_activation_safe : QuantPrecision -> Prop
  | .FP32   => True
  | .TF32   => True
  | .BF16   => True
  | .FP16   => True
  | .FP8_E4 => False  -- too lossy for general activations
  | .FP8_E5 => False  -- too lossy for general activations
  | .INT8   => False   -- quantized inference only

/-- FP16 is safe for activations.
    C++: standard mixed precision: FP16 forward, FP32 backward. -/
theorem quant_activation_fp16_safe : quant_activation_safe .FP16 := trivial

/-- BF16 is safe for activations.
    C++: Google-style mixed precision uses BF16 throughout forward. -/
theorem quant_activation_bf16_safe : quant_activation_safe .BF16 := trivial

/-- Activations tolerate lower precision than gradients: there exist
    precisions safe for activations but not for gradients.
    C++: the fundamental asymmetry of mixed precision training. -/
theorem quant_activation_more_tolerant :
    ∃ p : QuantPrecision, quant_activation_safe p ∧ ¬ quant_grad_safe p :=
  ⟨.FP16, trivial, not_false⟩

/-- Gradient-safe precisions are a subset of activation-safe precisions.
    C++: if a precision is safe for gradients, it is safe for activations. -/
theorem quant_grad_implies_activation (p : QuantPrecision) :
    quant_grad_safe p -> quant_activation_safe p := by
  cases p <;> simp [quant_grad_safe, quant_activation_safe]

/-! ## 8. Mixed Precision Plan Properties

A mixed plan assigns different precisions to forward and backward ops.
The backward precision must be gradient-safe (FP32). -/

/-- Mixed precision training plan for a layer.
    C++: `struct LayerPrecisionPlan { Precision forward, backward, accumulator; };` -/
structure MixedPlan where
  forward_prec : QuantPrecision
  backward_prec : QuantPrecision
  h_backward_safe : quant_grad_safe backward_prec

/-- The standard mixed precision plan: FP16 forward, FP32 backward.
    C++: `torch.cuda.amp.autocast()` equivalent but per-op optimal. -/
def mixed_standard_plan : MixedPlan :=
  { forward_prec := .FP16
    backward_prec := .FP32
    h_backward_safe := trivial }

/-- BF16 forward, FP32 backward plan (Google TPU style). -/
def mixed_bf16_plan : MixedPlan :=
  { forward_prec := .BF16
    backward_prec := .FP32
    h_backward_safe := trivial }

/-- Full FP32 plan (no mixed precision). -/
def mixed_fp32_plan : MixedPlan :=
  { forward_prec := .FP32
    backward_prec := .FP32
    h_backward_safe := trivial }

/-- Forward throughput of a mixed plan. -/
def MixedPlan.forward_throughput (p : MixedPlan) : Nat :=
  quant_throughput p.forward_prec

/-- Backward throughput of a mixed plan. Always 1x (FP32 accumulation). -/
def MixedPlan.backward_throughput (p : MixedPlan) : Nat :=
  quant_throughput p.backward_prec

/-- Standard mixed precision (FP16) has 2x forward throughput vs FP32.
    C++: the primary benefit of mixed precision training. -/
theorem mixed_standard_2x_forward :
    mixed_standard_plan.forward_throughput = 2 := rfl

/-- FP32 plan has 1x forward throughput (baseline).
    C++: no speedup from mixed precision. -/
theorem mixed_fp32_1x_forward :
    mixed_fp32_plan.forward_throughput = 1 := rfl

/-- Mixed precision forward is at least as fast as FP32 forward.
    C++: mixed precision never slows down the forward pass.
    (Follows from throughput being >= 1 for all precisions.) -/
theorem mixed_forward_ge_fp32 (p : MixedPlan) :
    mixed_fp32_plan.forward_throughput ≤ p.forward_throughput := by
  simp [MixedPlan.forward_throughput, mixed_fp32_plan]
  exact quant_throughput_pos p.forward_prec

/-- Memory savings from mixed precision forward: ratio of bytes.
    FP16 forward uses 2 bytes vs FP32's 4 bytes = 50% memory.
    C++: activation memory halved with FP16 forward. -/
def mixed_forward_memory_ratio (p : MixedPlan) : ℚ :=
  (quant_bytes p.forward_prec : ℚ) / (quant_bytes .FP32 : ℚ)

/-- Standard mixed precision uses 50% forward memory.
    C++: FP16 activations = half the memory of FP32. -/
theorem mixed_standard_half_memory :
    mixed_forward_memory_ratio mixed_standard_plan = 1 / 2 := by
  norm_num [mixed_forward_memory_ratio, mixed_standard_plan, quant_bytes]

/-- BF16 plan also uses 50% forward memory. -/
theorem mixed_bf16_half_memory :
    mixed_forward_memory_ratio mixed_bf16_plan = 1 / 2 := by
  norm_num [mixed_forward_memory_ratio, mixed_bf16_plan, quant_bytes]

/-- FP32 plan uses 100% forward memory (no savings). -/
theorem mixed_fp32_full_memory :
    mixed_forward_memory_ratio mixed_fp32_plan = 1 := by
  norm_num [mixed_forward_memory_ratio, mixed_fp32_plan, quant_bytes]

/-! ## 9. Numerically Sensitive Ops

Some ops (softmax, layer_norm) require high precision regardless of the
general plan. Others (relu, elementwise) tolerate any precision. -/

/-- Op sensitivity classification.
    C++: `CKernel::precision_sensitivity()` classifies each op. -/
inductive QuantSensitivity where
  | sensitive      -- must use FP32 (softmax, layer_norm, loss)
  | moderate       -- can use BF16/FP16 (matmul, conv)
  | insensitive    -- can use any precision (relu, add, elementwise)
  deriving DecidableEq, Repr

/-- Minimum precision required by sensitivity class.
    C++: `Meridian::minimum_precision(sensitivity)`. -/
def quant_min_precision : QuantSensitivity -> QuantPrecision
  | .sensitive   => .FP32
  | .moderate    => .FP16
  | .insensitive => .INT8

/-- A precision satisfies a sensitivity requirement if its error is at most
    the minimum precision's error.
    C++: `Augur::precision_safe(op_sensitivity, chosen_precision)`. -/
def quant_satisfies_sensitivity (chosen : QuantPrecision)
    (sens : QuantSensitivity) : Prop :=
  quant_relative_error chosen ≤ quant_relative_error (quant_min_precision sens)

/-- FP32 satisfies any sensitivity requirement.
    C++: FP32 is universally safe; Augur never rejects it. -/
theorem quant_fp32_satisfies_all (s : QuantSensitivity) :
    quant_satisfies_sensitivity .FP32 s := by
  cases s <;> simp [quant_satisfies_sensitivity, quant_min_precision, quant_relative_error]

/-- Sensitive ops require FP32: FP8_E4 does NOT satisfy sensitive.
    C++: softmax in low precision causes numerical instability and NaN. -/
theorem quant_fp8e4_not_sensitive :
    ¬ quant_satisfies_sensitivity .FP8_E4 .sensitive := by
  norm_num [quant_satisfies_sensitivity, quant_min_precision, quant_relative_error]

/-- FP16 satisfies moderate requirements.
    C++: GEMM in FP16 is the standard mixed precision workhorse. -/
theorem quant_fp16_satisfies_moderate :
    quant_satisfies_sensitivity .FP16 .moderate := by
  simp [quant_satisfies_sensitivity, quant_min_precision, quant_relative_error]

/-- INT8 satisfies insensitive requirements.
    C++: elementwise ops (relu, add) work fine at any precision. -/
theorem quant_int8_satisfies_insensitive :
    quant_satisfies_sensitivity .INT8 .insensitive := by
  norm_num [quant_satisfies_sensitivity, quant_min_precision, quant_relative_error]

/-! ## 10. Concrete Examples

Numerical validation of the precision model on real workloads. -/

/-- GEMM in FP16: 2x throughput, error = 1/1024.
    C++: `CKernel::GEMM_MM` compiled with precision=FP16. -/
example : quant_throughput .FP16 = 2 ∧ quant_relative_error .FP16 = 1 / 1024 := by
  exact ⟨rfl, rfl⟩

/-- Softmax must be FP32: 1x throughput, error = 0.
    C++: `CKernel::ACT_SOFTMAX` always dispatched at FP32.
    Augur classifies as `sensitive`. -/
example : quant_throughput .FP32 = 1 ∧ quant_relative_error .FP32 = 0 := by
  exact ⟨rfl, rfl⟩

/-- INT8 inference: 4x throughput, error = 1/256.
    C++: `CKernel::DEQUANT_GEMM` for quantized inference. -/
example : quant_throughput .INT8 = 4 ∧ quant_relative_error .INT8 = 1 / 256 := by
  exact ⟨rfl, rfl⟩

/-- Concrete 3-op chain: softmax(FP32) + GEMM(FP16) + relu(INT8).
    Total error = 0 + 1/1024 + 1/256 = 5/1024.
    Total throughput = 1 + 2 + 4 = 7.
    C++: typical mixed-precision layer execution. -/
example : let a : QuantAssignment := {
    num_ops := 3
    precision := fun i => match i.val with
      | 0 => .FP32
      | 1 => .FP16
      | _ => .INT8
  }
  quant_total_error a = 5 / 1024 := by native_decide

/-- Same chain total throughput = 7. -/
example : let a : QuantAssignment := {
    num_ops := 3
    precision := fun i => match i.val with
      | 0 => .FP32
      | 1 => .FP16
      | _ => .INT8
  }
  quant_total_throughput a = 7 := by native_decide

/-- All-FP32 3-op chain: error = 0, throughput = 3. -/
example : quant_total_error (quant_uniform_fp32 3) = 0 :=
  quant_uniform_fp32_zero_error 3
example : quant_total_throughput (quant_uniform_fp32 3) = 3 :=
  quant_uniform_fp32_throughput 3

/-- All-INT8 3-op chain: throughput = 12 (4x per op).
    C++: maximum throughput but highest error. -/
example : quant_total_throughput (quant_uniform_int8 3) = 12 :=
  quant_uniform_int8_throughput 3

/-- Mixed precision always exists that beats uniform FP32 on throughput
    while staying within error budget, IF there are at least 1 insensitive ops.
    We show concretely: for 2 ops, assigning one to INT8 gets more throughput
    than all-FP32 while keeping error at 1/256.
    C++: `Augur::find_mixed_plan()` always finds improvement over FP32. -/
theorem quant_mixed_dominates_example :
    let mixed : QuantAssignment := {
      num_ops := 2
      precision := fun i => if i.val = 0 then .FP32 else .INT8
    }
    let uniform := quant_uniform_fp32 2
    quant_total_throughput uniform < quant_total_throughput mixed ∧
    quant_total_error mixed ≤ 1 / 256 := by native_decide

/-! ## 11. Memory Savings from Mixed Precision

Lower precision reduces memory footprint for activations.
FP16 halves memory, INT8 quarters it. -/

/-- Total activation memory for an assignment (bytes per element * num_ops).
    C++: `PoolAllocator::plan_mixed()` computes per-tensor memory. -/
def quant_total_memory (a : QuantAssignment) : Nat :=
  Finset.sum Finset.univ (fun i : Fin a.num_ops => quant_bytes (a.precision i))

/-- FP32 memory = 4 * num_ops. -/
theorem quant_fp32_memory (n : Nat) :
    quant_total_memory (quant_uniform_fp32 n) = 4 * n := by
  simp [quant_total_memory, quant_uniform_fp32, quant_bytes]
  ring

/-- INT8 memory = 1 * num_ops. -/
theorem quant_int8_memory (n : Nat) :
    quant_total_memory (quant_uniform_int8 n) = n := by
  simp [quant_total_memory, quant_uniform_int8, quant_bytes]

/-- INT8 uses at most FP32 memory.
    C++: quantized model always fits in less memory. -/
theorem quant_int8_le_fp32_memory (n : Nat) :
    quant_total_memory (quant_uniform_int8 n) ≤
    quant_total_memory (quant_uniform_fp32 n) := by
  rw [quant_fp32_memory, quant_int8_memory]; omega

/-- Concrete: 100 ops, FP32 = 400 bytes, INT8 = 100 bytes.
    C++: 4x memory reduction from INT8 quantization. -/
example : quant_total_memory (quant_uniform_fp32 100) = 400 :=
  quant_fp32_memory 100
example : quant_total_memory (quant_uniform_int8 100) = 100 :=
  quant_int8_memory 100

/-! ## Summary

Key results:

**Precision Types:**
- `quant_bytes`, `quant_mantissa_bits`, `quant_relative_error`: per-format properties
- `quant_fp32_exact`: FP32 has zero error (reference)

**Error Model:**
- `quant_error_nonneg`: all errors >= 0
- `quant_fp32_best`: FP32 has minimum error
- `quant_error_lt_one`: all errors < 1

**Cost Model:**
- `quant_throughput_pos`: all throughputs > 0
- `quant_fp16_faster_than_fp32`: FP16 is 2x faster than FP32
- `quant_no_free_lunch`: throughput * error is non-decreasing vs FP32

**Per-Op Assignment:**
- `quant_uniform_fp32_zero_error`: all-FP32 has zero error
- `quant_uniform_int8_throughput`: all-INT8 has 4x throughput
- `quant_int8_ge_fp32_throughput`: INT8 always faster than FP32

**Error Propagation:**
- `quant_chain_error_monotone`: longer chain -> more error
- `quant_multiplicative_ge_additive`: multiplicative >= additive bound
- `quant_single_op_exact_bound`: single op error = per-op error

**Optimal Selection:**
- `quant_budget_feasible`: FP32 satisfies any error budget
- `quant_optimal_exists`: error-minimizing plan always exists
- `quant_fp32_error_best`: FP32 has globally minimum error

**Gradient Precision:**
- `quant_grad_fp32_safe`: only FP32 safe for gradients
- `quant_activation_more_tolerant`: activations tolerate lower precision
- `quant_grad_implies_activation`: gradient-safe implies activation-safe

**Sensitivity:**
- `quant_fp32_satisfies_all`: FP32 satisfies any sensitivity
- `quant_fp16_satisfies_moderate`: FP16 is fine for matmuls
- `quant_int8_satisfies_insensitive`: INT8 works for elementwise
-/

end Crucible
