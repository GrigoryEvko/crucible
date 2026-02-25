import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.Curriculum -- L11 Data Pipeline, Curriculum Learning & Backpressure

From L11 Data (MANIFESTO.md):

  "Backpressure: measure GPU idle between iterations -> signal DataLoader to
   prefetch more/less."

  "GPU-side augmentation: tensor ops (crop, flip, jitter, blur) moved to GPU
   as compiled DAG ops. ~500us CPU -> ~5us GPU."

  "Curriculum integration: L10 measures difficulty -> L11 reorders data stream."

  "per-sample loss observable -> order by difficulty. Try random/hard-first/
   easy->hard for 100 steps each -> keep best -> re-evaluate.
   20-40% faster convergence."

  "Manifold Mixup: interpolate hidden states at layer K:
   h_mix = alpha * h_A + (1-alpha) * h_B -> forward remainder -> loss against
   interpolated label."

  "KL divergence between current activations and training reference
   -> trigger continuous learning (L13) or alert."

This file formalizes:

1. **Curriculum orders**: Random, EasyToHard, HardToEasy, AntiCurriculum
2. **Difficulty sorting**: mergeSort-based ordering, permutation, sortedness
3. **Sample weighting**: weighted sums, uniform/zero-weight properties
4. **Backpressure**: prefetch depth adaptation from GPU idle fraction
5. **GPU-side augmentation**: CPU vs GPU cost model, 100x speedup
6. **Distribution shift detection**: L1 distance, self-zero, symmetry
7. **Manifold Mixup**: cost model, data doubling
8. **Pipeline throughput**: bottleneck = minimum stage throughput

All quantities use Nat (costs, counts, difficulties) or Q (fractions).
Zero sorry.

C++ correspondence:
- `Augur::curriculum_analysis()` -- per-sample loss ordering
- `Augur::backpressure_signal()` -- GPU idle measurement -> prefetch depth
- `Augur::distribution_shift()` -- KL divergence monitoring
- `BackgroundThread::compile_augmentation()` -- GPU-side augmentation kernels
- `Augur::manifold_mixup_layer()` -- optimal layer selection for mixup
- `DataLoader::pipeline_throughput()` -- bottleneck stage detection
-/

namespace Crucible

/-! ## 1. Curriculum Orders

From L11: "per-sample loss observable -> order by difficulty. Try
random/hard-first/easy->hard for 100 steps each -> keep best -> re-evaluate."

Four orderings, each a DAG BranchNode. Crucible benchmarks all four then
commits the best via atomic swap.

C++: `enum class CurriculumOrder : uint8_t { Random, EasyToHard, HardToEasy,
AntiCurriculum };` -/

/-- Curriculum ordering strategy.
    C++: `Augur::select_curriculum()` tries each for 100 steps. -/
inductive CurriculumOrder where
  | Random          -- no ordering, uniform sampling
  | EasyToHard      -- increasing difficulty (classic curriculum)
  | HardToEasy      -- decreasing difficulty
  | AntiCurriculum  -- hardest first (anti-curriculum)
  deriving Repr, DecidableEq

/-- All four curriculum orders are covered -- no fifth option exists.
    C++: exhaustive switch on CurriculumOrder, std::unreachable() default. -/
theorem curriculum_orders_exhaustive (o : CurriculumOrder) :
    o = .Random ∨ o = .EasyToHard ∨ o = .HardToEasy ∨ o = .AntiCurriculum := by
  cases o <;> simp

/-- Whether a curriculum order produces sorted output.
    Random and AntiCurriculum do not guarantee monotone difficulty. -/
def curriculum_is_sorted (o : CurriculumOrder) : Bool :=
  match o with
  | .EasyToHard => true
  | .HardToEasy => true
  | _ => false

/-- The two sorted orders are exactly EasyToHard and HardToEasy.
    C++: only these two feed into the sorted-merge kernel path. -/
theorem curriculum_sorted_iff (o : CurriculumOrder) :
    curriculum_is_sorted o = true ↔ (o = .EasyToHard ∨ o = .HardToEasy) := by
  cases o <;> simp [curriculum_is_sorted]

/-! ## 2. Difficulty Sorting

