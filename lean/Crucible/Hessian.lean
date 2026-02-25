import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.Hessian -- L10 Second-Order Optimization

From L10 Training (MANIFESTO.md):

  "Hessian-vector products (Pearlmutter 1994): O(N) cost for Hv
   -> per-parameter curvature (principled LR), top eigenvalues
   (saddle detection, sharpness), K-FAC factors (F ~ A (x) G,
   natural gradient F^{-1}g). Periodic, not per-step."

  "Per-layer LR from curvature: Hessian diagonal gives optimal
   lr proportional to 1/H_ii."

  "K-FAC natural gradient: F ~ A (x) G per layer, tractable inverse.
   Steepest descent in distribution space. 2-3x fewer steps, ~2x
   cost/step. Activated where SNR is moderate."

This file formalizes:

1. **Hessian-vector product cost**: O(N) vs O(N^2) full Hessian
2. **K-FAC approximation**: Kronecker factorization cost savings
3. **Natural gradient**: F^{-1}g step cost and convergence advantage
4. **Condition number & optimal LR**: kappa = lambda_max/lambda_min, lr* = 2/(L+mu)
5. **Lanczos eigenvalue estimation**: k Hv products for top-k spectrum
6. **Per-layer optimization strategy**: SGD/AdamW/KFAC/LBFGS cost ordering
7. **Saddle point detection**: negative Hessian eigenvalue detection
8. **Sharpness & generalization**: flat minima generalize better

All quantities use Nat (costs/dimensions) or Q (eigenvalues/learning rates).
Zero sorry.

C++ correspondence:
- `Augur::hessian_spectrum()` -- Lanczos iteration for top-k eigenvalues
- `Augur::gradient_health()` -- per-layer SNR, Jacobian analysis
- `Augur::curvature_lr()` -- optimal lr from Hessian diagonal
- `Meridian::kfac_config()` -- K-FAC factor sizes, update frequency
- `BackgroundThread::compile_kfac()` -- K-FAC inverse computation
-/

namespace Crucible

/-! ## 1. Hessian-Vector Product Cost

From L10: "Hessian-vector products (Pearlmutter 1994): O(N) cost for Hv."

Computing the full N x N Hessian is O(N^2) -- intractable for millions of
parameters. But computing Hv (Hessian times a vector) costs only O(N) via
reverse-mode autodiff: one forward + one backward = 2N ops.

C++: `Augur::hessian_vector_product(params, direction)`. -/

/-- Cost of computing the full N x N Hessian matrix.
    O(N^2) -- one second-order derivative per parameter pair.
    C++: never computed directly; always approximated. -/
def hess_full_cost (params : Nat) : Nat := params * params

/-- Cost of one Hessian-vector product via Pearlmutter's trick.
    Two passes (forward + backward) through the network = 2N ops.
    C++: `Augur::hessian_vector_product()` uses reverse-over-reverse. -/
def hess_hvp_cost (params : Nat) : Nat := 2 * params

/-- HVP is cheaper than full Hessian when params > 2.
    2N < N^2 iff N > 2. This is THE reason second-order methods are viable.
    C++: Augur uses HVP exclusively; full Hessian is never materialized. -/
theorem hess_hvp_cheaper_than_full (params : Nat) (h : 2 < params) :
    hess_hvp_cost params < hess_full_cost params := by
  simp [hess_hvp_cost, hess_full_cost]
  nlinarith

/-- HVP cost is linear in params: hess_hvp_cost(a + b) = hess_hvp_cost(a) + hess_hvp_cost(b).
    C++: HVP scales linearly with model size. -/
theorem hess_hvp_linear (a b : Nat) :
    hess_hvp_cost (a + b) = hess_hvp_cost a + hess_hvp_cost b := by
  simp [hess_hvp_cost]; ring

/-- Full Hessian cost is quadratic: doubling params -> 4x cost.
    C++: full Hessian is completely intractable for large models. -/
theorem hess_full_quadratic (n : Nat) :
    hess_full_cost (2 * n) = 4 * hess_full_cost n := by
  simp [hess_full_cost]; ring

/-- Savings ratio: hvp_cost / full_cost = 2/N for N > 0.
    For N = 1M params: ratio = 0.000002. Massive savings.
    C++: Augur logs this ratio to justify HVP approach. -/
theorem hess_hvp_savings_ratio (params : Nat) (h : 0 < params) :
    (hess_hvp_cost params : ℚ) / (hess_full_cost params : ℚ) = 2 / (params : ℚ) := by
  simp only [hess_hvp_cost, hess_full_cost]
  have hp : (params : ℚ) ≠ 0 := Nat.cast_ne_zero.mpr (by omega)
  push_cast
  field_simp

/-- HVP cost is always positive when params > 0.
    C++: `assert(hvp_cost > 0)`. -/
theorem hess_hvp_pos (params : Nat) (h : 0 < params) :
    0 < hess_hvp_cost params := by
  simp [hess_hvp_cost]; omega

/-! ## 2. K-FAC Approximation

K-FAC approximates the Fisher information matrix F (d_in*d_out x d_in*d_out)
as a Kronecker product A (x) G where:
  A = E[a_i a_i^T]  (input covariance,  d_in  x d_in)
  G = E[g_i g_i^T]  (gradient covariance, d_out x d_out)

Storage and inversion of two small matrices instead of one huge one.
C++: `Meridian::kfac_config()` computes factor sizes per layer. -/

