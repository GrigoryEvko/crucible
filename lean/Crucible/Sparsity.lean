import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.Sparsity -- L9 Structured Sparsity, Pruning & Sparse Computation

From L9 Models (MANIFESTO.md):

  "Matrix structure discovery per layer:
   - Full-rank -> dense matmul
   - Low-rank (r << d) -> A(d x r) . B(r x d), 2x cheaper at r=d/4
   - Near-Toeplitz -> depthwise conv + correction, 10x cheaper
   - Sparse (>95%) -> cuSPARSE
   - Block-diagonal -> smaller independent matmuls"

From L8 Layers:

  "Selective backpropagation: skip backward for layers with gradient norm < eps
   for N steps. 50-70% layers skippable late in training."

This file formalizes:

1. **Sparsity patterns**: unstructured, block-sparse, N:M structured
2. **N:M structured sparsity**: density, speedup, NVIDIA 2:4 support
3. **Pruning masks**: element-level masks, density, count bounds
4. **Block sparsity**: block-level pruning, memory savings, speedup
5. **Sparse cost model**: FLOPs proportional to density, speedup = 1/density
6. **Pruning error model**: magnitude-based pruning minimizes error
7. **Layer-wise pruning sensitivity**: per-layer density allocation
8. **Matrix structure discovery**: classification into computation strategies
9. **Gradual pruning schedule**: linear interpolation from dense to target

All quantities use Nat (counts, sizes) or Q (densities, errors, ratios).
Zero sorry.

C++ correspondence:
- `Augur::matrix_structure_discovery()` -- per-layer structure classification
- `Augur::sparsity_analysis()` -- density measurement and pruning recommendation
- `CKernel::SPMM_GNN` / cuSPARSE dispatch -- sparse computation kernels
- `Meridian::sparsity_config()` -- Z3-optimal per-layer sparsity
- `BackgroundThread::compile_sparse()` -- sparse kernel compilation
-/

namespace Crucible

/-! ## 1. Sparsity Patterns

From L9: structured sparsity patterns determine which kernel to dispatch.
Unstructured = arbitrary zeros (cuSPARSE). Block = zero entire blocks.
N:M = hardware-friendly structured pattern (NVIDIA sparse tensor cores).

C++: `enum class SparsePattern : uint8_t { Unstructured, Block, NM };` -/

/-- Sparsity pattern classification. Each pattern maps to a kernel strategy.
    C++: `Augur::classify_sparsity(weight_tensor)`. -/
inductive SparsePattern where
  | Unstructured              -- arbitrary zero locations, cuSPARSE
  | BlockSparse (block_size : Nat)  -- zero in block_size x block_size blocks
  | NM (n m : Nat)            -- N nonzeros per M elements (e.g. 2:4)
  deriving Repr

/-- Classification is exhaustive: every SparsePattern is one of the three forms.
    C++: `classify_sparsity()` always returns a valid enum. -/
theorem sparse_pattern_exhaustive (p : SparsePattern) :
    (∃ bs, p = .BlockSparse bs) ∨ (∃ n m, p = .NM n m) ∨ p = .Unstructured := by
  cases p with
  | Unstructured => right; right; rfl
  | BlockSparse bs => left; exact ⟨bs, rfl⟩
  | NM n m => right; left; exact ⟨n, m, rfl⟩

/-! ## 2. N:M Structured Sparsity

NVIDIA A100+ sparse tensor cores support 2:4 sparsity natively: exactly 2
nonzeros per group of 4 elements. Hardware delivers 2x throughput for 50%
density. Generalized: N nonzeros per M elements.

C++: `Meridian::nm_sparsity_supported(n, m, device_capability)`. -/

/-- Density of N:M sparsity pattern: fraction of nonzero elements.
    For 2:4: density = 2/4 = 1/2.
    C++: `Augur::nm_density(n, m)`. -/
def sparse_nm_density (n m : Nat) (_hm : 0 < m) : ℚ :=
  (n : ℚ) / (m : ℚ)

/-- N:M density is at most 1 when n <= m.
    C++: `assert(n <= m)` in N:M sparsity configuration. -/
theorem sparse_nm_density_le_one (n m : Nat) (hm : 0 < m) (hnm : n ≤ m) :
    sparse_nm_density n m hm ≤ 1 := by
  simp only [sparse_nm_density]
  rw [div_le_one (by exact_mod_cast hm : (0 : ℚ) < m)]
  exact_mod_cast hnm

/-- N:M density is non-negative.
    C++: density is always >= 0 (fraction of elements). -/
theorem sparse_nm_density_nonneg (n m : Nat) (hm : 0 < m) :
    0 ≤ sparse_nm_density n m hm :=
  div_nonneg (Nat.cast_nonneg n) (Nat.cast_nonneg m)

/-- N:M density is positive when n > 0.
    C++: at least one nonzero per group. -/
theorem sparse_nm_density_pos (n m : Nat) (hm : 0 < m) (hn : 0 < n) :
    0 < sparse_nm_density n m hm := by
  simp only [sparse_nm_density]
  exact div_pos (by exact_mod_cast hn) (by exact_mod_cast hm)