Samples have difficulty (Nat, e.g. loss * 1000 for integer arithmetic).
Sorting by difficulty for EasyToHard curriculum.

C++: `std::sort(samples.begin(), samples.end(), by_difficulty)`. -/

/-- Sort samples by difficulty (ascending = easy to hard).
    Uses List.mergeSort which is stable and O(n log n).
    C++: `Augur::sort_by_difficulty(sample_losses)`. -/
def curriculum_sort_by_difficulty (samples : List Nat) : List Nat :=
  samples.mergeSort (· ≤ ·)

/-- Sorting preserves length -- no samples lost or duplicated.
    C++: `assert(sorted.size() == original.size())`. -/
theorem curriculum_sorted_length (samples : List Nat) :
    (curriculum_sort_by_difficulty samples).length = samples.length := by
  simp [curriculum_sort_by_difficulty]

/-- Sorted result is pairwise ≤ (ascending order).
    C++: `assert(std::is_sorted(sorted.begin(), sorted.end()))`. -/
theorem curriculum_sorted_pairwise (samples : List Nat) :
    (curriculum_sort_by_difficulty samples).Pairwise (· ≤ ·) := by
  unfold curriculum_sort_by_difficulty
  exact List.pairwise_mergeSort' (· ≤ ·) samples

/-- Sorting is a permutation -- same multiset of difficulties.
    C++: no sample lost, no sample duplicated. -/
theorem curriculum_sorted_perm (samples : List Nat) :
    (curriculum_sort_by_difficulty samples).Perm samples := by
  exact List.mergeSort_perm samples (· ≤ ·)

/-- Easy-to-hard difficulty progression: for any indices i < j in the sorted
    output, the earlier element is ≤ the later.
    Direct consequence of pairwise sortedness.
    C++: `assert(sorted[i] <= sorted[i+1])` for all i. -/
theorem curriculum_easy_hard_progression (samples : List Nat)
    (i j : Nat) (hij : i < j)
    (hj : j < (curriculum_sort_by_difficulty samples).length) :
    (curriculum_sort_by_difficulty samples)[i]'(by omega) ≤
    (curriculum_sort_by_difficulty samples)[j]'hj := by
  have hp := curriculum_sorted_pairwise samples
  rw [List.pairwise_iff_getElem] at hp
  exact hp i j (by omega) hj hij

/-- Reverse sort for HardToEasy curriculum.
    C++: `std::sort(samples.begin(), samples.end(), std::greater<>())`. -/
def curriculum_sort_hard_to_easy (samples : List Nat) : List Nat :=
  (curriculum_sort_by_difficulty samples).reverse

/-- Reverse sort preserves length.
    C++: `assert(sorted_desc.size() == original.size())`. -/
theorem curriculum_hard_to_easy_length (samples : List Nat) :
    (curriculum_sort_hard_to_easy samples).length = samples.length := by
  simp [curriculum_sort_hard_to_easy, curriculum_sorted_length]

/-! ## 3. Sample Weighting

Weight samples by importance. Inverse frequency weighting, curriculum
weighting, loss-proportional weighting all reduce to weighted sum.

C++: `Augur::weighted_sample_loss(weights, losses)`. -/

/-- Weighted sum of sample values. Each sample contributes weight * value.
    C++: `std::inner_product(w.begin(), w.end(), v.begin(), 0ULL)`. -/
def curriculum_weighted_sum (weights values : List Nat) : Nat :=
  (weights.zip values).foldl (fun acc (w, v) => acc + w * v) 0

/-- Empty weights/values produce zero sum.
    C++: empty batch has zero contribution. -/
theorem curriculum_weighted_sum_nil_left (values : List Nat) :
    curriculum_weighted_sum [] values = 0 := by
  simp [curriculum_weighted_sum]

/-- Empty values produce zero sum.
    C++: no samples means zero loss. -/
theorem curriculum_weighted_sum_nil_right (weights : List Nat) :
    curriculum_weighted_sum weights [] = 0 := by
  simp [curriculum_weighted_sum, List.zip_nil_right]

/-- Single element: weighted sum = w * v.
    C++: single-sample batch. -/
theorem curriculum_weighted_sum_singleton (w v : Nat) :
    curriculum_weighted_sum [w] [v] = w * v := by
  simp [curriculum_weighted_sum, List.zip, List.foldl]