/-- Cost of storing/inverting the two K-FAC factors.
    A is d_in x d_in, G is d_out x d_out.
    C++: `sizeof(A) + sizeof(G) = d_in^2 + d_out^2`. -/
def kfac_factor_cost (d_in d_out : Nat) : Nat := d_in * d_in + d_out * d_out

/-- Cost of storing/inverting the full Fisher matrix.
    F is (d_in * d_out) x (d_in * d_out).
    C++: never computed; intractable for any real layer. -/
def kfac_full_fisher_cost (d_in d_out : Nat) : Nat :=
  (d_in * d_out) * (d_in * d_out)

/-- K-FAC is cheaper than full Fisher when both dimensions > 1.
    d_in^2 + d_out^2 < (d_in * d_out)^2 when d_in > 1 and d_out > 1.
    THE fundamental K-FAC advantage.
    C++: Augur always uses K-FAC approximation, never full Fisher. -/
theorem kfac_cheaper_than_full (d_in d_out : Nat)
    (hi : 1 < d_in) (ho : 1 < d_out) :
    kfac_factor_cost d_in d_out < kfac_full_fisher_cost d_in d_out := by
  simp only [kfac_factor_cost, kfac_full_fisher_cost]
  -- d_in^2 + d_out^2 < (d_in * d_out)^2
  -- Since d_in >= 2 and d_out >= 2: d_in*d_out >= 4,
  -- (d_in*d_out)^2 >= 4*d_in*d_out >= 4*4 = 16,
  -- while d_in^2 + d_out^2 < d_in*d_out*(d_in+d_out)/2 ... < (d_in*d_out)^2
  have hdi : 2 ≤ d_in := hi
  have hdo : 2 ≤ d_out := ho
  -- d_in * d_out >= 4
  have hprod : 4 ≤ d_in * d_out := Nat.mul_le_mul hdi hdo
  -- We need: d_in^2 + d_out^2 < (d_in*d_out)^2
  -- Equivalently: d_in^2 + d_out^2 < d_in^2*d_out^2
  -- Since d_out >= 2: d_in^2*d_out^2 >= 4*d_in^2 > d_in^2
  -- Since d_in >= 2: d_in^2*d_out^2 >= 4*d_out^2 > d_out^2
  -- So: d_in^2*d_out^2 > d_in^2 and d_in^2*d_out^2 > d_out^2
  -- Need: d_in^2*d_out^2 > d_in^2 + d_out^2
  -- Since d_in^2*d_out^2 >= 4*d_in^2 and >= 4*d_out^2:
  -- d_in^2*d_out^2 = (1/2)*d_in^2*d_out^2 + (1/2)*d_in^2*d_out^2
  --               >= 2*d_in^2 + 2*d_out^2 > d_in^2 + d_out^2
  nlinarith [Nat.mul_le_mul hdi hdi, Nat.mul_le_mul hdo hdo,
             Nat.mul_le_mul (Nat.mul_le_mul hdi hdi) (Nat.mul_le_mul hdo hdo)]

/-- K-FAC factor cost is symmetric: swapping d_in and d_out doesn't change cost.
    C++: factor computation is symmetric w.r.t. input/output roles. -/
theorem kfac_factor_symmetric (d_in d_out : Nat) :
    kfac_factor_cost d_in d_out = kfac_factor_cost d_out d_in := by
  simp [kfac_factor_cost]; ring

/-- Full Fisher cost is symmetric: swapping d_in and d_out doesn't change cost. -/
theorem kfac_full_fisher_symmetric (d_in d_out : Nat) :
    kfac_full_fisher_cost d_in d_out = kfac_full_fisher_cost d_out d_in := by
  simp [kfac_full_fisher_cost]; ring

/-- K-FAC inverse cost: inverting A (d_in^3) + inverting G (d_out^3) via Cholesky.
    Full Fisher inverse would be (d_in * d_out)^3.
    C++: `BackgroundThread::invert_kfac_factors()`. -/
def kfac_inverse_cost (d_in d_out : Nat) : Nat :=
  d_in * d_in * d_in + d_out * d_out * d_out

/-- Full Fisher inverse cost: O((d_in * d_out)^3).
    C++: completely intractable; K-FAC avoids this entirely. -/
def kfac_full_inverse_cost (d_in d_out : Nat) : Nat :=
  (d_in * d_out) * (d_in * d_out) * (d_in * d_out)

/-- K-FAC inverse is cheaper than full Fisher inverse when d_in > 1 and d_out > 1.
    d_in^3 + d_out^3 < (d_in * d_out)^3.
    C++: K-FAC makes natural gradient tractable. -/
