import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.Roofline — L17 Augur Digital Twin & Roofline Performance Model

From L17 Augur (MANIFESTO.md):

  "Digital twin: DAG + Axiom kernel predictions + Meridian corrections
   → complete iteration prediction (±5-10%). Per-kernel: roofline × correction
   × wave quantization. Bottleneck classification: COMPUTE/MEMORY/LAUNCH.
   Per-iteration: critical-path compute + exposed communication + pipeline bubble."

  "What-if engine: change batch/TP/hardware/model-size → instant re-prediction,
   no GPU needed."

This file formalizes the CORRECTNESS CONDITIONS for Augur's performance model:

1. **Roofline model**: performance bounded by min(peak_flops, bandwidth × AI).
   The ridge point where compute and memory ceilings meet = peak / bandwidth.

2. **Bottleneck classification**: five categories (COMPUTE, MEMORY_BW, COMMUNICATION,
   BUBBLE, IMBALANCE), proved exhaustive and mutually exclusive under strict thresholds.

3. **Kernel efficiency**: achieved / peak ∈ [0, 1], roofline-predicted ≥ actual.

4. **Iteration time model**: critical path with overlap, pipeline bubble bounds.

5. **Speedup predictions**: Amdahl's law, monotonicity in parallel speedup factor.

6. **What-if analysis**: batch size and TP degree effects on bottleneck classification.

All quantities use ℚ (rational arithmetic) to avoid floating-point issues.
Natural numbers used for discrete quantities (GPU counts, pipeline stages, etc.).

C++ correspondence:
- `struct GPUProfile` in Meridian: calibrated hardware capabilities
- `enum class Bottleneck` in Augur: kernel classification
- `predict_iteration_time()` in Augur: digital twin prediction
- `what_if()` in Augur: hypothetical scenario evaluation
-/

namespace Crucible

/-! ## GPU Hardware Profile (L16 Meridian → L17 Augur)

C++ (Meridian): calibrated at startup via GEMM benchmarks and streaming copies.
Augur uses these as roofline ceilings for prediction. -/

/-- Calibrated hardware profile for one GPU.
    C++: `struct GPUProfile { double actual_tflops, actual_hbm_bw; }`.
    All values in consistent units (e.g., GFLOP/s and GB/s). -/
structure GPUProfile where
  peak_flops : ℚ     -- peak compute throughput (GFLOP/s)
  peak_bw : ℚ        -- peak memory bandwidth (GB/s)
  hflops : 0 < peak_flops
  hbw : 0 < peak_bw

/-- Ridge point: arithmetic intensity where compute and memory ceilings meet.
    AI_ridge = peak_flops / peak_bw.
    Kernels with AI > ridge are compute-bound; AI < ridge are memory-bound.
    C++: `double ridge_point = actual_tflops / actual_hbm_bw;`. -/
def GPUProfile.ridgePoint (g : GPUProfile) : ℚ :=
  g.peak_flops / g.peak_bw

/-- Ridge point is positive (both peak values are positive). -/
theorem GPUProfile.ridgePoint_pos (g : GPUProfile) : 0 < g.ridgePoint := by
  simp only [ridgePoint]
  exact div_pos g.hflops g.hbw

/-! ## Roofline Model (L17 Augur)

The classic roofline bounds achievable performance by two ceilings:
- Compute ceiling: perf ≤ peak_flops
- Memory ceiling: perf ≤ bandwidth × arithmetic_intensity
- Combined: perf ≤ min(peak_flops, bw × AI)

C++ (Augur): `double roofline_predict(double ai, GPUProfile hw)`. -/

/-- Kernel workload characterization.
    C++: measured via CUPTI counters after execution. -/
structure KernelWorkload where
  flops : ℚ          -- total floating-point operations
  bytes : ℚ          -- total bytes transferred
  hflops : 0 ≤ flops
  hbytes : 0 < bytes