/-- 2:4 density = 1/2.
    C++: NVIDIA A100+ hardware sparsity format. -/
theorem sparse_24_density : sparse_nm_density 2 4 (by omega) = 1 / 2 := by
  simp [sparse_nm_density]; norm_num

/-- N:M speedup = m/n (inverse of density).
    2:4 gives 4/2 = 2x throughput on sparse tensor cores.
    C++: `Meridian::nm_speedup(n, m)`. -/
def sparse_nm_speedup (n m : Nat) (_hn : 0 < n) : ℚ :=
  (m : ℚ) / (n : ℚ)

/-- 2:4 speedup = 2x.
    C++: NVIDIA sparse tensor core delivers exactly 2x for 2:4. -/
theorem sparse_24_speedup : sparse_nm_speedup 2 4 (by omega) = 2 := by
  simp [sparse_nm_speedup]; norm_num

/-- N:M speedup >= 1 when n <= m (sparsity never slows down).
    C++: sparse kernel is at least as fast as dense. -/
theorem sparse_nm_speedup_ge_one (n m : Nat) (hn : 0 < n) (hnm : n ≤ m) :
    1 ≤ sparse_nm_speedup n m hn := by
  simp only [sparse_nm_speedup]
  rw [le_div_iff₀ (by exact_mod_cast hn : (0 : ℚ) < n)]
  simp
  exact_mod_cast hnm

/-- Speedup * density = 1 (they are inverses).
    C++: speedup = 1/density, fundamental identity. -/
theorem sparse_nm_speedup_density_inv (n m : Nat) (hn : 0 < n) (hm : 0 < m) :
    sparse_nm_speedup n m hn * sparse_nm_density n m hm = 1 := by
  simp only [sparse_nm_speedup, sparse_nm_density]
  rw [div_mul_div_comm, div_eq_one_iff_eq]
  · ring
  · exact mul_ne_zero
      (Nat.cast_ne_zero.mpr (by omega))
      (Nat.cast_ne_zero.mpr (by omega))

/-- Speedup is positive for any m when n > 0.
    C++: speedup ratio is always well-defined. -/
theorem sparse_nm_speedup_pos (n m : Nat) (hn : 0 < n) (hm : 0 < m) :
    0 < sparse_nm_speedup n m hn := by
  simp only [sparse_nm_speedup]
  exact div_pos (by exact_mod_cast hm) (by exact_mod_cast hn)

/-! ## 3. Pruning Masks

A pruning mask is a list of booleans: True = keep, False = prune.
Density = fraction of True entries. Count = number of True entries.

C++: represented as a bitmask in `Augur::PruneMask`. -/

/-- Count of nonzero (kept) elements in a pruning mask.
    C++: `__builtin_popcountll(mask)` for 64-bit mask words. -/
def sparse_mask_count (mask : List Bool) : Nat :=
  (mask.filter id).length

/-- Density of a pruning mask: fraction of kept elements.
    C++: `Augur::mask_density(mask)`. -/
def sparse_mask_density (mask : List Bool) (_hm : 0 < mask.length) : ℚ :=
  (sparse_mask_count mask : ℚ) / (mask.length : ℚ)

/-- Nonzero count is at most mask length: can't keep more than we have.
    C++: `assert(popcount(mask) <= total_elements)`. -/
theorem sparse_mask_count_le_length (mask : List Bool) :
    sparse_mask_count mask ≤ mask.length := by
  simp [sparse_mask_count]
  exact List.length_filter_le _ _

/-- Mask density is at most 1.
    C++: `assert(density <= 1.0)`. -/
theorem sparse_mask_density_le_one (mask : List Bool) (hm : 0 < mask.length) :
    sparse_mask_density mask hm ≤ 1 := by
  simp only [sparse_mask_density]
  rw [div_le_one (by exact_mod_cast hm : (0 : ℚ) < mask.length)]
  exact_mod_cast sparse_mask_count_le_length mask

/-- Mask density is non-negative.
    C++: density is a ratio of non-negative values. -/
theorem sparse_mask_density_nonneg (mask : List Bool) (hm : 0 < mask.length) :
    0 ≤ sparse_mask_density mask hm :=
  div_nonneg (Nat.cast_nonneg _) (Nat.cast_nonneg _)

/-- All-true mask has count = length (no pruning).
    C++: mask = 0xFFFF...F means keep everything. -/
theorem sparse_mask_all_true_count (n : Nat) :
    sparse_mask_count (List.replicate n true) = n := by
  simp [sparse_mask_count]

/-- All-true mask has density = 1.
    C++: no pruning applied. -/
theorem sparse_mask_all_true_density (n : Nat) (hn : 0 < n) :
    sparse_mask_density (List.replicate n true) (by simp; omega) = 1 := by
  simp only [sparse_mask_density, sparse_mask_all_true_count, List.length_replicate]
  exact div_self (Nat.cast_ne_zero.mpr (by omega))