theorem kfac_inverse_cheaper (d_in d_out : Nat)
    (hi : 1 < d_in) (ho : 1 < d_out) :
    kfac_inverse_cost d_in d_out < kfac_full_inverse_cost d_in d_out := by
  simp only [kfac_inverse_cost, kfac_full_inverse_cost]
  have hdi : 2 ≤ d_in := hi
  have hdo : 2 ≤ d_out := ho
  -- d_in^3 + d_out^3 < (d_in*d_out)^3
  -- (d_in*d_out)^3 >= (2*d_out)^3 = 8*d_out^3 >= 2*d_out^3
  -- (d_in*d_out)^3 >= (d_in*2)^3 = 8*d_in^3 >= 2*d_in^3
  -- So (d_in*d_out)^3 >= max(2*d_in^3, 2*d_out^3) and since both >= 2:
  -- Use: d_in*d_out >= 4, so (d_in*d_out)^3 >= 64
  -- Also d_in^3 + d_out^3 <= (d_in+d_out)/2 * ..., but simpler with nlinarith hints
  nlinarith [Nat.mul_le_mul hdi hdi, Nat.mul_le_mul hdo hdo,
             Nat.mul_le_mul (Nat.mul_le_mul hdi hdi) hdi,
             Nat.mul_le_mul (Nat.mul_le_mul hdo hdo) hdo,
             Nat.mul_le_mul (Nat.mul_le_mul hdi hdo) (Nat.mul_le_mul hdi hdo),
             Nat.mul_le_mul (Nat.mul_le_mul (Nat.mul_le_mul hdi hdo) hdi) hdo]

/-- Kronecker product size: A (x) G has dimensions (d_in * d_out) x (d_in * d_out),
    same as the full Fisher. The approximation preserves the matrix dimensions
    but factors the structure.
    C++: `kfac_product_size == full_fisher_size`. -/
theorem kfac_kronecker_product_size (d_in d_out : Nat) :
    d_in * d_out * (d_in * d_out) = kfac_full_fisher_cost d_in d_out := by
  simp [kfac_full_fisher_cost]

/-- Concrete: 4096 x 4096 layer.
    K-FAC factors: 2 * 4096^2 = 33,554,432 elements.
    Full Fisher: (4096*4096)^2 = 281,474,976,710,656 elements.
    Ratio: ~8.4 million x cheaper. -/
example : kfac_factor_cost 4096 4096 = 33554432 := by native_decide
example : kfac_full_fisher_cost 4096 4096 = 281474976710656 := by native_decide

/-! ## 3. Natural Gradient

The natural gradient step is F^{-1}g -- steepest descent in distribution space
(not parameter space). K-FAC makes this tractable:
  F^{-1}g ~ (A^{-1} (x) G^{-1}) vec(grad)
  = G^{-1} * grad * A^{-1}   (via Kronecker property)

This costs d_out^2*d_in + d_in^2*d_out (two matrix multiplies), vs
(d_in*d_out)^2 for a direct system solve.

C++: `BackgroundThread::apply_natural_gradient(layer)`. -/

/-- Cost of applying K-FAC natural gradient to one layer.
    Step 1: compute K-FAC factors (d_in^2 + d_out^2)
    Step 2: apply to gradient (d_in * d_out matrix operations)
    C++: `BackgroundThread::kfac_step(layer)`. -/
def natgrad_step_cost (d_in d_out : Nat) : Nat :=
  kfac_factor_cost d_in d_out + d_in * d_out

/-- Cost of one SGD step: just scale and add the gradient.
    O(d_in * d_out) -- one pass over all parameters.
    C++: `Optimizer::sgd_step()`. -/
def natgrad_sgd_cost (d_in d_out : Nat) : Nat := d_in * d_out

/-- Natural gradient costs more than SGD per step.
    K-FAC adds factor computation overhead on top of the gradient application.
    C++: K-FAC is 2-3x more expensive per step but converges 2-3x faster. -/
theorem natgrad_more_expensive_than_sgd (d_in d_out : Nat) :
    natgrad_sgd_cost d_in d_out ≤ natgrad_step_cost d_in d_out := by
  simp only [natgrad_step_cost, natgrad_sgd_cost, kfac_factor_cost]
  omega

/-- Natural gradient overhead ratio for square layers: (2d^2 + d^2) / d^2 = 3.
    K-FAC costs ~3x SGD for square layers.
    C++: Augur uses this ratio to decide when K-FAC is worthwhile. -/
theorem natgrad_square_overhead (d : Nat) (hd : 0 < d) :
    (natgrad_step_cost d d : ℚ) / (natgrad_sgd_cost d d : ℚ) = 3 := by
  simp only [natgrad_step_cost, natgrad_sgd_cost, kfac_factor_cost]
  have hd2 : (d : ℚ) * d ≠ 0 := by positivity
  push_cast
  field_simp
  ring

/-- K-FAC convergence model: total cost = per_step_cost * num_steps.
    K-FAC needs fewer_steps to reach the same loss.
    C++: `Augur::kfac_total_cost(layer, target_loss)`. -/
def natgrad_total_cost (per_step num_steps : Nat) : Nat := per_step * num_steps

/-- K-FAC wins when 3x cost/step * (1/3) steps < 1x cost/step * 1x steps.
    I.e., if step_reduction > cost_increase, K-FAC wins overall.
    Concrete: 3x cost, 1/3 steps = same total. But K-FAC typically
    achieves > 3x step reduction, so it wins.
    C++: Augur compares `kfac_total_cost < sgd_total_cost`. -/
theorem natgrad_kfac_wins_when_fewer_steps
    (sgd_cost kfac_cost sgd_steps kfac_steps : Nat)
    (h : kfac_cost * kfac_steps < sgd_cost * sgd_steps) :
    natgrad_total_cost kfac_cost kfac_steps < natgrad_total_cost sgd_cost sgd_steps := by
  exact h

/-! ## 4. Condition Number and Optimal Learning Rate