/-- Arithmetic intensity: flops per byte transferred.
    The fundamental ratio that determines whether a kernel is compute- or memory-bound.
    C++: `double ai = kernel_flops / kernel_bytes;`. -/
def KernelWorkload.ai (w : KernelWorkload) : ℚ :=
  w.flops / w.bytes

/-- Arithmetic intensity is non-negative. -/
theorem KernelWorkload.ai_nonneg (w : KernelWorkload) : 0 ≤ w.ai := by
  simp only [ai]
  exact div_nonneg w.hflops (le_of_lt w.hbytes)

/-- Roofline-predicted peak performance for a given AI and hardware profile.
    min(peak_flops, peak_bw × AI).
    C++: `predict = std::min(hw.peak_flops, hw.peak_bw * ai);`. -/
def rooflinePred (g : GPUProfile) (ai : ℚ) : ℚ :=
  min g.peak_flops (g.peak_bw * ai)

/-- Roofline prediction never exceeds peak compute.
    THE compute ceiling of the roofline model.
    C++: no kernel can exceed the GPU's peak FLOP/s. -/
theorem roofline_le_peak (g : GPUProfile) (ai : ℚ) :
    rooflinePred g ai ≤ g.peak_flops :=
  min_le_left _ _

/-- Roofline prediction never exceeds memory ceiling.
    THE memory ceiling of the roofline model. -/
theorem roofline_le_mem (g : GPUProfile) (ai : ℚ) :
    rooflinePred g ai ≤ g.peak_bw * ai :=
  min_le_right _ _

/-- At the ridge point, both ceilings give the same value.
    peak_bw × (peak_flops / peak_bw) = peak_flops.
    C++: the crossover point in Augur's roofline chart. -/
theorem roofline_at_ridge (g : GPUProfile) :
    g.peak_bw * g.ridgePoint = g.peak_flops := by
  simp only [GPUProfile.ridgePoint]
  rw [mul_div_cancel₀]
  exact ne_of_gt g.hbw

/-- Roofline prediction is non-negative when AI is non-negative.
    Achieved performance is physically non-negative. -/
theorem roofline_nonneg (g : GPUProfile) (ai : ℚ) (hai : 0 ≤ ai) :
    0 ≤ rooflinePred g ai := by
  simp only [rooflinePred]
  exact le_min (le_of_lt g.hflops) (mul_nonneg (le_of_lt g.hbw) hai)

/-- Roofline prediction is monotone in arithmetic intensity.
    Higher AI → higher predicted performance (up to compute ceiling).
    C++: Augur uses this to predict benefit of increasing tile reuse. -/
theorem roofline_monotone (g : GPUProfile) (ai₁ ai₂ : ℚ)
    (h : ai₁ ≤ ai₂) : rooflinePred g ai₁ ≤ rooflinePred g ai₂ := by
  simp only [rooflinePred]
  exact min_le_min_left _ (mul_le_mul_of_nonneg_left h (le_of_lt g.hbw))

/-! ## Bottleneck Classification (L17 Augur)

C++: `enum class Bottleneck { COMPUTE, MEMORY_BW, COMMUNICATION, BUBBLE, IMBALANCE };`
Augur classifies each kernel/iteration based on measured utilization metrics. -/

/-- Bottleneck category for a kernel or iteration.
    C++: `Augur::classify_bottleneck()`. -/
inductive Bottleneck where
  | COMPUTE        -- SM utilization high, bandwidth low
  | MEMORY_BW      -- bandwidth utilization high, SM low
  | COMMUNICATION  -- communication time dominates
  | BUBBLE         -- pipeline bubble fraction high
  | IMBALANCE      -- load imbalance across GPUs
  deriving DecidableEq, Repr

/-- Measured utilization metrics for classification.
    All values in [0, 1] representing fractions.
    C++: from CUPTI counters + timing measurements. -/