/-- Zero-weight sample contributes nothing.
    C++: masked-out samples excluded from gradient. -/
theorem curriculum_zero_weight_excluded (v : Nat) (ws vs : List Nat) :
    curriculum_weighted_sum (0 :: ws) (v :: vs) =
    curriculum_weighted_sum ws vs := by
  simp [curriculum_weighted_sum]

/-- Uniform weight-1 on matching-length lists: weighted sum = plain sum.
    C++: when all weights = 1, reduces to `std::accumulate`. -/
theorem curriculum_uniform_weights (values : List Nat) :
    curriculum_weighted_sum (List.replicate values.length 1) values =
    values.foldl (· + ·) 0 := by
  suffices gen : ∀ (acc : Nat) (vs : List Nat),
      (List.zip (List.replicate vs.length 1) vs).foldl
        (fun a (p : Nat × Nat) => a + p.1 * p.2) acc =
      vs.foldl (fun x y => x + y) acc by
    simp only [curriculum_weighted_sum]
    exact gen 0 values
  intro acc vs
  induction vs generalizing acc with
  | nil => simp
  | cons v vs ih =>
    simp only [List.length_cons, List.replicate_succ, List.zip_cons_cons,
               List.foldl_cons, one_mul]
    exact ih (acc + v)

/-! ## 4. Backpressure Model

From L11: "measure GPU idle between iterations -> signal DataLoader
to prefetch more/less."

Backpressure adapts prefetch depth: GPU idle -> prefetch more.
GPU busy -> prefetch less (save memory).

C++: `DataLoader::adjust_prefetch(gpu_idle_frac)`. -/

/-- Backpressure state tracking prefetch depth and GPU idle fraction.
    C++: `struct BackpressureState { uint32_t prefetch_depth; float gpu_idle; ... };` -/
structure BackpressureState where
  prefetch_depth : Nat    -- current prefetch queue depth
  min_depth : Nat         -- minimum allowed depth
  max_depth : Nat         -- maximum allowed depth
  depth_ge_min : min_depth ≤ prefetch_depth
  depth_le_max : prefetch_depth ≤ max_depth
  min_le_max : min_depth ≤ max_depth
  deriving Repr

/-- Increase prefetch depth by 1, clamped to max.
    C++: `state.prefetch_depth = std::min(state.prefetch_depth + 1, state.max_depth)`. -/
def backpressure_increase (s : BackpressureState) : BackpressureState where
  prefetch_depth := min (s.prefetch_depth + 1) s.max_depth
  min_depth := s.min_depth
  max_depth := s.max_depth
  depth_ge_min := by have := s.depth_ge_min; have := s.depth_le_max; omega
  depth_le_max := by omega
  min_le_max := s.min_le_max

/-- Decrease prefetch depth by 1, clamped to min.
    C++: `state.prefetch_depth = std::max(state.prefetch_depth - 1, state.min_depth)`. -/
def backpressure_decrease (s : BackpressureState) : BackpressureState where
  prefetch_depth := max (s.prefetch_depth - 1) s.min_depth
  min_depth := s.min_depth
  max_depth := s.max_depth
  depth_ge_min := by omega
  depth_le_max := by have := s.depth_ge_min; have := s.depth_le_max; omega
  min_le_max := s.min_le_max

/-- Increasing preserves bounds: new depth in [min, max].
    C++: `assert(min <= depth && depth <= max)` after adjust. -/
theorem backpressure_increase_bounded (s : BackpressureState) :
    (backpressure_increase s).min_depth ≤ (backpressure_increase s).prefetch_depth ∧
    (backpressure_increase s).prefetch_depth ≤ (backpressure_increase s).max_depth :=
  ⟨(backpressure_increase s).depth_ge_min, (backpressure_increase s).depth_le_max⟩

/-- Decreasing preserves bounds: new depth in [min, max].
    C++: `assert(min <= depth && depth <= max)` after adjust. -/
theorem backpressure_decrease_bounded (s : BackpressureState) :
    (backpressure_decrease s).min_depth ≤ (backpressure_decrease s).prefetch_depth ∧
    (backpressure_decrease s).prefetch_depth ≤ (backpressure_decrease s).max_depth :=
  ⟨(backpressure_decrease s).depth_ge_min, (backpressure_decrease s).depth_le_max⟩