From L10: "Hessian diagonal gives optimal lr proportional to 1/H_ii."
Condition number kappa = lambda_max / lambda_min.
Optimal learning rate: lr* = 2 / (lambda_max + lambda_min).

C++: `Augur::curvature_lr(layer)` computes per-layer optimal LR. -/

/-- Condition number: ratio of largest to smallest eigenvalue.
    kappa >= 1 when lambda_max >= lambda_min > 0.
    C++: `Augur::condition_number(layer)`. -/
def curvature_condition_number (lambda_max lambda_min : ℚ) : ℚ :=
  lambda_max / lambda_min

/-- Optimal learning rate from Hessian spectrum.
    lr* = 2 / (lambda_max + lambda_min).
    Minimizes worst-case convergence rate for quadratic objectives.
    C++: `Augur::optimal_lr(lambda_max, lambda_min)`. -/
def curvature_optimal_lr (lambda_max lambda_min : ℚ) : ℚ :=
  2 / (lambda_max + lambda_min)

/-- Condition number >= 1 when lambda_max >= lambda_min > 0.
    Well-posed problems always have kappa >= 1.
    C++: `assert(kappa >= 1)` after Lanczos iteration. -/
theorem curvature_kappa_ge_one (lambda_max lambda_min : ℚ)
    (hmin : 0 < lambda_min) (hle : lambda_min ≤ lambda_max) :
    1 ≤ curvature_condition_number lambda_max lambda_min := by
  simp only [curvature_condition_number]
  rw [le_div_iff₀ hmin]
  linarith

/-- Optimal LR is bounded above by 2/lambda_max.
    Cannot exceed the step size dictated by the largest eigenvalue.
    C++: `assert(optimal_lr <= 2.0 / lambda_max)`. -/
theorem curvature_lr_bounded (lambda_max lambda_min : ℚ)
    (hmax : 0 < lambda_max) (hmin : 0 < lambda_min) :
    curvature_optimal_lr lambda_max lambda_min ≤ 2 / lambda_max := by
  simp only [curvature_optimal_lr]
  apply div_le_div_of_nonneg_left (by norm_num : (0:ℚ) ≤ 2)
    (by linarith) (by linarith)

/-- Optimal LR is positive when both eigenvalues are positive.
    C++: `assert(optimal_lr > 0)`. -/
theorem curvature_lr_pos (lambda_max lambda_min : ℚ)
    (hmax : 0 < lambda_max) (hmin : 0 < lambda_min) :
    0 < curvature_optimal_lr lambda_max lambda_min := by
  simp only [curvature_optimal_lr]
  positivity

/-- When kappa = 1 (perfectly conditioned), lr* = 1/lambda.
    The best case: gradient descent converges in one step for quadratics.
    C++: `Augur::is_well_conditioned(layer)` checks kappa close to 1. -/
theorem curvature_perfect_conditioning (lambda : ℚ) (h : 0 < lambda) :
    curvature_optimal_lr lambda lambda = 1 / lambda := by
  simp only [curvature_optimal_lr]
  have : lambda + lambda = 2 * lambda := by ring
  rw [this]
  field_simp

/-- Convergence rate for gradient descent: (kappa - 1) / (kappa + 1).
    Smaller kappa -> faster convergence. kappa = 1 -> rate = 0 (instant).
    C++: `Augur::convergence_rate(kappa)`. -/
def curvature_convergence_rate (kappa : ℚ) : ℚ :=
  (kappa - 1) / (kappa + 1)

/-- Perfect conditioning (kappa = 1) gives zero convergence rate.
    Rate = 0 means convergence in one step.
    C++: `Augur::is_instant_convergence(layer)`. -/
theorem curvature_rate_perfect : curvature_convergence_rate 1 = 0 := by
  simp [curvature_convergence_rate]

/-- Convergence rate is non-negative when kappa >= 1.
    C++: `assert(convergence_rate >= 0)`. -/
theorem curvature_rate_nonneg (kappa : ℚ) (h : 1 ≤ kappa) :
    0 ≤ curvature_convergence_rate kappa := by
  simp only [curvature_convergence_rate]
  apply div_nonneg <;> linarith

/-- Convergence rate is strictly less than 1 when kappa >= 1.
    Rate < 1 guarantees eventual convergence.
    C++: `assert(convergence_rate < 1)` -- divergence impossible. -/
theorem curvature_rate_lt_one (kappa : ℚ) (h : 1 ≤ kappa) :
    curvature_convergence_rate kappa < 1 := by
  simp only [curvature_convergence_rate]
  rw [div_lt_one (by linarith)]
  linarith

/-- Larger kappa -> slower convergence: rate is monotone increasing in kappa.
    C++: `Augur::predict_steps_to_converge()` uses this. -/
theorem curvature_rate_monotone (k1 k2 : ℚ)
    (h1 : 1 ≤ k1) (h2 : k1 ≤ k2) :
    curvature_convergence_rate k1 ≤ curvature_convergence_rate k2 := by
  simp only [curvature_convergence_rate]
  have hd1 : (0:ℚ) < k1 + 1 := by linarith
  have hd2 : (0:ℚ) < k2 + 1 := by linarith
  rw [div_le_div_iff₀ hd1 hd2]
  nlinarith

/-- Concrete: kappa=10, lr* = 2/11, rate = 9/11 ~ 0.82.
    C++: typical ill-conditioned layer metrics. -/