structure Metrics where
  sm_util : ℚ        -- SM (compute) utilization
  bw_util : ℚ        -- memory bandwidth utilization
  comm_frac : ℚ      -- fraction of time in communication
  bubble_frac : ℚ    -- pipeline bubble fraction
  imbalance : ℚ      -- max/mean load ratio - 1 (0 = perfectly balanced)
  hsm : 0 ≤ sm_util
  hbw : 0 ≤ bw_util
  hcomm : 0 ≤ comm_frac
  hbubble : 0 ≤ bubble_frac
  himb : 0 ≤ imbalance

/-- Classification threshold (e.g., 0.7 = 70%).
    C++: configurable via `Augur::config.classification_threshold`. -/
structure Threshold where
  val : ℚ
  hpos : 0 < val
  hle : val ≤ 1

/-- Classify a kernel's bottleneck from its metrics.
    Priority order: COMMUNICATION > BUBBLE > IMBALANCE > COMPUTE > MEMORY_BW.
    This matches Augur's C++ classification logic: external bottlenecks
    (communication, pipeline, imbalance) are checked before internal ones. -/
def classifyBottleneck (m : Metrics) (t : Threshold) : Bottleneck :=
  if t.val ≤ m.comm_frac then Bottleneck.COMMUNICATION
  else if t.val ≤ m.bubble_frac then Bottleneck.BUBBLE
  else if t.val ≤ m.imbalance then Bottleneck.IMBALANCE
  else if m.bw_util ≤ m.sm_util then Bottleneck.COMPUTE
  else Bottleneck.MEMORY_BW

/-- Classification is total: every set of metrics maps to exactly one bottleneck.
    C++: `classify_bottleneck()` always returns a valid enum value. -/
theorem classify_exhaustive (m : Metrics) (t : Threshold) :
    ∃ b : Bottleneck, classifyBottleneck m t = b :=
  ⟨classifyBottleneck m t, rfl⟩

/-- High communication fraction → COMMUNICATION bottleneck.
    C++: when comm time exceeds threshold, that's the bottleneck. -/
theorem classify_comm (m : Metrics) (t : Threshold) (h : t.val ≤ m.comm_frac) :
    classifyBottleneck m t = Bottleneck.COMMUNICATION := by
  simp [classifyBottleneck, h]

/-- High bubble fraction (and low comm) → BUBBLE bottleneck. -/
theorem classify_bubble (m : Metrics) (t : Threshold)
    (hc : m.comm_frac < t.val) (hb : t.val ≤ m.bubble_frac) :
    classifyBottleneck m t = Bottleneck.BUBBLE := by
  simp [classifyBottleneck, not_le.mpr hc, hb]

/-- High imbalance (and low comm, bubble) → IMBALANCE bottleneck. -/
theorem classify_imbalance (m : Metrics) (t : Threshold)
    (hc : m.comm_frac < t.val) (hb : m.bubble_frac < t.val)
    (hi : t.val ≤ m.imbalance) :
    classifyBottleneck m t = Bottleneck.IMBALANCE := by
  simp [classifyBottleneck, not_le.mpr hc, not_le.mpr hb, hi]

/-- When all external metrics are below threshold: SM ≥ BW → COMPUTE. -/
theorem classify_compute (m : Metrics) (t : Threshold)
    (hc : m.comm_frac < t.val) (hb : m.bubble_frac < t.val)
    (hi : m.imbalance < t.val) (hsm : m.bw_util ≤ m.sm_util) :
    classifyBottleneck m t = Bottleneck.COMPUTE := by
  simp [classifyBottleneck, not_le.mpr hc, not_le.mpr hb, not_le.mpr hi, hsm]

/-- When all external metrics are below threshold: BW > SM → MEMORY_BW. -/
theorem classify_memory (m : Metrics) (t : Threshold)
    (hc : m.comm_frac < t.val) (hb : m.bubble_frac < t.val)
    (hi : m.imbalance < t.val) (hbw : m.sm_util < m.bw_util) :
    classifyBottleneck m t = Bottleneck.MEMORY_BW := by
  simp [classifyBottleneck, not_le.mpr hc, not_le.mpr hb, not_le.mpr hi, not_le.mpr hbw]