/-- Increasing never decreases depth.
    C++: `assert(new_depth >= old_depth)` when GPU was idle. -/
theorem backpressure_increase_monotone (s : BackpressureState) :
    s.prefetch_depth ≤ (backpressure_increase s).prefetch_depth := by
  show s.prefetch_depth ≤ min (s.prefetch_depth + 1) s.max_depth
  have := s.depth_le_max; omega

/-- Decreasing never increases depth.
    C++: `assert(new_depth <= old_depth)` when GPU was busy. -/
theorem backpressure_decrease_monotone (s : BackpressureState) :
    (backpressure_decrease s).prefetch_depth ≤ s.prefetch_depth := by
  show max (s.prefetch_depth - 1) s.min_depth ≤ s.prefetch_depth
  have := s.depth_ge_min; omega

/-- At max depth, increasing is idempotent (no change).
    C++: already saturated, no-op. -/
theorem backpressure_increase_at_max (s : BackpressureState)
    (h : s.prefetch_depth = s.max_depth) :
    (backpressure_increase s).prefetch_depth = s.max_depth := by
  show min (s.prefetch_depth + 1) s.max_depth = s.max_depth
  omega

/-- At min depth, decreasing is idempotent (no change).
    C++: already at minimum, no-op. -/
theorem backpressure_decrease_at_min (s : BackpressureState)
    (h : s.prefetch_depth = s.min_depth) :
    (backpressure_decrease s).prefetch_depth = s.min_depth := by
  show max (s.prefetch_depth - 1) s.min_depth = s.min_depth
  omega

/-- Adaptive step: increase if GPU idle fraction > threshold (in thousandths),
    decrease if below, hold if equal. Threshold and idle in per-mille (0-1000).
    C++: `Augur::backpressure_signal(gpu_idle_frac)`. -/
def backpressure_adapt (s : BackpressureState) (idle_permille threshold : Nat) :
    BackpressureState :=
  if idle_permille > threshold then backpressure_increase s
  else if idle_permille < threshold then backpressure_decrease s
  else s

/-- When idle = threshold, state is unchanged (equilibrium).
    C++: no prefetch adjustment when GPU utilization matches target. -/
theorem backpressure_stable_at_equilibrium (s : BackpressureState) (t : Nat) :
    backpressure_adapt s t t = s := by
  simp [backpressure_adapt]

/-- When idle > threshold, depth increases.
    C++: GPU starving for data, prefetch more. -/
theorem backpressure_increase_when_idle (s : BackpressureState)
    (idle threshold : Nat) (h : idle > threshold) :
    (backpressure_adapt s idle threshold).prefetch_depth ≥ s.prefetch_depth := by
  simp only [backpressure_adapt, h, ↓reduceIte]
  exact backpressure_increase_monotone s

/-- When idle < threshold, depth decreases.
    C++: GPU is busy, reduce prefetch to save memory. -/
theorem backpressure_decrease_when_busy (s : BackpressureState)
    (idle threshold : Nat) (h : idle < threshold) :
    (backpressure_adapt s idle threshold).prefetch_depth ≤ s.prefetch_depth := by
  simp only [backpressure_adapt]
  have hle : ¬(idle > threshold) := by omega
  simp only [hle, ↓reduceIte, h]
  exact backpressure_decrease_monotone s

/-! ## 5. GPU-Side Augmentation Cost Model

From L11: "tensor ops (crop, flip, jitter, blur) moved to GPU as compiled DAG
ops. ~500us CPU -> ~5us GPU."

Moving augmentation to GPU eliminates CPU->GPU transfer overhead and
leverages massive parallelism for data transforms.

C++: `BackgroundThread::compile_augmentation()` compiles crop/flip/jitter
into the DAG as CKernel ops. -/

/-- CPU augmentation cost: ~500us per op.
    C++: CPU-side augmentation measured by `Augur::augmentation_benchmark()`. -/
def augment_cpu_cost (ops : Nat) : Nat := ops * 500

/-- GPU augmentation cost: ~5us per op.
    C++: GPU-side augmentation compiled as CKernel ops. -/