example : curvature_optimal_lr 10 1 = 2 / 11 := by
  simp [curvature_optimal_lr]; norm_num
example : curvature_convergence_rate 10 = 9 / 11 := by
  simp [curvature_convergence_rate]; norm_num

/-! ## 5. Lanczos Eigenvalue Estimation

Lanczos iteration: k Hessian-vector products -> approximate top-k eigenvalues.
From L17 Augur: "Lanczos iteration (10 Hv products), condition number
kappa = L/mu, optimal lr = 2/(L+mu)."

C++: `Augur::lanczos_iteration(params, k)`. -/

/-- Cost of Lanczos iteration: k HVP operations.
    Each HVP costs 2*params. Total = k * 2 * params.
    C++: `Augur::lanczos_cost(params, num_eigenvalues)`. -/
def lanczos_cost (params : Nat) (k : Nat) : Nat := k * hess_hvp_cost params

/-- Lanczos cost scales linearly with k (number of eigenvalues requested).
    Doubling k doubles the cost.
    C++: Augur sets k=10-20 based on a cost/benefit analysis. -/
theorem lanczos_cost_linear_k (params k : Nat) :
    lanczos_cost params (2 * k) = 2 * lanczos_cost params k := by
  simp [lanczos_cost]; ring

/-- Lanczos cost scales linearly with params.
    C++: cost is proportional to model size. -/
theorem lanczos_cost_linear_params (params k : Nat) :
    lanczos_cost (2 * params) k = 2 * lanczos_cost params k := by
  simp [lanczos_cost, hess_hvp_cost]; ring

/-- k = 0 eigenvalues costs nothing. -/
theorem lanczos_zero_cost (params : Nat) : lanczos_cost params 0 = 0 := by
  simp [lanczos_cost]

/-- Lanczos with k eigenvalues is cheaper than full eigendecomposition (O(N^3)).
    Full eigendecomposition = N^3 (symmetric). Lanczos = k * 2N.
    Cheaper when k < N^2 / 2, which is ALWAYS true for k = 10-20.
    C++: Augur uses k=10, never full eigendecomposition. -/
def hess_full_eigen_cost (params : Nat) : Nat := params * params * params

theorem lanczos_cheaper_than_full_spectrum (params k : Nat) (hp : 0 < params)
    (hk : 2 * k < params * params) :
    lanczos_cost params k < hess_full_eigen_cost params := by
  simp only [lanczos_cost, hess_hvp_cost, hess_full_eigen_cost]
  nlinarith

/-- Concrete: 1M params, k=10: Lanczos = 20M ops, full = 10^18 ops.
    50 billion times cheaper.
    C++: this is why Augur's Hessian analysis is affordable. -/
example : lanczos_cost 1000000 10 = 20000000 := by native_decide
example : hess_full_eigen_cost 1000000 = 1000000000000000000 := by native_decide

/-! ## 6. Per-Layer Optimization Strategy

Based on measured Hessian properties, Augur selects a per-layer strategy.
Each strategy has different per-step cost and convergence speed.

C++: `Augur::select_optimizer(layer, hessian_info)`. -/

/-- Per-layer optimization strategy.
    C++: `enum class OptimizerKind : uint8_t { SGD, AdamW, KFAC, LBFGS };` -/
inductive HessOptStrategy where
  | SGD    -- simple gradient descent, lowest overhead
  | AdamW  -- adaptive moments, good for varied curvature
  | KFAC   -- natural gradient via K-FAC, best convergence
  | LBFGS  -- quasi-Newton, best for smooth problems
  deriving DecidableEq, Repr

/-- Relative per-step cost of each strategy (normalized: SGD = 1).
    C++: `Augur::optimizer_cost(kind)`. -/
def hess_strategy_cost : HessOptStrategy -> Nat
  | .SGD   => 1
  | .AdamW => 2   -- 2x SGD: maintains m, v buffers
  | .KFAC  => 6   -- ~3x AdamW: factor computation + inverse + apply
  | .LBFGS => 4   -- ~2x AdamW: history storage + two-loop recursion

/-- Relative convergence steps (normalized: KFAC = 1).
    Fewer = better. K-FAC converges fastest due to curvature info.
    C++: `Augur::predicted_steps_to_converge(kind, kappa)`. -/
def hess_strategy_steps : HessOptStrategy -> Nat
  | .SGD   => 6   -- 6x K-FAC steps needed
  | .AdamW => 3   -- 3x K-FAC steps needed
  | .KFAC  => 1   -- baseline: best convergence
  | .LBFGS => 2   -- 2x K-FAC: less curvature info than K-FAC

/-- Total normalized cost = per_step_cost * steps_to_converge.
    The strategy with lowest total cost wins.
    C++: `Augur::total_optimization_cost(kind)`. -/
def hess_strategy_total_cost (s : HessOptStrategy) : Nat :=
  hess_strategy_cost s * hess_strategy_steps s

/-- SGD has the lowest per-step cost.
    C++: SGD is the simplest optimizer; minimal overhead. -/
theorem hess_sgd_cheapest_per_step (s : HessOptStrategy) :
    hess_strategy_cost .SGD ≤ hess_strategy_cost s := by
  cases s <;> simp [hess_strategy_cost]

/-- K-FAC needs the fewest steps to converge.
    C++: natural gradient = steepest descent in distribution space. -/