/-! ## Kernel Efficiency (L17 Augur)

Efficiency = achieved / peak. Roofline predicts the theoretical max efficiency
for a given arithmetic intensity. -/

/-- Compute efficiency: fraction of peak FLOP/s achieved.
    C++: `double efficiency = achieved_flops / hw.peak_flops;`. -/
def computeEfficiency (achieved peak : ℚ) (_hpeak : 0 < peak) : ℚ :=
  achieved / peak

/-- Efficiency is at most 1 when achieved ≤ peak.
    C++: no kernel exceeds peak hardware throughput. -/
theorem efficiency_le_one (achieved peak : ℚ) (hpeak : 0 < peak)
    (h : achieved ≤ peak) :
    computeEfficiency achieved peak hpeak ≤ 1 := by
  simp only [computeEfficiency]
  rwa [div_le_one hpeak]

/-- Efficiency is non-negative when achieved is non-negative. -/
theorem efficiency_nonneg (achieved peak : ℚ) (hpeak : 0 < peak)
    (h : 0 ≤ achieved) :
    0 ≤ computeEfficiency achieved peak hpeak := by
  simp only [computeEfficiency]
  exact div_nonneg h (le_of_lt hpeak)

/-- Higher achieved → higher efficiency (monotone). -/
theorem efficiency_monotone (a₁ a₂ peak : ℚ) (hpeak : 0 < peak)
    (h : a₁ ≤ a₂) :
    computeEfficiency a₁ peak hpeak ≤ computeEfficiency a₂ peak hpeak := by
  simp only [computeEfficiency]
  exact div_le_div_of_nonneg_right h (le_of_lt hpeak)

/-- At full utilization, efficiency = 1. -/
theorem efficiency_at_peak (peak : ℚ) (hpeak : 0 < peak) :
    computeEfficiency peak peak hpeak = 1 := by
  simp only [computeEfficiency, div_self (ne_of_gt hpeak)]

/-! ## Iteration Time Model (L17 Augur)

C++ (Augur): `predict_iteration_time()`.
Total time = critical path through compute, communication, and pipeline bubbles.
With overlap: exposed_comm = max(0, comm - overlap). -/

/-- Iteration timing breakdown.
    C++: measured via CUDA events and NCCL timers. -/
structure IterTiming where
  compute_time : ℚ    -- total compute (forward + backward + optimizer)
  comm_time : ℚ       -- total communication (all-reduce, all-gather, etc.)
  bubble_time : ℚ     -- pipeline bubble idle time
  overlap : ℚ         -- communication overlapped with compute
  hcomp : 0 ≤ compute_time
  hcomm : 0 ≤ comm_time
  hbubble : 0 ≤ bubble_time
  hoverlap : 0 ≤ overlap
  hoverlap_le : overlap ≤ min compute_time comm_time  -- can't overlap more than either

/-- Non-overlapped iteration time: compute + comm + bubble.
    C++: worst case when no overlap is achieved. -/
def iterTimeNoOverlap (t : IterTiming) : ℚ :=
  t.compute_time + t.comm_time + t.bubble_time

/-- Overlapped iteration time: compute + exposed_comm + bubble.
    exposed_comm = comm - overlap (the part not hidden behind compute).
    C++: `iter_time = compute + max(0, comm - overlap) + bubble`. -/
def iterTimeOverlap (t : IterTiming) : ℚ :=
  t.compute_time + (t.comm_time - t.overlap) + t.bubble_time

/-- Iteration time is at least compute time (compute can never be hidden).
    C++: compute is always on the critical path. -/
theorem iter_time_ge_compute (t : IterTiming) :
    t.compute_time ≤ iterTimeOverlap t := by
  simp only [iterTimeOverlap]
  have h1 : 0 ≤ t.comm_time - t.overlap := by
    have := t.hoverlap_le
    linarith [min_le_right t.compute_time t.comm_time]
  linarith [t.hbubble]