def augment_gpu_cost (ops : Nat) : Nat := ops * 5

/-- Speedup factor from moving augmentation to GPU.
    500 / 5 = 100x per op. Division on Nat; proved separately.
    C++: `Augur::augmentation_speedup()` reports this to dashboard. -/
def augment_speedup (ops : Nat) : Nat :=
  augment_cpu_cost ops / augment_gpu_cost ops

/-- GPU augmentation is strictly cheaper than CPU when ops > 0.
    C++: `assert(gpu_cost < cpu_cost)` after migration. -/
theorem augment_gpu_faster (ops : Nat) (h : 0 < ops) :
    augment_gpu_cost ops < augment_cpu_cost ops := by
  unfold augment_gpu_cost augment_cpu_cost; omega

/-- The speedup is exactly 100x (for ops > 0).
    500 * ops / (5 * ops) = 100. Key identity: a*b/(c*b) = a/c when c | a.
    C++: 500us / 5us = 100x, logged as `augmentation_speedup: 100`. -/
theorem augment_100x_speedup (ops : Nat) (h : 0 < ops) :
    augment_speedup ops = 100 := by
  unfold augment_speedup augment_cpu_cost augment_gpu_cost
  rw [show ops * 500 = 100 * (ops * 5) from by ring]
  rw [Nat.mul_div_cancel _ (by omega : 0 < ops * 5)]

/-- Time saved by moving to GPU = cpu_cost - gpu_cost.
    C++: `Augur::augmentation_savings()`. -/
def augment_savings (ops : Nat) : Nat :=
  augment_cpu_cost ops - augment_gpu_cost ops

/-- Savings are 99% of CPU cost (495 out of 500 per op).
    C++: near-total elimination of augmentation overhead. -/
theorem augment_savings_value (ops : Nat) :
    augment_savings ops = ops * 495 := by
  unfold augment_savings augment_cpu_cost augment_gpu_cost; omega

/-- Savings scale linearly with op count.
    C++: benefit proportional to augmentation pipeline length. -/
theorem augment_savings_linear (a b : Nat) :
    augment_savings (a + b) = augment_savings a + augment_savings b := by
  simp [augment_savings_value]; ring

/-- Zero ops cost zero on both CPU and GPU.
    C++: no augmentation -> no cost. -/
theorem augment_zero_cost : augment_cpu_cost 0 = 0 ∧ augment_gpu_cost 0 = 0 := by
  constructor <;> rfl

/-! ## 6. Distribution Shift Detection

From L11: "KL divergence between current activations and training reference
-> trigger continuous learning (L13) or alert."

We model shift detection using L1 distance (total variation) as a discrete
proxy for KL divergence. Distributions represented as histograms (List Nat).

C++: `Augur::distribution_shift(current_hist, reference_hist)`. -/

/-- L1 distance between two discrete histograms (bin-wise absolute difference).
    Proxy for KL divergence: KL(p||q) >= (1/2) * TV(p,q)^2 (Pinsker).
    C++: `Augur::l1_distance(hist_a, hist_b)`. -/
def shift_l1_distance (p q : List Nat) : Nat :=
  (p.zip q).foldl (fun acc (a, b) => acc + (if a ≥ b then a - b else b - a)) 0

/-- Self-distance is zero: no shift from reference to itself.
    C++: `assert(l1_distance(ref, ref) == 0)` at startup. -/
theorem shift_self_zero (p : List Nat) : shift_l1_distance p p = 0 := by
  unfold shift_l1_distance
  induction p with
  | nil => simp
  | cons x xs ih =>
    simp only [List.zip_cons_cons, List.foldl_cons,
               le_refl, ↓reduceIte, Nat.sub_self, Nat.add_zero]
    exact ih

/-- L1 distance is symmetric: shift(p, q) = shift(q, p).
    C++: shift detection is symmetric w.r.t. reference direction. -/