theorem hess_kfac_fewest_steps (s : HessOptStrategy) :
    hess_strategy_steps .KFAC ≤ hess_strategy_steps s := by
  cases s <;> simp [hess_strategy_steps]

/-- Total cost comparison: SGD = 6, AdamW = 6, K-FAC = 6, LBFGS = 8.
    SGD, AdamW, and K-FAC are tied in normalized total cost.
    C++: Augur uses curvature info to break ties. -/
theorem hess_total_sgd : hess_strategy_total_cost .SGD = 6 := rfl
theorem hess_total_adamw : hess_strategy_total_cost .AdamW = 6 := rfl
theorem hess_total_kfac : hess_strategy_total_cost .KFAC = 6 := rfl
theorem hess_total_lbfgs : hess_strategy_total_cost .LBFGS = 8 := rfl

/-- LBFGS has the highest total normalized cost in our model.
    C++: LBFGS is rarely the best choice for deep learning. -/
theorem hess_lbfgs_most_expensive (s : HessOptStrategy) :
    hess_strategy_total_cost s ≤ hess_strategy_total_cost .LBFGS := by
  cases s <;> simp [hess_strategy_total_cost, hess_strategy_cost, hess_strategy_steps]

/-- All strategies have positive per-step cost. -/
theorem hess_strategy_cost_pos (s : HessOptStrategy) :
    0 < hess_strategy_cost s := by
  cases s <;> simp [hess_strategy_cost]

/-- All strategies require at least one step to converge. -/
theorem hess_strategy_steps_pos (s : HessOptStrategy) :
    0 < hess_strategy_steps s := by
  cases s <;> simp [hess_strategy_steps]

/-! ## 7. Saddle Point Detection

Top Hessian eigenvalues reveal the local geometry:
- All eigenvalues >= 0 -> local minimum
- Some eigenvalue < 0 -> saddle point (escape direction exists)
- All eigenvalues < 0 -> local maximum

C++: `Augur::classify_critical_point(eigenvalues)`. -/

/-- Detect whether a critical point is a saddle (has negative eigenvalue).
    C++: `Augur::is_saddle_point(top_eigenvalue)`. -/
def hess_is_saddle (top_eigenvalue : ℤ) : Bool := top_eigenvalue < 0

/-- Detect whether eigenvalues indicate a (local) minimum.
    All eigenvalues must be non-negative.
    C++: `Augur::is_local_minimum(eigenvalues)`. -/
def hess_is_minimum (eigenvalues : List ℤ) : Bool :=
  eigenvalues.all (· ≥ 0)

/-- At a minimum, no eigenvalue is negative.
    C++: `assert(!is_saddle_point(min_eigenvalue))` when is_local_minimum. -/
theorem hess_minimum_no_negative (eigenvalues : List ℤ)
    (h : hess_is_minimum eigenvalues = true) (e : ℤ) (he : e ∈ eigenvalues) :
    0 ≤ e := by
  simp only [hess_is_minimum, List.all_eq_true, decide_eq_true_eq] at h
  exact h e he

/-- If any eigenvalue is negative, it is NOT a minimum.
    C++: `Augur::detected_saddle()` triggers escape strategy. -/
theorem hess_negative_not_minimum (eigenvalues : List ℤ) (e : ℤ)
    (he : e ∈ eigenvalues) (hneg : e < 0) :
    hess_is_minimum eigenvalues = false := by
  by_contra h
  simp only [Bool.not_eq_false] at h
  have := hess_minimum_no_negative eigenvalues h e he
  omega

/-- A positive eigenvalue means saddle detection returns false.
    C++: positive curvature in this direction is safe. -/
theorem hess_positive_not_saddle (v : ℤ) (hv : 0 < v) :
    hess_is_saddle v = false := by
  simp [hess_is_saddle]; omega

/-- Zero eigenvalue is not a saddle (flat, but not negative curvature). -/
theorem hess_zero_not_saddle : hess_is_saddle 0 = false := by
  simp [hess_is_saddle]

/-- Negative eigenvalue IS a saddle point indicator.
    C++: `Augur::escape_direction()` uses the corresponding eigenvector. -/
theorem hess_negative_is_saddle (v : ℤ) (hv : v < 0) :
    hess_is_saddle v = true := by
  simp [hess_is_saddle]; exact hv

/-- Empty eigenvalue list is trivially a minimum. -/
theorem hess_empty_is_minimum : hess_is_minimum [] = true := rfl

/-- Concrete: eigenvalues [3, 1, -2] -> not a minimum (has negative). -/
example : hess_is_minimum [3, 1, -2] = false := by native_decide

/-- Concrete: eigenvalues [3, 1, 0] -> is a minimum (all >= 0). -/
example : hess_is_minimum [3, 1, 0] = true := by native_decide

/-! ## 8. Sharpness and Generalization

Sharp minima (high curvature / large lambda_max) generalize worse than flat
minima (low curvature / small lambda_max). The PAC-Bayes bound connects
Hessian trace to generalization gap.

C++: `Augur::sharpness_metric(layer)` -- monitors during training. -/

/-- Sharpness metric: simply the largest eigenvalue.
    Higher = sharper minimum = worse generalization expected.
    C++: `Augur::sharpness()` returns lambda_max. -/
def hess_sharpness (lambda_max : ℚ) : ℚ := lambda_max

/-- Flatness reward: inversely related to sharpness.
    reward = 1 / (1 + sharpness). Ranges from (0, 1].
    C++: `Augur::flatness_reward()` for model selection. -/