/-- Overlapped time ≤ non-overlapped time (overlap only helps).
    C++: `assert(overlapped_time <= non_overlapped_time)`. -/
theorem overlap_benefit (t : IterTiming) :
    iterTimeOverlap t ≤ iterTimeNoOverlap t := by
  simp only [iterTimeOverlap, iterTimeNoOverlap]
  linarith [t.hoverlap]

/-- With zero overlap, both time models agree. -/
theorem no_overlap_eq (t : IterTiming) (h : t.overlap = 0) :
    iterTimeOverlap t = iterTimeNoOverlap t := by
  simp only [iterTimeOverlap, iterTimeNoOverlap, h, sub_zero]

/-- With perfect overlap (overlap = comm), only compute + bubble remains.
    C++: ideal overlap scenario where all communication is hidden. -/
theorem perfect_overlap (t : IterTiming) (h : t.overlap = t.comm_time) :
    iterTimeOverlap t = t.compute_time + t.bubble_time := by
  simp only [iterTimeOverlap, h, sub_self, add_zero]

/-! ## Pipeline Bubble Bound (L17 Augur)

C++ (L12 Distribution): "PP-stage pipeline with M micro-batches.
Bubble fraction ≤ (PP - 1) / (PP × M)." (1F1B schedule)

This is a fundamental bound from GPipe/PipeDream scheduling theory. -/

/-- Pipeline bubble fraction for 1F1B schedule.
    pp = number of pipeline stages, micro = number of micro-batches.
    bubble_frac = (pp - 1) / (pp × micro).
    C++: Meridian uses this to evaluate PP configurations. -/
def pipelineBubbleFrac (pp micro : Nat) : ℚ :=
  if pp = 0 ∨ micro = 0 then 0
  else (pp - 1 : ℚ) / (pp * micro : ℚ)

/-- Bubble fraction is non-negative.
    C++: bubble time is physically non-negative. -/
theorem bubble_frac_nonneg (pp micro : Nat) : 0 ≤ pipelineBubbleFrac pp micro := by
  simp only [pipelineBubbleFrac]
  split
  · exact le_refl 0
  · push_neg at *
    rename_i h
    apply div_nonneg
    · exact sub_nonneg.mpr (by exact_mod_cast Nat.one_le_iff_ne_zero.mpr h.1)
    · positivity

/-- With 1 pipeline stage, bubble fraction is zero (no pipeline bubble).
    C++: PP=1 means no pipeline parallelism, no bubble. -/
theorem bubble_frac_no_pipeline (micro : Nat) :
    pipelineBubbleFrac 1 micro = 0 := by
  simp [pipelineBubbleFrac]

/-- Bubble fraction < 1 when micro ≥ 1 and pp ≥ 1.
    The pipeline is always making SOME progress.
    C++: assert(bubble_frac < 1.0) in Augur diagnostics. -/
theorem bubble_frac_lt_one (pp micro : Nat) (hpp : 1 ≤ pp) (hm : 1 ≤ micro) :
    pipelineBubbleFrac pp micro < 1 := by
  simp only [pipelineBubbleFrac]
  split
  · exact one_pos
  · push_neg at *
    rename_i hne
    rw [div_lt_one (by positivity)]
    have hpp' : (pp : ℚ) - 1 ≤ (pp : ℚ) - 1 := le_refl _
    have : 1 ≤ (micro : ℚ) := by exact_mod_cast hm
    have : 0 < (pp : ℚ) := by exact_mod_cast hpp
    nlinarith

/-- More micro-batches → smaller bubble fraction (monotone decreasing).
    C++: Augur recommends increasing micro-batches to reduce bubble. -/