/-- All-false mask has count = 0 (everything pruned).
    C++: mask = 0x0 means prune everything. -/
theorem sparse_mask_all_false_count (n : Nat) :
    sparse_mask_count (List.replicate n false) = 0 := by
  simp [sparse_mask_count]

/-- All-false mask has density = 0.
    C++: complete pruning, density = 0. -/
theorem sparse_mask_all_false_density (n : Nat) (hn : 0 < n) :
    sparse_mask_density (List.replicate n false) (by simp; omega) = 0 := by
  simp only [sparse_mask_density, sparse_mask_all_false_count, Nat.cast_zero, zero_div]

/-! ## 4. Block Sparsity

Group parameters into blocks of size B. Prune entire blocks.
Block sparsity is more hardware-friendly than unstructured (better memory
access patterns, can use dense kernels on surviving blocks).

C++: `Augur::block_sparsity_analysis(weight, block_size)`. -/

/-- Number of blocks for a tensor of given total elements.
    C++: `total_elements / block_size` (integer division). -/
def block_sparse_num_blocks (total : Nat) (block_size : Nat) : Nat :=
  total / block_size

/-- Number of surviving (nonzero) blocks after pruning.
    C++: `total_blocks - pruned_blocks`. -/
def block_sparse_surviving (total_blocks pruned : Nat) (_hp : pruned ≤ total_blocks) : Nat :=
  total_blocks - pruned

/-- Surviving blocks <= total blocks.
    C++: `assert(surviving <= total)`. -/
theorem block_sparse_surviving_le (total_blocks pruned : Nat) (hp : pruned ≤ total_blocks) :
    block_sparse_surviving total_blocks pruned hp ≤ total_blocks :=
  Nat.sub_le total_blocks pruned

/-- Memory usage after block pruning: surviving blocks * block_size.
    C++: `surviving * block_size` bytes in compressed format. -/
def block_sparse_memory (total_blocks pruned block_size : Nat)
    (hp : pruned ≤ total_blocks) : Nat :=
  block_sparse_surviving total_blocks pruned hp * block_size

/-- Block sparse memory <= dense memory.
    Pruning blocks never increases memory usage.
    C++: `assert(sparse_memory <= dense_memory)`. -/
theorem block_sparse_memory_le_dense (total_blocks pruned block_size : Nat)
    (hp : pruned ≤ total_blocks) :
    block_sparse_memory total_blocks pruned block_size hp ≤ total_blocks * block_size := by
  simp only [block_sparse_memory, block_sparse_surviving]
  exact Nat.mul_le_mul_right block_size (Nat.sub_le total_blocks pruned)

/-- Block sparsity speedup model: b/(b-k) where b = total blocks, k = pruned.
    Only compute on surviving blocks.
    C++: `Augur::block_sparse_speedup(total, pruned)`. -/
def block_sparse_speedup (total_blocks pruned : Nat)
    (_hp : pruned ≤ total_blocks) (_hs : 0 < total_blocks - pruned) : ℚ :=
  (total_blocks : ℚ) / ((total_blocks - pruned : Nat) : ℚ)

/-- Block sparse speedup >= 1 (pruning never slows down).
    C++: fewer blocks = less compute. -/
theorem block_sparse_speedup_ge_one (total_blocks pruned : Nat)
    (hp : pruned ≤ total_blocks) (hs : 0 < total_blocks - pruned) :
    1 ≤ block_sparse_speedup total_blocks pruned hp hs := by
  simp only [block_sparse_speedup]
  rw [le_div_iff₀ (by exact_mod_cast hs : (0 : ℚ) < (total_blocks - pruned : Nat))]
  simp only [one_mul]
  have : total_blocks - pruned ≤ total_blocks := Nat.sub_le total_blocks pruned
  exact_mod_cast this

/-- Pruning zero blocks gives speedup = 1 (no change).
    C++: no pruning = no speedup. -/
theorem block_sparse_speedup_zero_prune (b : Nat) (hb : 0 < b) :
    block_sparse_speedup b 0 (Nat.zero_le b) (by omega) = 1 := by
  simp only [block_sparse_speedup, Nat.sub_zero]
  exact div_self (Nat.cast_ne_zero.mpr (by omega))

/-- More pruning -> higher speedup (monotone).
    C++: pruning more blocks always improves throughput. -/
theorem block_sparse_speedup_monotone (b k₁ k₂ : Nat)
    (hk₁ : k₁ ≤ b) (hk₂ : k₂ ≤ b)
    (hs₁ : 0 < b - k₁) (hs₂ : 0 < b - k₂)
    (h : k₁ ≤ k₂) :
    block_sparse_speedup b k₁ hk₁ hs₁ ≤ block_sparse_speedup b k₂ hk₂ hs₂ := by
  simp only [block_sparse_speedup]
  have hb_pos : (0 : ℚ) < b := by exact_mod_cast Nat.zero_lt_of_lt (by omega : 0 < b)
  have hs₂_pos : (0 : ℚ) < (b - k₂ : Nat) := by exact_mod_cast hs₂
  have hs₁_pos : (0 : ℚ) < (b - k₁ : Nat) := by exact_mod_cast hs₁
  apply div_le_div_of_nonneg_left hb_pos.le hs₂_pos
  have : b - k₂ ≤ b - k₁ := Nat.sub_le_sub_left h b
  exact_mod_cast this