def hess_flatness_reward (sharpness : ℚ) : ℚ := 1 / (1 + sharpness)

/-- Flatness reward is positive when sharpness >= 0.
    C++: `assert(flatness_reward > 0)`. -/
theorem hess_flatness_pos (s : ℚ) (hs : 0 ≤ s) :
    0 < hess_flatness_reward s := by
  simp only [hess_flatness_reward]
  positivity

/-- Flatness reward is at most 1.
    Maximum achieved at sharpness = 0 (perfectly flat).
    C++: `assert(flatness_reward <= 1)`. -/
theorem hess_flatness_le_one (s : ℚ) (hs : 0 ≤ s) :
    hess_flatness_reward s ≤ 1 := by
  simp only [hess_flatness_reward]
  rw [div_le_one (by linarith : (0:ℚ) < 1 + s)]
  linarith

/-- Lower sharpness -> higher flatness reward (monotone decreasing).
    Flatter minimum = better generalization.
    C++: `Augur::prefer_flat_minimum()`. -/
theorem hess_flat_better (s1 s2 : ℚ) (hs1 : 0 ≤ s1) (_hs2 : 0 ≤ s2) (h : s1 ≤ s2) :
    hess_flatness_reward s2 ≤ hess_flatness_reward s1 := by
  simp only [hess_flatness_reward]
  apply div_le_div_of_nonneg_left (by norm_num : (0:ℚ) ≤ 1)
    (by linarith) (by linarith)

/-- Zero curvature gives maximum flatness reward (reward = 1).
    Perfectly flat loss landscape = best generalization.
    C++: ideal case (rarely achieved in practice). -/
theorem hess_zero_curvature_max_reward : hess_flatness_reward 0 = 1 := by
  simp [hess_flatness_reward]

/-- Flatness reward is bounded in (0, 1] for non-negative sharpness.
    C++: `Augur::validate_flatness()`. -/
theorem hess_flatness_bounded (s : ℚ) (hs : 0 ≤ s) :
    0 < hess_flatness_reward s ∧ hess_flatness_reward s ≤ 1 :=
  ⟨hess_flatness_pos s hs, hess_flatness_le_one s hs⟩

/-- Sharpness-aware minimization (SAM) perturbation radius:
    rho proportional to 1 / sqrt(sharpness). We model the squared
    relationship: rho^2 * sharpness should be constant.
    C++: `Augur::sam_radius(sharpness, budget)`. -/
def hess_sam_budget (rho_sq sharpness : ℚ) : ℚ := rho_sq * sharpness

/-- SAM budget is non-negative when both inputs are non-negative.
    C++: `assert(sam_budget >= 0)`. -/
theorem hess_sam_budget_nonneg (rho_sq sharpness : ℚ)
    (hr : 0 ≤ rho_sq) (hs : 0 ≤ sharpness) :
    0 ≤ hess_sam_budget rho_sq sharpness :=
  mul_nonneg hr hs

/-- Higher sharpness with fixed budget -> smaller perturbation radius.
    rho_sq = budget / sharpness decreases as sharpness increases.
    C++: SAM adapts perturbation to local curvature. -/
theorem hess_sam_tradeoff (budget s1 s2 : ℚ) (hb : 0 < budget)
    (hs1 : 0 < s1) (_hs2 : 0 < s2) (h : s1 ≤ s2) :
    budget / s2 ≤ budget / s1 := by
  apply div_le_div_of_nonneg_left (le_of_lt hb) hs1 h

/-- Concrete: sharpness 100, reward = 1/101 ~ 0.0099.
    High sharpness -> low reward -> bad generalization predicted.
    C++: `Augur::warn_sharp_minimum(layer)`. -/
example : hess_flatness_reward 100 = 1 / 101 := by
  simp [hess_flatness_reward]; norm_num

/-- Concrete: sharpness 0.01, reward = 1/1.01 ~ 0.99.
    Low sharpness -> high reward -> good generalization predicted.
    C++: `Augur::healthy_minimum(layer)`. -/
example : hess_flatness_reward (1/100) = 100 / 101 := by
  simp [hess_flatness_reward]; norm_num

/-! ## 9. Integration: Hessian Analysis Pipeline

Augur's Hessian analysis pipeline combines all components:
1. Lanczos iteration to get top-k eigenvalues
2. Condition number and optimal LR from eigenvalues
3. Saddle point detection from sign of eigenvalues
4. Sharpness metric from lambda_max
5. Strategy selection from curvature properties

C++: `Augur::full_hessian_analysis(model)`. -/

/-- Complete Hessian analysis result for one layer.
    C++: `struct HessianAnalysis { ... };` in Augur. -/
structure HessAnalysisResult where
  params : Nat
  lambda_max : ℚ
  lambda_min : ℚ
  h_params_pos : 0 < params
  h_min_pos : 0 < lambda_min
  h_ordered : lambda_min ≤ lambda_max

/-- Condition number from analysis result. -/
def HessAnalysisResult.kappa (r : HessAnalysisResult) : ℚ :=
  curvature_condition_number r.lambda_max r.lambda_min

/-- Optimal LR from analysis result. -/
def HessAnalysisResult.optimalLR (r : HessAnalysisResult) : ℚ :=
  curvature_optimal_lr r.lambda_max r.lambda_min