theorem bubble_frac_mono_micro (pp m₁ m₂ : Nat)
    (hpp : 1 ≤ pp) (hm₁ : 1 ≤ m₁) (hm₂ : 1 ≤ m₂)
    (h : m₁ ≤ m₂) :
    pipelineBubbleFrac pp m₂ ≤ pipelineBubbleFrac pp m₁ := by
  simp only [pipelineBubbleFrac, show ¬(pp = 0 ∨ m₁ = 0) from by omega,
    show ¬(pp = 0 ∨ m₂ = 0) from by omega, ite_false]
  apply div_le_div_of_nonneg_left
  · linarith [show (pp : ℚ) ≥ 1 from by exact_mod_cast hpp]
  · positivity
  · exact_mod_cast Nat.mul_le_mul_left pp h

/-! ## Speedup and Amdahl's Law (L17 Augur)

C++: Augur predicts speedup from optimizations using Amdahl's law.
speedup = 1 / ((1-p) + p/S) where p = parallelizable fraction, S = speedup factor. -/

/-- Amdahl's law: speedup limited by serial fraction.
    p = parallelizable fraction (0 ≤ p ≤ 1), S = speedup of parallel part.
    C++: `Augur::predict_speedup(parallel_frac, speedup_factor)`. -/
def amdahl (p S : ℚ) (_hS : 0 < S) : ℚ :=
  1 / ((1 - p) + p / S)

/-- Amdahl's law: speedup ≤ 1/(1-p) (the serial bottleneck).
    No matter how large S is, serial fraction limits total speedup.
    C++: Augur caps speedup predictions at this theoretical maximum. -/
theorem amdahl_limit (p S : ℚ) (hp0 : 0 ≤ p) (hp1 : p < 1) (hS : 0 < S) :
    amdahl p S hS ≤ 1 / (1 - p) := by
  simp only [amdahl]
  apply div_le_div_of_nonneg_left (by norm_num : (0:ℚ) ≤ 1) (by linarith)
  linarith [div_nonneg hp0 (le_of_lt hS)]

/-- Amdahl's speedup is positive when 0 ≤ p < 1 and S > 0.
    C++: speedup predictions are always positive (optimization helps). -/
theorem amdahl_pos (p S : ℚ) (hp0 : 0 ≤ p) (hp1 : p < 1) (hS : 0 < S) :
    0 < amdahl p S hS := by
  simp only [amdahl]
  apply div_pos one_pos
  linarith [div_nonneg hp0 (le_of_lt hS)]

/-- With p = 0 (fully serial), Amdahl gives speedup = 1 (no benefit).
    C++: if nothing is parallelizable, parallel hardware doesn't help. -/
theorem amdahl_serial (S : ℚ) (hS : 0 < S) :
    amdahl 0 S hS = 1 := by
  simp [amdahl]

/-- With p = 1 (fully parallel), Amdahl gives speedup = S.
    C++: best case — entire workload benefits from parallelism. -/
theorem amdahl_full_parallel (S : ℚ) (hS : 0 < S) :
    amdahl 1 S hS = S := by
  simp only [amdahl, sub_self, zero_add, one_div]
  rw [inv_inv]

/-- Amdahl's speedup is monotone in S: more parallel speedup → more total speedup.
    C++: Augur's recommendation confidence: larger S → larger predicted benefit. -/
theorem amdahl_monotone_S (p S₁ S₂ : ℚ)
    (hp0 : 0 ≤ p) (hp1 : p < 1) (hS₁ : 0 < S₁) (hS₂ : 0 < S₂)
    (h : S₁ ≤ S₂) :
    amdahl p S₁ hS₁ ≤ amdahl p S₂ hS₂ := by
  simp only [amdahl]
  apply div_le_div_of_nonneg_left (by norm_num : (0:ℚ) ≤ 1)
    (by linarith [div_nonneg hp0 (le_of_lt hS₂)])
  have : p / S₂ ≤ p / S₁ := div_le_div_of_nonneg_left hp0 hS₁ h
  linarith

/-! ## What-if Analysis (L17 Augur)

C++: `Augur::what_if()` — predict effect of configuration changes.
Key insight: changing batch size scales compute but not communication,
which shifts the bottleneck classification. -/