/-! ## 5. Sparse Cost Model

Dense matmul: O(n * m) FLOPs for (n x k) * (k x m).
Sparse at density d: O(d * n * m) FLOPs (only compute nonzero entries).

C++: `Augur::sparse_flops(dense_flops, density)`. -/

/-- Sparse FLOPs given dense FLOPs and density (as numerator/denominator).
    sparse_flops = dense_flops * density_num / density_den.
    C++: `sparse_flops = dense_flops * density`. -/
def sparse_flops (dense_flops density_num density_den : Nat) : Nat :=
  dense_flops * density_num / density_den

/-- Sparse FLOPs <= dense FLOPs when density <= 1 (num <= den).
    C++: sparse computation is never more expensive than dense. -/
theorem sparse_flops_le_dense (dense_flops density_num density_den : Nat)
    (hd : 0 < density_den) (hle : density_num ≤ density_den) :
    sparse_flops dense_flops density_num density_den ≤ dense_flops := by
  simp only [sparse_flops]
  calc dense_flops * density_num / density_den
      ≤ dense_flops * density_den / density_den := by
        apply Nat.div_le_div_right
        exact Nat.mul_le_mul_left dense_flops hle
    _ = dense_flops := Nat.mul_div_cancel dense_flops hd

/-- At density = 1 (num = den), sparse FLOPs = dense FLOPs.
    C++: dense matrix = 100% density = no savings. -/
theorem sparse_flops_at_full_density (dense_flops d : Nat) (hd : 0 < d) :
    sparse_flops dense_flops d d = dense_flops := by
  simp [sparse_flops, Nat.mul_div_cancel _ hd]

/-- At density = 0, sparse FLOPs = 0 (everything pruned).
    C++: fully sparse = no computation. -/
theorem sparse_flops_at_zero_density (dense_flops d : Nat) (_hd : 0 < d) :
    sparse_flops dense_flops 0 d = 0 := by
  simp [sparse_flops]

/-- 50% density halves FLOPs (when dense_flops divisible by 2).
    C++: 2:4 sparsity halves compute on sparse tensor cores. -/
theorem sparse_50_half_flops (f : Nat) :
    sparse_flops (2 * f) 1 2 = f := by
  simp only [sparse_flops, Nat.mul_one]
  exact Nat.mul_div_cancel_left f (by omega)

/-- Sparse speedup as rational: 1/density = den/num.
    C++: `Augur::sparse_speedup(density)`. -/
def sparse_speedup_ratio (density_num density_den : Nat) (_hn : 0 < density_num) : ℚ :=
  (density_den : ℚ) / (density_num : ℚ)

/-- Speedup at 50% density = 2x.
    C++: 2:4 sparsity delivers 2x throughput. -/
theorem sparse_speedup_50 : sparse_speedup_ratio 1 2 (by omega) = 2 := by
  simp [sparse_speedup_ratio]

/-- Speedup at 25% density = 4x.
    C++: aggressive pruning with 75% zeros. -/
theorem sparse_speedup_25 : sparse_speedup_ratio 1 4 (by omega) = 4 := by
  simp [sparse_speedup_ratio]

/-- Speedup >= 1 when density <= 1 (num <= den).
    C++: sparsity never slows computation. -/
theorem sparse_speedup_ge_one (num den : Nat) (hn : 0 < num) (hle : num ≤ den) :
    1 ≤ sparse_speedup_ratio num den hn := by
  simp only [sparse_speedup_ratio]
  rw [le_div_iff₀ (by exact_mod_cast hn : (0 : ℚ) < num)]
  simp only [one_mul]
  exact_mod_cast hle

/-! ## 6. Pruning Error Model

Removing weights introduces approximation error. Magnitude-based pruning
(remove smallest weights first) minimizes the L2 error.

C++: `Augur::pruning_error(weight_tensor, mask)`. -/

/-- Pruning error ratio: sum of pruned magnitudes / sum of all magnitudes.
    Both arguments are non-negative rationals.
    C++: `pruned_norm / total_norm`. -/
def prune_error_ratio (total_magnitude pruned_magnitude : ℚ)
    (_ht : 0 < total_magnitude) : ℚ :=
  pruned_magnitude / total_magnitude

/-- Pruning error is non-negative when pruned magnitudes are non-negative.
    C++: `assert(error >= 0)`. -/
theorem prune_error_nonneg (total pruned : ℚ) (ht : 0 < total) (hp : 0 ≤ pruned) :
    0 ≤ prune_error_ratio total pruned ht :=
  div_nonneg hp (le_of_lt ht)