/-- Convergence rate from analysis result. -/
def HessAnalysisResult.convergenceRate (r : HessAnalysisResult) : ℚ :=
  curvature_convergence_rate r.kappa

/-- Sharpness from analysis result. -/
def HessAnalysisResult.sharpness (r : HessAnalysisResult) : ℚ :=
  hess_sharpness r.lambda_max

/-- Analysis cost: Lanczos with k=10 eigenvalues.
    C++: `Augur::hessian_analysis_cost(params)`. -/
def HessAnalysisResult.analysisCost (r : HessAnalysisResult) : Nat :=
  lanczos_cost r.params 10

/-- Kappa >= 1 for any analysis result.
    C++: `assert(kappa >= 1)`. -/
theorem HessAnalysisResult.kappa_ge_one (r : HessAnalysisResult) :
    1 ≤ r.kappa :=
  curvature_kappa_ge_one r.lambda_max r.lambda_min r.h_min_pos r.h_ordered

/-- Optimal LR is positive for any analysis result.
    C++: `assert(optimal_lr > 0)`. -/
theorem HessAnalysisResult.lr_pos (r : HessAnalysisResult) :
    0 < r.optimalLR :=
  curvature_lr_pos r.lambda_max r.lambda_min
    (lt_of_lt_of_le r.h_min_pos r.h_ordered) r.h_min_pos

/-- Convergence rate in [0, 1) for any analysis result.
    C++: `assert(0 <= rate && rate < 1)`. -/
theorem HessAnalysisResult.rate_bounded (r : HessAnalysisResult) :
    0 ≤ r.convergenceRate ∧ r.convergenceRate < 1 :=
  ⟨curvature_rate_nonneg r.kappa r.kappa_ge_one,
   curvature_rate_lt_one r.kappa r.kappa_ge_one⟩

/-- Analysis cost is linear in params (k=10 fixed).
    C++: Augur's overhead is bounded and predictable. -/
theorem HessAnalysisResult.cost_linear (r : HessAnalysisResult) :
    r.analysisCost = 20 * r.params := by
  simp [HessAnalysisResult.analysisCost, lanczos_cost, hess_hvp_cost]; ring

/-- Concrete: 10M parameter layer analysis.
    Cost = 200M ops (20 * 10M). -/
example : (HessAnalysisResult.mk 10000000 100 1
    (by norm_num) (by norm_num) (by norm_num)).analysisCost = 200000000 := by
  simp [HessAnalysisResult.analysisCost, lanczos_cost, hess_hvp_cost]

example : (HessAnalysisResult.mk 10000000 100 1
    (by norm_num) (by norm_num) (by norm_num)).kappa = 100 := by
  simp only [HessAnalysisResult.kappa, curvature_condition_number]; norm_num

example : (HessAnalysisResult.mk 10000000 100 1
    (by norm_num) (by norm_num) (by norm_num)).optimalLR = 2 / 101 := by
  simp only [HessAnalysisResult.optimalLR, curvature_optimal_lr]; norm_num

/-! ## Summary

Key results:

**Hessian-Vector Product:**
- `hess_hvp_cheaper_than_full`: HVP < full Hessian when N > 2
- `hess_hvp_linear`: HVP cost is linear in params
- `hess_full_quadratic`: full Hessian cost is quadratic
- `hess_hvp_savings_ratio`: HVP/full = 2/N

**K-FAC Approximation:**
- `kfac_cheaper_than_full`: K-FAC < full Fisher when d_in, d_out > 1
- `kfac_inverse_cheaper`: K-FAC inverse < full Fisher inverse
- `kfac_kronecker_product_size`: Kronecker product matches Fisher dimensions

**Natural Gradient:**
- `natgrad_more_expensive_than_sgd`: K-FAC per-step > SGD per-step
- `natgrad_square_overhead`: K-FAC costs 3x SGD for square layers
- `natgrad_kfac_wins_when_fewer_steps`: K-FAC wins when step reduction > cost increase

**Condition Number & Learning Rate:**
- `curvature_kappa_ge_one`: kappa >= 1
- `curvature_lr_bounded`: lr* <= 2/lambda_max
- `curvature_lr_pos`: lr* > 0
- `curvature_rate_lt_one`: convergence rate < 1 (guaranteed convergence)
- `curvature_rate_monotone`: larger kappa -> slower convergence

**Lanczos Iteration:**
- `lanczos_cost_linear_k`: cost scales linearly with k
- `lanczos_cost_linear_params`: cost scales linearly with params
- `lanczos_cheaper_than_full_spectrum`: k HVPs < full eigendecomposition

**Strategy Selection:**
- `hess_sgd_cheapest_per_step`: SGD has lowest per-step cost
- `hess_kfac_fewest_steps`: K-FAC needs fewest steps
- `hess_lbfgs_most_expensive`: LBFGS has highest total cost

**Saddle Points:**
- `hess_minimum_no_negative`: minimum iff all eigenvalues >= 0
- `hess_negative_not_minimum`: negative eigenvalue -> not a minimum
- `hess_negative_is_saddle`: negative eigenvalue -> saddle detected

**Sharpness & Generalization:**
- `hess_flat_better`: lower sharpness -> higher flatness reward
- `hess_flatness_bounded`: reward in (0, 1] for non-negative sharpness
- `hess_zero_curvature_max_reward`: perfectly flat -> reward = 1
-/

end Crucible