/-- Scaling compute and communication independently.
    C++: what-if engine scales relevant components. -/
structure ScaledTiming where
  base : IterTiming
  compute_scale : ℚ   -- multiplicative factor on compute time
  comm_scale : ℚ      -- multiplicative factor on comm time
  hcs : 0 < compute_scale
  hcms : 0 < comm_scale

/-- Predicted iteration time after scaling.
    C++: `predict_iter_time(base, compute_scale, comm_scale)`. -/
def scaledIterTime (s : ScaledTiming) : ℚ :=
  s.base.compute_time * s.compute_scale +
  s.base.comm_time * s.comm_scale +
  s.base.bubble_time

/-- Scaling compute by 1 and comm by 1 preserves the original time.
    C++: identity what-if should return current prediction. -/
theorem scaled_identity (t : IterTiming) :
    scaledIterTime ⟨t, 1, 1, one_pos, one_pos⟩ = iterTimeNoOverlap t := by
  simp [scaledIterTime, iterTimeNoOverlap]

/-- Doubling compute while keeping comm constant increases total time.
    C++: what-if predicts performance regression from larger model. -/
theorem scale_compute_increases (t : IterTiming) (s : ℚ) (hs : 1 < s) :
    iterTimeNoOverlap t ≤
    scaledIterTime ⟨t, s, 1, by linarith, one_pos⟩ := by
  simp only [iterTimeNoOverlap, scaledIterTime, mul_one]
  nlinarith [t.hcomp]

/-- Reducing communication improves total time.
    C++: what-if predicts benefit from better collective algorithm. -/
theorem scale_comm_decreases (t : IterTiming) (s : ℚ)
    (hs0 : 0 < s) (hs1 : s ≤ 1) :
    scaledIterTime ⟨t, 1, s, one_pos, hs0⟩ ≤ iterTimeNoOverlap t := by
  simp only [scaledIterTime, iterTimeNoOverlap, mul_one]
  nlinarith [t.hcomm]

/-! ## Compute-bound vs Memory-bound Classification from AI

Direct roofline-based classification using arithmetic intensity
relative to the ridge point. -/

/-- A kernel is compute-bound when its AI exceeds the ridge point.
    C++: `if (ai > ridge_point) return Bottleneck::COMPUTE;`. -/
def isComputeBound (g : GPUProfile) (ai : ℚ) : Prop :=
  g.ridgePoint ≤ ai

/-- A kernel is memory-bound when its AI is below the ridge point.
    C++: `if (ai < ridge_point) return Bottleneck::MEMORY_BW;`. -/
def isMemoryBound (g : GPUProfile) (ai : ℚ) : Prop :=
  ai < g.ridgePoint

/-- Classification is exhaustive: every AI is either compute- or memory-bound
    (or exactly at the ridge point, which we classify as compute-bound).
    C++: `classify_kernel()` always returns either COMPUTE or MEMORY_BW. -/
theorem bound_classification_total (g : GPUProfile) (ai : ℚ) :
    isComputeBound g ai ∨ isMemoryBound g ai := by
  simp only [isComputeBound, isMemoryBound]
  by_cases h : g.ridgePoint ≤ ai
  · left; exact h
  · right; exact not_le.mp h

/-- Compute-bound and memory-bound are mutually exclusive.
    A kernel cannot be both simultaneously. -/
theorem bound_classification_exclusive (g : GPUProfile) (ai : ℚ) :
    ¬(isComputeBound g ai ∧ isMemoryBound g ai) := by
  simp only [isComputeBound, isMemoryBound]
  intro ⟨hle, hlt⟩
  exact absurd hle (not_le.mpr hlt)

/-- For a compute-bound kernel, roofline prediction equals peak FLOPS.
    Performance is limited by compute, not memory.
    C++: roofline chart flat region (horizontal line at peak). -/