/-- Pruning error <= 1 when pruned <= total (can't lose more than everything).
    C++: `assert(error <= 1.0)`. -/
theorem prune_error_le_one (total pruned : ℚ) (ht : 0 < total) (hp : pruned ≤ total) :
    prune_error_ratio total pruned ht ≤ 1 := by
  simp only [prune_error_ratio]
  rw [div_le_one (by linarith)]
  exact hp

/-- Zero pruning = zero error.
    C++: keeping everything introduces no approximation. -/
theorem prune_error_zero (total : ℚ) (ht : 0 < total) :
    prune_error_ratio total 0 ht = 0 := by
  simp [prune_error_ratio]

/-- Full pruning = error of 1.
    C++: removing all weights loses all information. -/
theorem prune_error_full (total : ℚ) (ht : 0 < total) :
    prune_error_ratio total total ht = 1 :=
  div_self (ne_of_gt ht)

/-- Monotonicity: pruning more (larger pruned_magnitude) increases error.
    C++: removing more weights always increases approximation error. -/
theorem prune_error_monotone (total p₁ p₂ : ℚ) (ht : 0 < total)
    (h : p₁ ≤ p₂) :
    prune_error_ratio total p₁ ht ≤ prune_error_ratio total p₂ ht := by
  simp only [prune_error_ratio]
  exact div_le_div_of_nonneg_right h (le_of_lt ht)

/-- Magnitude-based pruning minimizes error: for two candidate pruned sets,
    the one with smaller total magnitude has lower error.
    C++: `Augur::sort_by_magnitude(weights)` before pruning. -/
theorem prune_smallest_minimizes_error (total m₁ m₂ : ℚ) (ht : 0 < total)
    (_hm1 : 0 ≤ m₁) (_hm2 : 0 ≤ m₂) (h : m₁ ≤ m₂) :
    prune_error_ratio total m₁ ht ≤ prune_error_ratio total m₂ ht :=
  prune_error_monotone total m₁ m₂ ht h

/-! ## 7. Layer-wise Pruning Sensitivity

Different layers tolerate different amounts of pruning. Sensitive layers
(high gradient norm, high impact on loss) should keep more weights.
Insensitive layers can be aggressively pruned.

C++: `Augur::layer_sensitivity(layer_id)` computes sensitivity from gradient
statistics and Fisher information. -/

/-- Per-layer pruning configuration.
    C++: `struct PruneLayerConfig { uint32_t id; float sensitivity, density; };` -/
structure PruneLayerConfig where
  prune_layer_id : Nat
  prune_sensitivity : ℚ     -- error per unit of pruning (lower = more prunable)
  prune_density : ℚ          -- current density (1 = dense)
  h_sens_nonneg : 0 ≤ prune_sensitivity
  h_dens_nonneg : 0 ≤ prune_density
  h_dens_le_one : prune_density ≤ 1

/-- Effective error for a layer: sensitivity * (1 - density).
    Higher sensitivity + lower density = more error.
    C++: `Augur::layer_pruning_error(config)`. -/
def prune_layer_error (c : PruneLayerConfig) : ℚ :=
  c.prune_sensitivity * (1 - c.prune_density)

/-- Layer error is non-negative.
    C++: `assert(layer_error >= 0)`. -/
theorem prune_layer_error_nonneg (c : PruneLayerConfig) :
    0 ≤ prune_layer_error c := by
  simp only [prune_layer_error]
  exact mul_nonneg c.h_sens_nonneg (by linarith [c.h_dens_le_one])

/-- Dense layer (density = 1) has zero error.
    C++: keeping all weights introduces no pruning error. -/
theorem prune_layer_error_dense (c : PruneLayerConfig) (hd : c.prune_density = 1) :
    prune_layer_error c = 0 := by
  simp [prune_layer_error, hd]

/-- Higher density -> lower error (for same sensitivity).
    C++: Augur allocates higher density to more sensitive layers. -/
theorem prune_layer_error_density_monotone (s d₁ d₂ : ℚ)
    (hs : 0 ≤ s) (_hd₁ : 0 ≤ d₁) (_hd₁' : d₁ ≤ 1)
    (_hd₂ : 0 ≤ d₂) (_hd₂' : d₂ ≤ 1) (h : d₁ ≤ d₂) :
    s * (1 - d₂) ≤ s * (1 - d₁) := by
  exact mul_le_mul_of_nonneg_left (by linarith) hs

/-- Higher sensitivity -> more error (for same density).
    C++: sensitive layers contribute more pruning error. -/
theorem prune_layer_error_sensitivity_monotone (s₁ s₂ d : ℚ)
    (_hs₁ : 0 ≤ s₁) (_hs₂ : 0 ≤ s₂)
    (_hd : 0 ≤ d) (hd' : d ≤ 1) (h : s₁ ≤ s₂) :
    s₁ * (1 - d) ≤ s₂ * (1 - d) := by
  exact mul_le_mul_of_nonneg_right h (by linarith)

/-- Uniform density is suboptimal: for two layers with different sensitivities,
    giving more density to the more sensitive layer reduces total error.
    Specifically: if s₁ > s₂ and we shift delta density from layer 2 to layer 1,
    total error decreases.
    C++: `Augur::optimize_per_layer_density()` solves this allocation. -/
theorem prune_uniform_suboptimal (s₁ s₂ d delta : ℚ)
    (_hs₁ : 0 ≤ s₁) (_hs₂ : 0 ≤ s₂)
    (_hd : 0 ≤ d) (_hd' : d ≤ 1)
    (hdelta : 0 < delta) (_hd_delta : d + delta ≤ 1) (_hd_sub : delta ≤ d)
    (h_sens : s₂ < s₁) :
    -- Shifted allocation: layer 1 gets d+delta, layer 2 gets d-delta
    -- Total error with shift < total error with uniform d
    s₁ * (1 - (d + delta)) + s₂ * (1 - (d - delta)) <
    s₁ * (1 - d) + s₂ * (1 - d) := by
  nlinarith

/-! ## 8. Matrix Structure Discovery

From L9: "Matrix structure discovery per layer" classifies each weight
matrix into a computation strategy. Each strategy has a cost model.

C++: `Augur::discover_matrix_structure(weight)`. -/

/-- Matrix structure classification for a weight matrix.
    C++: `enum class MatrixStructure { FullRank, LowRank, BlockDiagonal, Sparse };` -/
inductive MatrixStructure where
  | FullRank                              -- dense matmul, O(d^2)
  | LowRank (rank : Nat) (dim : Nat)     -- factored A(d x r) . B(r x d), O(2dr)
  | BlockDiagonal (num_blocks : Nat)      -- k independent (d/k x d/k) matmuls
  | Sparse (density_num density_den : Nat) -- sparse computation at given density
  deriving Repr

/-- Cost of dense (full-rank) computation for d x d matrix.
    C++: d * d FLOPs for standard GEMM. -/
def sparse_full_rank_cost (d : Nat) : Nat := d * d

/-- Cost of low-rank factored computation: 2 * d * r.
    A(d x r) . B(r x d) = d*r + r*d = 2*d*r.
    C++: `Augur::low_rank_cost(dim, rank)`. -/
def sparse_low_rank_cost (d r : Nat) : Nat := 2 * d * r

/-- Cost of block-diagonal computation: k * (d/k)^2 = d^2/k.
    C++: `Augur::block_diagonal_cost(dim, num_blocks)`. -/
def sparse_block_diag_cost (d k : Nat) : Nat := d * d / k

/-- Low-rank is cheaper than full-rank when 2*r < d.
    THE fundamental low-rank speedup theorem.
    C++: "Low-rank (r << d) -> A(d x r) . B(r x d), 2x cheaper at r=d/4". -/
theorem sparse_low_rank_cheaper (d r : Nat) (_hd : 0 < d) (h : 2 * r < d) :
    sparse_low_rank_cost d r < sparse_full_rank_cost d := by
  simp only [sparse_low_rank_cost, sparse_full_rank_cost]
  nlinarith

/-- Low-rank cost <= full-rank cost when 2*r <= d.
    C++: low-rank factorization is never worse when rank is small enough. -/
theorem sparse_low_rank_le_full (d r : Nat) (h : 2 * r ≤ d) :
    sparse_low_rank_cost d r ≤ sparse_full_rank_cost d := by
  simp only [sparse_low_rank_cost, sparse_full_rank_cost]
  nlinarith

/-- At rank = d/4, low-rank cost <= full-rank cost.
    2*d*(d/4) <= d*d because 2*(d/4) <= d for natural number division.
    C++: "2x cheaper at r=d/4". -/
theorem sparse_low_rank_quarter_cost (d : Nat) (_hd : 0 < d) :
    sparse_low_rank_cost d (d / 4) ≤ sparse_full_rank_cost d := by
  simp only [sparse_low_rank_cost, sparse_full_rank_cost]
  have : 2 * (d / 4) ≤ d := by omega
  calc 2 * d * (d / 4) = d * (2 * (d / 4)) := by ring
    _ ≤ d * d := Nat.mul_le_mul_left d this

/-- Block-diagonal is cheaper than full-rank when k > 1.
    k independent (d/k x d/k) matmuls total d^2/k < d^2.
    C++: "Block-diagonal -> smaller independent matmuls". -/
theorem sparse_block_diag_cheaper (d k : Nat) (hd : 0 < d) (hk : 1 < k) :
    sparse_block_diag_cost d k < sparse_full_rank_cost d := by
  simp only [sparse_block_diag_cost, sparse_full_rank_cost]
  exact Nat.div_lt_self (Nat.mul_pos hd hd) hk

/-- Block-diagonal cost <= full-rank cost for any k >= 1.
    C++: block structure is never worse than dense. -/
theorem sparse_block_diag_le_full (d k : Nat) (_hk : 0 < k) :
    sparse_block_diag_cost d k ≤ sparse_full_rank_cost d := by
  simp only [sparse_block_diag_cost, sparse_full_rank_cost]
  exact Nat.div_le_self (d * d) k

/-- Structure detection is complete: every matrix falls into one category.
    C++: `discover_matrix_structure()` always returns a valid classification. -/
theorem sparse_structure_detection_complete (s : MatrixStructure) :
    s = .FullRank ∨
    (∃ r d, s = .LowRank r d) ∨
    (∃ k, s = .BlockDiagonal k) ∨
    (∃ n d, s = .Sparse n d) := by
  cases s with
  | FullRank => left; rfl
  | LowRank r d => right; left; exact ⟨r, d, rfl⟩
  | BlockDiagonal k => right; right; left; exact ⟨k, rfl⟩
  | Sparse n d => right; right; right; exact ⟨n, d, rfl⟩

/-- Cost model for each structure type given dimension d.
    C++: `Augur::structure_cost(structure, dim)`. -/
def sparse_structure_cost (s : MatrixStructure) (d : Nat) : Nat :=
  match s with
  | .FullRank => sparse_full_rank_cost d
  | .LowRank r _ => sparse_low_rank_cost d r
  | .BlockDiagonal k => sparse_block_diag_cost d k
  | .Sparse num den => sparse_flops (sparse_full_rank_cost d) num den

/-- Full-rank structure always has the same cost as dense matmul.
    C++: FullRank = no optimization applied. -/
theorem sparse_full_rank_structure_cost (d : Nat) :
    sparse_structure_cost .FullRank d = d * d := rfl

/-! ## 9. Gradual Pruning Schedule

Pruning happens gradually during training. Linear interpolation from
density = 1 (dense) at step 0 to target density at the final step.
This avoids sudden quality drops from aggressive one-shot pruning.

C++: `Augur::pruning_schedule(target, total_steps, current_step)`.
Zhu & Gupta 2017: "To Prune or Not to Prune". -/

/-- Gradual pruning density at a given step.
    Linear interpolation: density(t) = 1 - (1 - target) * t / T.
    At t=0: density = 1 (dense). At t=T: density = target.
    C++: `Augur::scheduled_density(target, total_steps, current_step)`. -/
def prune_schedule_density (target : ℚ) (total_steps current_step : Nat)
    (_ht : 0 < total_steps) : ℚ :=
  1 - (1 - target) * (current_step : ℚ) / (total_steps : ℚ)

/-- At step 0, density = 1 (start dense).
    C++: training begins with no pruning. -/
theorem prune_schedule_starts_dense (target : ℚ) (T : Nat) (hT : 0 < T) :
    prune_schedule_density target T 0 hT = 1 := by
  simp [prune_schedule_density]

/-- At the final step, density = target.
    C++: pruning completes at the scheduled step. -/
theorem prune_schedule_reaches_target (target : ℚ) (T : Nat) (hT : 0 < T) :
    prune_schedule_density target T T hT = target := by
  simp only [prune_schedule_density]
  have hT_ne : (T : ℚ) ≠ 0 := Nat.cast_ne_zero.mpr (by omega)
  rw [mul_div_cancel_right₀ _ hT_ne]
  ring

/-- Schedule is monotone decreasing: density decreases over steps.
    Later steps have lower density (more pruning).
    C++: pruning is gradual and monotonic. -/
theorem prune_schedule_monotone (target : ℚ) (T t₁ t₂ : Nat)
    (hT : 0 < T) (htgt : target ≤ 1)
    (h12 : t₁ ≤ t₂) (_ht₂ : t₂ ≤ T) :
    prune_schedule_density target T t₂ hT ≤ prune_schedule_density target T t₁ hT := by
  simp only [prune_schedule_density]
  have hT_pos : (0 : ℚ) < T := by exact_mod_cast hT
  have h_coeff_nn : 0 ≤ (1 - target) := by linarith
  have : (1 - target) * (↑t₁ : ℚ) / (↑T : ℚ) ≤ (1 - target) * (↑t₂ : ℚ) / (↑T : ℚ) := by
    apply div_le_div_of_nonneg_right _ hT_pos.le
    exact mul_le_mul_of_nonneg_left (by exact_mod_cast h12) h_coeff_nn
  linarith

/-- Schedule density is in [target, 1] for valid steps.
    C++: `assert(density >= target && density <= 1.0)`. -/
theorem prune_schedule_bounded (target : ℚ) (T t : Nat)
    (hT : 0 < T) (_htgt_nn : 0 ≤ target) (htgt : target ≤ 1)
    (ht : t ≤ T) :
    target ≤ prune_schedule_density target T t hT ∧
    prune_schedule_density target T t hT ≤ 1 := by
  constructor
  · -- Lower bound: density >= target
    simp only [prune_schedule_density]
    have hT_pos : (0 : ℚ) < T := by exact_mod_cast hT
    have h2 : 0 ≤ 1 - target := by linarith
    have h3 : (1 - target) * (↑t : ℚ) / ↑T ≤ 1 - target := by
      rw [div_le_iff₀ hT_pos]
      calc (1 - target) * ↑t ≤ (1 - target) * ↑T :=
            mul_le_mul_of_nonneg_left (by exact_mod_cast ht) h2
        _ = (1 - target) * ↑T := rfl
    linarith
  · -- Upper bound: density <= 1
    simp only [prune_schedule_density]
    have hT_pos : (0 : ℚ) < T := by exact_mod_cast hT
    have : 0 ≤ (1 - target) * (↑t : ℚ) / ↑T := by
      apply div_nonneg
      · exact mul_nonneg (by linarith) (Nat.cast_nonneg t)
      · exact hT_pos.le
    linarith

/-! ## 10. Concrete Examples

Numerical validation of the sparsity model on real workloads. -/

/-- 2:4 sparsity on 1M-element tensor: 500K nonzeros.
    C++: NVIDIA A100 sparse tensor core format. -/
example : sparse_flops 1000000 2 4 = 500000 := by native_decide

/-- Block sparsity: 64 blocks, prune 16 -> 48 surviving -> 1.33x speedup.
    speedup = 64/48 = 4/3. -/
example : block_sparse_speedup 64 16 (by omega) (by omega) = 4 / 3 := by
  simp [block_sparse_speedup]; norm_num

/-- Low-rank: d=4096, r=512 -> cost 2*4096*512 = 4194304 vs 4096^2 = 16777216.
    4x cheaper. -/
example : sparse_low_rank_cost 4096 512 = 4194304 := by native_decide
example : sparse_full_rank_cost 4096 = 16777216 := by native_decide
example : (16777216 : ℚ) / 4194304 = 4 := by norm_num

/-- Block diagonal: d=1024, k=4 -> cost 1024^2/4 = 262144 vs 1048576.
    4x cheaper. -/
example : sparse_block_diag_cost 1024 4 = 262144 := by native_decide
example : sparse_full_rank_cost 1024 = 1048576 := by native_decide

/-- Gradual pruning: target 0.5 density over 100 steps.
    At step 50: density = 1 - 0.5 * 50/100 = 0.75. -/
example : prune_schedule_density (1/2) 100 50 (by omega) = 3/4 := by
  simp [prune_schedule_density]; norm_num

/-- Pruning error: remove weights summing to 1 out of total 10 -> 10% error. -/
example : prune_error_ratio 10 1 (by norm_num) = 1 / 10 := by
  simp [prune_error_ratio]

/-- N:M density chain: 1:4 -> density 0.25, speedup 4x. -/
example : sparse_nm_density 1 4 (by omega) = 1 / 4 := by
  simp [sparse_nm_density]
example : sparse_nm_speedup 1 4 (by omega) = 4 := by
  simp [sparse_nm_speedup]

/-! ## Summary

Key results:

**Sparsity Patterns:**
- `sparse_pattern_exhaustive`: every pattern is classified

**N:M Structured Sparsity:**
- `sparse_nm_density_le_one`: density in [0, 1]
- `sparse_nm_density_pos`: positive when n > 0
- `sparse_24_density`: 2:4 density = 1/2
- `sparse_24_speedup`: 2:4 speedup = 2x
- `sparse_nm_speedup_ge_one`: sparsity never slows down
- `sparse_nm_speedup_density_inv`: speedup * density = 1

**Pruning Masks:**
- `sparse_mask_count_le_length`: nonzeros <= total
- `sparse_mask_density_le_one`: density <= 1
- `sparse_mask_all_true_density`: no pruning -> density = 1
- `sparse_mask_all_false_density`: full pruning -> density = 0

**Block Sparsity:**
- `block_sparse_memory_le_dense`: sparse memory <= dense memory
- `block_sparse_speedup_ge_one`: pruning never slows down
- `block_sparse_speedup_monotone`: more pruning -> more speedup

**Sparse Cost Model:**
- `sparse_flops_le_dense`: sparse FLOPs <= dense FLOPs
- `sparse_50_half_flops`: 50% density halves FLOPs
- `sparse_speedup_ge_one`: speedup >= 1 when density <= 1

**Pruning Error:**
- `prune_error_nonneg`: error >= 0
- `prune_error_le_one`: error <= 1
- `prune_error_monotone`: more pruning -> more error
- `prune_smallest_minimizes_error`: magnitude-based pruning is optimal

**Layer-wise Sensitivity:**
- `prune_layer_error_density_monotone`: higher density -> less error
- `prune_layer_error_sensitivity_monotone`: higher sensitivity -> more error
- `prune_uniform_suboptimal`: uniform density is suboptimal vs per-layer

**Matrix Structure:**
- `sparse_low_rank_cheaper`: low-rank cheaper when 2r < d
- `sparse_block_diag_cheaper`: block-diagonal cheaper when k > 1
- `sparse_structure_detection_complete`: classification is exhaustive

**Gradual Pruning:**
- `prune_schedule_starts_dense`: starts at density = 1
- `prune_schedule_reaches_target`: reaches target at final step
- `prune_schedule_monotone`: density decreases monotonically
- `prune_schedule_bounded`: density stays in [target, 1]
-/

end Crucible