theorem shift_symmetric (p q : List Nat) :
    shift_l1_distance p q = shift_l1_distance q p := by
  unfold shift_l1_distance
  -- Show zip-foldl is invariant under swapping each pair
  suffices h : ∀ (xs : List (Nat × Nat)) (a : Nat),
      xs.foldl (fun acc (ab : Nat × Nat) => acc + (if ab.1 ≥ ab.2 then ab.1 - ab.2 else ab.2 - ab.1)) a =
      (xs.map (fun ab => (ab.2, ab.1))).foldl (fun acc (ab : Nat × Nat) => acc + (if ab.1 ≥ ab.2 then ab.1 - ab.2 else ab.2 - ab.1)) a by
    have zip_swap : (p.zip q).map (fun ab => (ab.2, ab.1)) = q.zip p := by
      induction p generalizing q with
      | nil => simp
      | cons x xs ihx =>
        cases q with
        | nil => simp
        | cons y ys => simp [ihx]
    rw [h, zip_swap]
  intro xs a
  induction xs generalizing a with
  | nil => rfl
  | cons hd tl ih =>
    simp only [List.foldl_cons, List.map_cons]
    rw [ih]
    congr 1
    -- |hd.1 - hd.2| = |hd.2 - hd.1| via case split on ≥
    simp only [ge_iff_le]
    split <;> split <;> omega

/-- Empty histograms have zero distance.
    C++: degenerate case, no bins to compare. -/
theorem shift_empty_zero : shift_l1_distance [] [] = 0 := by
  rfl

/-- Distance against empty is zero (zip truncates).
    C++: mismatched histogram lengths -> compare common prefix. -/
theorem shift_nil_left (q : List Nat) : shift_l1_distance [] q = 0 := by
  rfl

/-- Shift detection threshold: shift above threshold triggers adaptation.
    C++: `if (l1_distance > threshold) trigger_continuous_learning()`. -/
def shift_exceeds_threshold (p q : List Nat) (threshold : Nat) : Bool :=
  shift_l1_distance p q > threshold

/-- Identical distributions never exceed any threshold.
    C++: no false alarms from self-comparison. -/
theorem shift_no_false_alarm (p : List Nat) (t : Nat) :
    shift_exceeds_threshold p p t = false := by
  simp [shift_exceeds_threshold, shift_self_zero]

/-! ## 7. Manifold Mixup Cost Model

From L11: "interpolate hidden states at layer K:
h_mix = alpha * h_A + (1-alpha) * h_B -> forward remainder -> loss
against interpolated label. Layer K chosen by linear probe accuracy."

Mixup is cheap: one elementwise interpolation (add + scale) per sample.
The benefit is regularization and data augmentation in representation space.

C++: `Augur::manifold_mixup_layer()` selects optimal layer K. -/

/-- Cost of mixup at one layer: one addition per element.
    C++: alpha * h_A + (1-alpha) * h_B = one fused multiply-add per element. -/
def data_mixup_cost (base_cost : Nat) : Nat := base_cost + 1

/-- Mixup overhead is exactly 1 unit above base cost.
    C++: negligible overhead -- one fused kernel. -/
theorem data_mixup_negligible_overhead (base_cost : Nat) :
    data_mixup_cost base_cost - base_cost ≤ 1 := by
  unfold data_mixup_cost; omega

/-- Mixup cost is always at least base cost.
    C++: mixup never makes things cheaper than no-mixup. -/
theorem data_mixup_ge_base (base_cost : Nat) :
    base_cost ≤ data_mixup_cost base_cost := by
  unfold data_mixup_cost; omega

/-- Mixup produces 1 new mixed sample per 2 input samples.
    C++: each pair (h_A, h_B) -> one h_mix. -/
def data_mixup_output_count (input_pairs : Nat) : Nat := input_pairs

/-- Output count equals input pair count: n mixup outputs + n originals = 2n.
    C++: 1:1 mapping from pairs to mixed samples. -/
theorem data_mixup_doubles_data (n : Nat) :
    data_mixup_output_count n + n = 2 * n := by
  unfold data_mixup_output_count; omega

/-- Mixup from zero pairs produces zero outputs.
    C++: empty batch -> no mixup. -/
theorem data_mixup_zero : data_mixup_output_count 0 = 0 := rfl

/-! ## 8. Pipeline Throughput Model

Total pipeline throughput is bounded by the slowest (bottleneck) stage.
Pipeline stages: read -> decode -> augment -> transfer -> compute.

C++: `DataLoader::pipeline_throughput()` measures per-stage rates. -/