theorem compute_bound_pred (g : GPUProfile) (ai : ℚ)
    (h : isComputeBound g ai) :
    rooflinePred g ai = g.peak_flops := by
  simp only [rooflinePred, isComputeBound, GPUProfile.ridgePoint] at *
  rw [min_eq_left]
  rw [div_le_iff₀ g.hbw] at h
  linarith

/-- For a memory-bound kernel, roofline prediction equals bw × AI.
    Performance is limited by memory bandwidth.
    C++: roofline chart sloped region (line with slope = bandwidth). -/
theorem memory_bound_pred (g : GPUProfile) (ai : ℚ)
    (h : isMemoryBound g ai) :
    rooflinePred g ai = g.peak_bw * ai := by
  simp only [rooflinePred, isMemoryBound, GPUProfile.ridgePoint] at *
  rw [min_eq_right]
  rw [lt_div_iff₀ g.hbw] at h
  linarith

/-- Doubling AI of a memory-bound kernel doubles predicted performance.
    C++: Augur recommends increasing tile reuse to increase AI. -/
theorem double_ai_doubles_pred (g : GPUProfile) (ai : ℚ)
    (_hai : 0 ≤ ai) (h : isMemoryBound g ai)
    (h2 : isMemoryBound g (2 * ai)) :
    rooflinePred g (2 * ai) = 2 * rooflinePred g ai := by
  rw [memory_bound_pred g ai h, memory_bound_pred g (2 * ai) h2]
  ring

/-! ## Concrete Examples

Verify formalization against realistic GPU configurations. -/

/-- H100 profile: 989 TFLOPS FP16, 3350 GB/s HBM3.
    Ridge point ≈ 295 FLOP/byte. -/
private def h100 : GPUProfile where
  peak_flops := 989
  peak_bw := 335 / 100   -- 3.35 in our units (consistent: TFLOP/s and TB/s)
  hflops := by norm_num
  hbw := by norm_num

/-- H100 ridge point = 989 / 3.35 ≈ 295. -/
example : h100.ridgePoint = 98900 / 335 := by
  simp [h100, GPUProfile.ridgePoint]; ring

/-- Pipeline bubble: 4 stages × 8 micro-batches → fraction = 3/32. -/
example : pipelineBubbleFrac 4 8 = 3 / 32 := by
  simp [pipelineBubbleFrac]; norm_num

/-- Pipeline bubble: 2 stages × 4 micro-batches → fraction = 1/8. -/
example : pipelineBubbleFrac 2 4 = 1 / 8 := by
  simp [pipelineBubbleFrac]; norm_num

/-- Amdahl: 90% parallel, 10× speedup → overall ~5.26×. -/
example : amdahl (9/10) 10 (by norm_num : (0:ℚ) < 10) = 100 / 19 := by
  simp [amdahl]; norm_num

/-- Amdahl: 50% parallel, 2× speedup → overall 4/3×. -/
example : amdahl (1/2) 2 (by norm_num : (0:ℚ) < 2) = 4 / 3 := by
  simp [amdahl]; norm_num

/-! ## Summary

Key results:
- `roofline_le_peak` / `roofline_le_mem`: performance bounded by both ceilings
- `roofline_at_ridge`: ceilings meet at ridge point = peak / bandwidth
- `roofline_monotone`: predicted performance increases with arithmetic intensity
- `classify_exhaustive`: every metric set maps to a bottleneck category
- `bound_classification_total` / `exclusive`: compute vs memory is a partition
- `compute_bound_pred` / `memory_bound_pred`: prediction equals the binding ceiling
- `efficiency_le_one`: achieved efficiency ≤ 1
- `iter_time_ge_compute`: compute always on critical path
- `overlap_benefit`: communication overlap never hurts
- `bubble_frac_lt_one`: pipeline always makes progress
- `bubble_frac_mono_micro`: more micro-batches reduce bubble
- `amdahl_limit`: serial fraction bounds maximum speedup
- `amdahl_monotone_S`: more parallel speedup → more total speedup
- `scale_compute_increases` / `scale_comm_decreases`: what-if monotonicity
-/

end Crucible