/-- Minimum of a non-empty list of stage throughputs.
    The pipeline cannot exceed any individual stage's throughput.
    C++: `Augur::pipeline_bottleneck(stage_throughputs)`. -/
def data_pipeline_throughput : List Nat → Nat
  | [] => 0
  | [x] => x
  | x :: xs => min x (data_pipeline_throughput xs)

/-- Pipeline throughput of a singleton is just that stage.
    C++: single-stage pipeline = that stage. -/
theorem data_pipeline_singleton (x : Nat) :
    data_pipeline_throughput [x] = x := rfl

/-- Pipeline throughput of cons is min of head and tail pipeline.
    Helper for downstream proofs. -/
private theorem data_pipeline_cons_cons (x y : Nat) (ys : List Nat) :
    data_pipeline_throughput (x :: y :: ys) =
    min x (data_pipeline_throughput (y :: ys)) := rfl

/-- Pipeline throughput is at most the first stage.
    C++: can't exceed read throughput. -/
theorem data_pipeline_le_head (x : Nat) (xs : List Nat) :
    data_pipeline_throughput (x :: xs) ≤ x := by
  cases xs with
  | nil => exact le_refl _
  | cons y ys => exact Nat.min_le_left _ _

/-- Pipeline throughput is at most any individual stage.
    C++: bottleneck dominates. This is the key invariant. -/
theorem data_pipeline_le_any_stage (stages : List Nat) (i : Nat) (h : i < stages.length) :
    data_pipeline_throughput stages ≤ stages[i] := by
  induction stages generalizing i with
  | nil => simp at h
  | cons x xs ih =>
    cases i with
    | zero => exact data_pipeline_le_head x xs
    | succ j =>
      simp only [List.length_cons] at h
      simp only [List.getElem_cons_succ]
      have ihj := ih j (by omega)
      cases xs with
      | nil => simp at h
      | cons y ys =>
        exact le_trans (Nat.min_le_right _ _) ihj

/-- Adding a faster stage doesn't change the bottleneck.
    C++: adding a stage faster than current bottleneck is free.
    Requires at least one existing stage. -/
theorem data_pipeline_add_fast_stage (x : Nat) (xs : List Nat) (s : Nat)
    (hs : data_pipeline_throughput (x :: xs) ≤ s) :
    data_pipeline_throughput (s :: x :: xs) = data_pipeline_throughput (x :: xs) := by
  simp only [data_pipeline_cons_cons]; omega

/-- Adding a slower stage becomes the new bottleneck.
    C++: new slow stage dominates pipeline throughput. -/
theorem data_pipeline_add_slow_stage (x : Nat) (xs : List Nat) (s : Nat)
    (hs : s ≤ data_pipeline_throughput (x :: xs)) :
    data_pipeline_throughput (s :: x :: xs) = s := by
  simp only [data_pipeline_cons_cons]; omega

/-- Improving the first stage can only improve the pipeline.
    C++: optimization targets the bottleneck stage. -/
theorem data_pipeline_improve_bottleneck (s s' : Nat) (rest : List Nat)
    (hss : s ≤ s') :
    data_pipeline_throughput (s :: rest) ≤ data_pipeline_throughput (s' :: rest) := by
  cases rest with
  | nil => exact hss
  | cons y ys => simp only [data_pipeline_cons_cons]; omega

/-! ## 9. Curriculum Phase Selection

From L11: "Try random/hard-first/easy->hard for 100 steps each -> keep best
-> re-evaluate." Crucible tries each ordering for a trial period, measures
convergence rate, and commits the best.

C++: `Augur::curriculum_search(orders, trial_steps)`. -/

/-- Trial result: ordering paired with measured loss after trial_steps.
    C++: `struct TrialResult { CurriculumOrder order; float final_loss; };` -/
structure CurriculumTrialResult where
  order : CurriculumOrder
  loss : Nat  -- loss * 1000 for integer arithmetic

/-- Select the best ordering from trial results (lowest loss).
    C++: `std::min_element(results.begin(), results.end(), by_loss)`. -/
def curriculum_select_best : List CurriculumTrialResult → Option CurriculumTrialResult
  | [] => none
  | [r] => some r
  | r :: rs =>
    match curriculum_select_best rs with
    | none => some r
    | some best => if r.loss ≤ best.loss then some r else some best

/-- Selection from non-empty list always succeeds.
    C++: `assert(best.has_value())` after search. -/
theorem curriculum_select_best_some (r : CurriculumTrialResult)
    (rs : List CurriculumTrialResult) :
    (curriculum_select_best (r :: rs)).isSome = true := by
  induction rs generalizing r with
  | nil => rfl
  | cons s ss ih =>
    unfold curriculum_select_best
    have := ih s
    split
    · rfl
    · split <;> rfl

/-! ## 10. Data Batch Size Adaptation

From L11: combined with backpressure -- adjust batch size based on memory
pressure and GPU utilization.

C++: `Meridian::optimal_batch_size(gpu_memory, model_size)`. -/

/-- Available memory for batch data = total - model - reserved.
    C++: `gpu_memory - model_footprint - reserved_bytes`. -/
def data_available_memory (total model reserved : Nat) : Nat :=
  total - model - reserved

/-- Maximum batch size that fits in available memory.
    Each sample requires `sample_bytes` bytes.
    C++: `available / sample_bytes`. -/
def data_max_batch_size (available sample_bytes : Nat) (_ : 0 < sample_bytes) : Nat :=
  available / sample_bytes

/-- Batch size fits in memory: batch * sample <= available.
    C++: `assert(batch_size * sample_bytes <= available)`. -/
theorem data_batch_fits (available sample_bytes : Nat) (h : 0 < sample_bytes) :
    data_max_batch_size available sample_bytes h * sample_bytes ≤ available :=
  Nat.div_mul_le_self available sample_bytes

/-- Larger memory allows larger batches.
    C++: more GPU memory -> can fit bigger batches. -/
theorem data_larger_memory_larger_batch (a1 a2 sb : Nat) (h : 0 < sb) (ha : a1 ≤ a2) :
    data_max_batch_size a1 sb h ≤ data_max_batch_size a2 sb h := by
  exact Nat.div_le_div_right ha

/-- Larger samples reduce batch size.
    C++: higher precision -> fewer samples per batch. -/
theorem data_larger_samples_smaller_batch (a s1 s2 : Nat) (h1 : 0 < s1) (h2 : 0 < s2)
    (hs : s1 ≤ s2) :
    data_max_batch_size a s2 h2 ≤ data_max_batch_size a s1 h1 := by
  exact Nat.div_le_div_left hs h1

/-! ## 11. Curriculum Difficulty Quantiles

Partition samples into difficulty quantiles for staged training.
Easy quartile first, then medium, then hard.

C++: `Augur::difficulty_quantiles(losses, num_buckets)`. -/

/-- Split a sorted list into a prefix of length n and the rest.
    C++: `std::partition_point` or `std::nth_element`. -/
def curriculum_split_at (samples : List Nat) (n : Nat) : List Nat × List Nat :=
  (samples.take n, samples.drop n)

/-- Split preserves total length.
    C++: no samples lost during quantile partitioning. -/
theorem curriculum_split_preserves_length (samples : List Nat) (n : Nat) :
    (curriculum_split_at samples n).1.length + (curriculum_split_at samples n).2.length =
    samples.length := by
  simp [curriculum_split_at, List.length_take, List.length_drop]; omega

/-- Elements in the easy (prefix) partition of a pairwise-sorted list are <=
    elements in the hard (suffix) partition.
    C++: quantile boundaries respected in sorted curriculum. -/
theorem curriculum_split_sorted_le (samples : List Nat)
    (hs : samples.Pairwise (· ≤ ·))
    (n : Nat) (hn : n ≤ samples.length)
    (i : Nat) (hi : i < (curriculum_split_at samples n).1.length)
    (j : Nat) (hj : j < (curriculum_split_at samples n).2.length) :
    (curriculum_split_at samples n).1[i] ≤ (curriculum_split_at samples n).2[j] := by
  unfold curriculum_split_at at hi hj ⊢
  simp only [List.getElem_take, List.getElem_drop]
  rw [List.pairwise_iff_getElem] at hs
  apply hs
  simp only [List.length_take, List.length_drop] at hi hj
  omega

end Crucible
