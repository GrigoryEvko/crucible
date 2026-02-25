import Mathlib.Tactic
import Crucible.Basic

/-!
# Crucible.LoopNode — L5/L6 Cyclic Computation in an Acyclic DAG

From L5 Graphs (MANIFESTO.md):

  "LoopNodes for cyclic computation: wraps acyclic body with feedback edges
   + termination (Repeat(N) | Until(ε)):
   - Compiled recurrence: RNN as one body × 1000 reps, no Python per timestep
   - Convergence execution: DEQ fixed-points, diffusion denoising — stop when converged
   - Cross-iteration pipelining: overlap N+1's forward with N's optimizer via double-buffering
   - Nested loops: DiLoCo inner/outer as nested LoopNodes, independently compilable
   - Self-referential: Crucible's own autotuning loop as a LoopNode"

From L6 Merkle DAG:

  "LoopNodes in the DAG: cycle semantics within acyclic hash framework.
   merkle_hash = hash(body.content_hash ⊕ "loop" ⊕ feedback_signature ⊕ termination).
   Transforms DAG from computation snapshot to computation PROGRAM."

This module formalizes:

1. **LoopNode structure**: body hash, feedback edges, termination condition
2. **Merkle hash for loops**: deterministic, sensitive to all components
3. **Repeat(N) semantics**: identity, composition, fixed points, monotonicity
4. **Until(ε) semantics**: fuel-bounded convergence detection
5. **Unrolling equivalence**: Repeat(N) = N sequential body applications
6. **Feedback edges**: output→input connections, signature hashing
7. **Cross-iteration pipelining**: double-buffered overlap, speedup bound
8. **Nested loops**: DiLoCo-style outer×inner, total iteration count
9. **Contraction mapping**: discrete model, convergence

C++ correspondence:
- LoopNode = planned extension to MerkleDag.h (LoopNode wraps RegionNode body)
- Repeat(N) = compiled recurrence with known iteration count
- Until(ε) = DEQ fixed-point, diffusion denoising loops
- Feedback edges = data_ptr tracking across iteration boundary
- Nested loops = DiLoCo inner (local SGD) / outer (pseudo-gradient sync)
- Pipelining = ReplayEngine double-buffer strategy for forward/backward overlap
-/

namespace Crucible

/-! ## 1. LoopNode Structure -/

/-- Termination condition for a LoopNode.
    C++ (planned): `enum class LoopTermination : uint8_t { Repeat, Until }`.
    Repeat(N): run body exactly N times (RNN, fixed unrolling).
    Until(ε): run until output ≈ input within tolerance (DEQ, diffusion). -/
inductive LoopTermination where
  | Repeat (n : Nat)        -- fixed N iterations
  | Until (epsilon : Nat)   -- converge within ε (discrete model)
  deriving DecidableEq, Repr

/-- A feedback edge: connects a body output to a body input for the next iteration.
    C++ (planned): `struct FeedbackEdge { uint16_t output_idx; uint16_t input_idx; }`. -/
structure FeedbackEdge where
  output_idx : Nat
  input_idx  : Nat
  deriving DecidableEq, Repr

/-- LoopNode: cyclic computation within the acyclic Merkle DAG.
    Wraps an acyclic body sub-DAG with feedback edges and termination. -/
structure LoopNode where
  body_hash      : Nat
  feedback_edges : List FeedbackEdge
  termination    : LoopTermination
  deriving DecidableEq, Repr

/-- Number of feedback edges. -/
def loopnode_feedback_count (ln : LoopNode) : Nat :=
  ln.feedback_edges.length

/-! ## 2. Merkle Hash for Loops

From L6: `merkle_hash = hash(body.content_hash ⊕ "loop" ⊕ feedback_signature ⊕ termination)`. -/

/-- Feedback signature: XOR-fold of edge pairs. -/
def feedback_signature (edges : List FeedbackEdge) : Nat :=
  edges.foldl (fun acc e => acc ^^^ (e.output_idx * 1000003 + e.input_idx)) 0

/-- Termination condition hash. Even = Repeat, odd = Until (injective). -/
def loopterm_hash : LoopTermination → Nat
  | .Repeat n   => n * 2
  | .Until eps  => eps * 2 + 1

/-- Merkle hash for a LoopNode via XOR combining. -/
def loopnode_merkle (body_hash loop_salt feedback_sig term_hash : Nat) : Nat :=
  body_hash ^^^ loop_salt ^^^ feedback_sig ^^^ term_hash

/-- Full merkle hash from a LoopNode structure. -/
def loopnode_full_merkle (ln : LoopNode) (loop_salt : Nat) : Nat :=
  loopnode_merkle ln.body_hash loop_salt
    (feedback_signature ln.feedback_edges) (loopterm_hash ln.termination)

/-- Merkle hash is deterministic: same inputs → same hash. -/
theorem loopnode_merkle_deterministic (bh ls fs th : Nat) :
    ∀ h₁ h₂, loopnode_merkle bh ls fs th = h₁ →
              loopnode_merkle bh ls fs th = h₂ → h₁ = h₂ := by
  intros _ _ e₁ e₂; rw [← e₁, ← e₂]

/-- Merkle hash sensitive to body: different body → different hash (when fb/term zeroed).
    THE content-addressing property. -/
theorem loopnode_merkle_body_sensitive (bh₁ bh₂ ls fs th : Nat)
    (hne : bh₁ ≠ bh₂) (hfs : fs = 0) (hth : th = 0) :
    loopnode_merkle bh₁ ls fs th ≠ loopnode_merkle bh₂ ls fs th := by
  subst hfs; subst hth
  simp only [loopnode_merkle, Nat.xor_zero]
  intro h
  apply hne
  -- h : bh₁ ^^^ ls = bh₂ ^^^ ls. XOR both sides by ls to cancel.
  have := congr_arg (· ^^^ ls) h
  simp only [Nat.xor_assoc, Nat.xor_self, Nat.xor_zero] at this
  exact this

/-- Termination encoding: Repeat ≠ Until (even vs odd). -/
theorem loopterm_hash_injective_kind (n m : Nat) :
    loopterm_hash (.Repeat n) ≠ loopterm_hash (.Until m) := by
  simp [loopterm_hash]; omega

/-- Repeat encoding injective. -/
theorem loopterm_hash_repeat_injective (n m : Nat) (hne : n ≠ m) :
    loopterm_hash (.Repeat n) ≠ loopterm_hash (.Repeat m) := by
  simp [loopterm_hash]; omega

/-- Until encoding injective. -/
theorem loopterm_hash_until_injective (n m : Nat) (hne : n ≠ m) :
    loopterm_hash (.Until n) ≠ loopterm_hash (.Until m) := by
  simp [loopterm_hash]; omega

/-! ## 3. Repeat(N) Semantics

Apply body N times. Direct recursion avoids `Function.iterate` API friction. -/

/-- Apply body N times to initial state.
    C++: compiled recurrence — one body × N reps, no Python per timestep. -/
def loop_repeat : (body : Nat → Nat) → Nat → Nat → Nat
  | _, init, 0     => init
  | f, init, n + 1 => loop_repeat f (f init) n

/-- Zero iterations = identity. -/
theorem loop_repeat_zero (body : Nat → Nat) (init : Nat) :
    loop_repeat body init 0 = init := rfl

/-- One iteration = single body application. -/
theorem loop_repeat_one (body : Nat → Nat) (init : Nat) :
    loop_repeat body init 1 = body init := rfl

/-- Successor: repeat(n+1, init) = repeat(n, body(init)).
    Peeling the first iteration. -/
theorem loop_repeat_succ (body : Nat → Nat) (init : Nat) (n : Nat) :
    loop_repeat body init (n + 1) = loop_repeat body (body init) n := rfl

/-- Composition: repeat(m+n) = repeat(n) ∘ repeat(m).
    Loop fission correctness. -/
theorem loop_repeat_compose (body : Nat → Nat) (init : Nat) (m n : Nat) :
    loop_repeat body init (m + n) = loop_repeat body (loop_repeat body init m) n := by
  induction m generalizing init with
  | zero => simp [loop_repeat]
  | succ k ih =>
    -- LHS: loop_repeat body init ((k+1) + n) = loop_repeat body (body init) (k + n) by succ rewrite
    -- RHS: loop_repeat body (loop_repeat body init (k+1)) n = loop_repeat body (loop_repeat body (body init) k) n
    show loop_repeat body init (k + 1 + n) = loop_repeat body (loop_repeat body (body init) k) n
    rw [show k + 1 + n = (k + n) + 1 from by omega, loop_repeat_succ]
    exact ih (body init)

/-- Repeat(n+1) = body ∘ repeat(n). Peeling the last iteration. -/
theorem loop_repeat_succ_last (body : Nat → Nat) (init : Nat) (n : Nat) :
    loop_repeat body init (n + 1) = body (loop_repeat body init n) := by
  rw [show n + 1 = n + 1 from rfl, loop_repeat_compose]
  rfl

/-- Fixed point: if body(x) = x, then repeat(n, x) = x for all n. -/
theorem loop_repeat_fixed (body : Nat → Nat) (x : Nat) (hfp : body x = x) (n : Nat) :
    loop_repeat body x n = x := by
  induction n with
  | zero => rfl
  | succ n ih => rw [loop_repeat_succ, hfp, ih]

/-- Monotonicity: body monotone and init₁ ≤ init₂ → repeat preserves ≤. -/
theorem loop_repeat_mono (body : Nat → Nat)
    (hmono : ∀ a b, a ≤ b → body a ≤ body b)
    (init₁ init₂ : Nat) (hle : init₁ ≤ init₂) (n : Nat) :
    loop_repeat body init₁ n ≤ loop_repeat body init₂ n := by
  induction n generalizing init₁ init₂ with
  | zero => exact hle
  | succ k ih => exact ih _ _ (hmono _ _ hle)

/-! ## 4. Until(ε) Semantics — Convergence -/

/-- Convergence iteration with fuel bound.
    Returns (final_state, iterations_used).
    Stops when |body(x) - x| ≤ epsilon OR fuel exhausted. -/
def converge_fuel (body : Nat → Nat) (init : Nat) (epsilon : Nat) :
    Nat → Nat × Nat
  | 0     => (init, 0)
  | fuel + 1 =>
    let next := body init
    if (next - init) + (init - next) ≤ epsilon
    then (next, 1)
    else
      let (result, iters) := converge_fuel body next epsilon fuel
      (result, iters + 1)

/-- Extract iteration count from convergence. -/
def converge_iters (body : Nat → Nat) (init epsilon fuel : Nat) : Nat :=
  (converge_fuel body init epsilon fuel).2

/-- Fixed point → convergence in 1 iteration. -/
theorem converge_at_fixed_point (body : Nat → Nat) (x : Nat) (eps fuel : Nat)
    (hfp : body x = x) (hfuel : 0 < fuel) :
    converge_fuel body x eps fuel = (x, 1) := by
  match fuel with
  | fuel' + 1 =>
    simp only [converge_fuel]
    rw [hfp]; simp

/-- Iterations used ≤ fuel. Safety bound. -/
theorem converge_iters_le_fuel (body : Nat → Nat) (init eps : Nat) :
    ∀ fuel, (converge_fuel body init eps fuel).2 ≤ fuel := by
  intro fuel
  induction fuel generalizing init with
  | zero => simp [converge_fuel]
  | succ n ih =>
    simp only [converge_fuel]
    split
    · omega
    · have := ih (body init); simp only []; omega

/-- Zero fuel → zero iterations. -/
theorem converge_zero_fuel (body : Nat → Nat) (init eps : Nat) :
    converge_fuel body init eps 0 = (init, 0) := rfl

/-- Large epsilon → at most 1 iteration. -/
theorem converge_large_epsilon (body : Nat → Nat) (init fuel : Nat)
    (hfuel : 0 < fuel) (eps : Nat)
    (heps : body init - init + (init - body init) ≤ eps) :
    (converge_fuel body init eps fuel).2 ≤ 1 := by
  match fuel with
  | fuel' + 1 =>
    simp only [converge_fuel]
    split
    · rfl
    · contradiction

/-- At a fixed point, convergence reports 1 iteration. -/
theorem loop_converge_at_fp (body : Nat → Nat) (x eps fuel : Nat)
    (hfp : body x = x) (hfuel : 0 < fuel) :
    converge_iters body x eps fuel = 1 := by
  simp only [converge_iters]
  rw [converge_at_fixed_point body x eps fuel hfp hfuel]

/-! ## 5. Unrolling Equivalence -/

/-- Unrolling: apply body N times. Definitionally equal to `loop_repeat`. -/
def unroll_loop (body : Nat → Nat) (n : Nat) (init : Nat) : Nat :=
  loop_repeat body init n

/-- Unrolled = looped (definitional). -/
theorem unroll_eq_repeat (body : Nat → Nat) (n : Nat) (init : Nat) :
    unroll_loop body n init = loop_repeat body init n := rfl

/-- Partial unroll: unroll(n+1) = body ∘ unroll(n). -/
theorem unroll_partial (body : Nat → Nat) (n : Nat) (init : Nat) :
    unroll_loop body (n + 1) init = body (unroll_loop body n init) := by
  simp only [unroll_loop]
  exact loop_repeat_succ_last body init n

/-- Unroll split: unroll(a+b) = unroll(b) ∘ unroll(a). -/
theorem unroll_split (body : Nat → Nat) (a b : Nat) (init : Nat) :
    unroll_loop body (a + b) init = unroll_loop body b (unroll_loop body a init) := by
  simp only [unroll_loop]
  exact loop_repeat_compose body init a b

/-- Identity body → unroll is identity. -/
theorem unroll_id (n : Nat) (init : Nat) :
    unroll_loop id n init = init := by
  simp only [unroll_loop]
  induction n with
  | zero => rfl
  | succ k ih => rw [loop_repeat_succ]; exact ih

/-! ## 6. Feedback Edges -/

/-- Empty feedback → zero signature. -/
theorem feedback_empty_zero : feedback_signature [] = 0 := rfl

/-- Feedback signature is deterministic. -/
theorem feedback_signature_det (edges : List FeedbackEdge) :
    ∀ s₁ s₂, feedback_signature edges = s₁ →
              feedback_signature edges = s₂ → s₁ = s₂ := by
  intros _ _ e₁ e₂; rw [← e₁, ← e₂]

/-- Feedback count matches edge list length. -/
theorem feedback_count_matches (ln : LoopNode) :
    loopnode_feedback_count ln = ln.feedback_edges.length := rfl

/-- Single edge with positive output_idx → nonzero signature. -/
theorem feedback_singleton_nonzero (e : FeedbackEdge) (h : 0 < e.output_idx) :
    feedback_signature [e] ≠ 0 := by
  simp [feedback_signature, List.foldl]; omega

/-- Appending an edge increases count by 1. -/
theorem feedback_append_count (edges : List FeedbackEdge) (e : FeedbackEdge) :
    (edges ++ [e]).length = edges.length + 1 := by simp

/-! ## 7. Cross-Iteration Pipelining

Sequential time = N × (forward + backward).
Pipeline time = N × max(forward, backward).
The overlap ensures the bottleneck stage dictates throughput. -/

/-- Sequential time for N iterations: no overlap. -/
def loop_sequential_time (f b : Nat) (n : Nat) : Nat := n * (f + b)

/-- Pipelined time with double-buffering: bottleneck stage dictates throughput. -/
def loop_pipeline_time (f b : Nat) (n : Nat) : Nat := n * max f b

theorem loop_sequential_zero (f b : Nat) :
    loop_sequential_time f b 0 = 0 := by simp [loop_sequential_time]

theorem loop_pipeline_zero (f b : Nat) :
    loop_pipeline_time f b 0 = 0 := by simp [loop_pipeline_time]

/-- Pipeline ≤ sequential: overlap never hurts.
    max(f, b) ≤ f + b, so n × max(f,b) ≤ n × (f + b).
    THE pipelining optimization theorem. -/
theorem loop_pipeline_le_sequential (f b : Nat) (n : Nat) :
    loop_pipeline_time f b n ≤ loop_sequential_time f b n := by
  unfold loop_pipeline_time loop_sequential_time
  exact Nat.mul_le_mul_left n (by omega)

/-- Balanced pipeline: f = b → pipeline_time = n × f. -/
theorem loop_pipeline_balanced (f : Nat) (n : Nat) :
    loop_pipeline_time f f n = n * f := by
  unfold loop_pipeline_time; simp

/-- Single iteration: sequential = f + b. -/
theorem loop_sequential_one (f b : Nat) :
    loop_sequential_time f b 1 = f + b := by simp [loop_sequential_time]

/-- Single iteration: pipeline = max(f, b). -/
theorem loop_pipeline_one (f b : Nat) :
    loop_pipeline_time f b 1 = max f b := by
  unfold loop_pipeline_time; simp

/-- Pipeline savings: sequential - pipeline = n × min(f, b). -/
theorem loop_pipeline_savings (f b : Nat) (n : Nat) :
    loop_sequential_time f b n - loop_pipeline_time f b n = n * min f b := by
  unfold loop_sequential_time loop_pipeline_time
  -- n * (f + b) - n * max f b = n * min f b
  -- Factor out n: n * ((f + b) - max f b) = n * min f b
  rw [← Nat.mul_sub]
  congr 1
  omega

/-! ## 8. Nested Loops (DiLoCo) -/

/-- Nested loops: outer_iters of (inner_iters of inner_body then one outer_body).
    C++ (L12): DiLoCo inner/outer as nested LoopNodes. -/
def loop_nested (outer_body inner_body : Nat → Nat) (outer_iters inner_iters : Nat)
    (init : Nat) : Nat :=
  loop_repeat (fun s => outer_body (loop_repeat inner_body s inner_iters))
    init outer_iters

/-- Zero outer iterations = identity. -/
theorem loop_nested_zero_outer (ob ib : Nat → Nat) (inner : Nat) (init : Nat) :
    loop_nested ob ib 0 inner init = init := rfl

/-- Zero inner iterations = just outer loop. -/
theorem loop_nested_zero_inner (ob ib : Nat → Nat) (outer : Nat) (init : Nat) :
    loop_nested ob ib outer 0 init = loop_repeat ob init outer := by
  induction outer generalizing init with
  | zero => rfl
  | succ k ih => simp [loop_nested, loop_repeat_succ] at *; exact ih _

/-- Single outer iteration: inner loop then one outer step. -/
theorem loop_nested_one_outer (ob ib : Nat → Nat) (inner : Nat) (init : Nat) :
    loop_nested ob ib 1 inner init = ob (loop_repeat ib init inner) := rfl

/-- Nested loop composition: outer(m+n) = outer(n) ∘ outer(m). -/
theorem loop_nested_compose (ob ib : Nat → Nat) (m n inner : Nat) (init : Nat) :
    loop_nested ob ib (m + n) inner init =
    loop_nested ob ib n inner (loop_nested ob ib m inner init) := by
  simp only [loop_nested]
  exact loop_repeat_compose _ init m n

/-- Repeat with id body is identity. -/
theorem loop_repeat_id (n : Nat) (init : Nat) :
    loop_repeat id init n = init := by
  induction n generalizing init with
  | zero => rfl
  | succ k ih => rw [loop_repeat_succ]; exact ih init

/-- Identity bodies → nested loop is identity. -/
theorem loop_nested_id (outer inner : Nat) (init : Nat) :
    loop_nested id id outer inner init = init := by
  unfold loop_nested
  -- Goal: loop_repeat (fun s => id (loop_repeat id s inner)) init outer = init
  suffices h : ∀ x, loop_repeat (fun s => id (loop_repeat id s inner)) x outer = x by exact h init
  intro x
  induction outer generalizing x with
  | zero => rfl
  | succ k ih =>
    rw [loop_repeat_succ]
    -- Goal: loop_repeat (fun s => id (loop_repeat id s inner)) (id (loop_repeat id x inner)) k = x
    -- Simplify the argument: id (loop_repeat id x inner) = x
    have : id (loop_repeat id x inner) = x := by simp [loop_repeat_id]
    rw [this]
    exact ih x

/-! ## 9. Contraction Mapping -/

/-- Weakly contracting: body(x) ≤ x for all x. -/
def loop_is_weakly_contracting (body : Nat → Nat) : Prop := ∀ x, body x ≤ x

/-- Weakly contracting → 0 is a fixed point. -/
theorem loop_contraction_has_fixed_point (body : Nat → Nat)
    (hc : loop_is_weakly_contracting body) :
    body 0 = 0 := by have := hc 0; omega

/-- Contraction produces non-increasing sequence: repeat(n+1) ≤ repeat(n). -/
theorem loop_contraction_monotone_decreasing (body : Nat → Nat)
    (hc : loop_is_weakly_contracting body) (init : Nat) (n : Nat) :
    loop_repeat body init (n + 1) ≤ loop_repeat body init n := by
  induction n generalizing init with
  | zero => exact hc init
  | succ k ih => exact ih (body init)

/-- Iterated contraction bounded by initial value. -/
theorem loop_contraction_bounded (body : Nat → Nat)
    (hc : loop_is_weakly_contracting body) (init : Nat) (n : Nat) :
    loop_repeat body init n ≤ init := by
  induction n with
  | zero => rfl
  | succ k ih =>
    exact le_trans (loop_contraction_monotone_decreasing body hc init k) ih

/-- Fixed point stability: body(x) = x → repeat(n, x) = x. -/
theorem loop_fixed_point_stable (body : Nat → Nat) (x : Nat)
    (hfp : body x = x) (n : Nat) :
    loop_repeat body x n = x :=
  loop_repeat_fixed body x hfp n

/-- Helper: loop_repeat body x m = 0 when m ≥ x and body is strictly contracting. -/
private theorem loop_contraction_reaches_zero_aux (body : Nat → Nat)
    (hc : loop_is_weakly_contracting body)
    (hstrict : ∀ x, 0 < x → body x < x)
    : ∀ m x, x ≤ m → loop_repeat body x m = 0 := by
  intro m
  induction m with
  | zero => intro x hxm; interval_cases x; rfl
  | succ n ih =>
    intro x hxm
    match x with
    | 0 => exact loop_repeat_fixed body 0 (loop_contraction_has_fixed_point body hc) (n + 1)
    | x' + 1 =>
      rw [loop_repeat_succ]
      exact ih (body (x' + 1)) (by have := hstrict (x' + 1) (by omega); omega)

/-- Strict contraction reaches 0 within init steps. -/
theorem loop_contraction_reaches_zero (body : Nat → Nat)
    (hc : loop_is_weakly_contracting body)
    (hstrict : ∀ x, 0 < x → body x < x) (init : Nat) :
    loop_repeat body init init = 0 :=
  loop_contraction_reaches_zero_aux body hc hstrict init init le_rfl

/-! ## 10. LoopNode in the DAG — Integration -/

/-- Different body → different full Merkle hash (when fb/term zeroed). -/
theorem loopnode_different_body_different_hash (ln₁ ln₂ : LoopNode) (salt : Nat)
    (hbody : ln₁.body_hash ≠ ln₂.body_hash)
    (hfb : ln₁.feedback_edges = ln₂.feedback_edges)
    (hterm : ln₁.termination = ln₂.termination)
    (hfs : feedback_signature ln₁.feedback_edges = 0)
    (hth : loopterm_hash ln₁.termination = 0) :
    loopnode_full_merkle ln₁ salt ≠ loopnode_full_merkle ln₂ salt := by
  simp only [loopnode_full_merkle, loopnode_merkle]
  -- Rewrite ln₂ side to use ln₁'s values (which are equal), then zero them
  rw [← hfb, ← hterm, hfs, hth]
  simp only [Nat.xor_zero]
  intro h
  apply hbody
  have := congr_arg (· ^^^ salt) h
  simp only [Nat.xor_assoc, Nat.xor_self, Nat.xor_zero] at this
  exact this

/-- XOR left cancellation: a ^^^ x = a ^^^ y → x = y. -/
private theorem xor_left_cancel (a x y : Nat) (h : a ^^^ x = a ^^^ y) : x = y := by
  have := congr_arg (a ^^^ ·) h
  simp only [← Nat.xor_assoc, Nat.xor_self, Nat.zero_xor] at this
  exact this

/-- Different termination → different Merkle hash. -/
theorem loopnode_different_term_different_hash (bh salt : Nat)
    (t₁ t₂ : LoopTermination) (hne : loopterm_hash t₁ ≠ loopterm_hash t₂) :
    loopnode_merkle bh salt 0 (loopterm_hash t₁) ≠
    loopnode_merkle bh salt 0 (loopterm_hash t₂) := by
  simp only [loopnode_merkle, Nat.xor_zero]
  -- Goal: bh ^^^ salt ^^^ loopterm_hash t₁ ≠ bh ^^^ salt ^^^ loopterm_hash t₂
  intro h
  exact hne (xor_left_cancel (bh ^^^ salt) _ _ h)

/-- Repeat(0) = no-op. -/
theorem loopnode_repeat_zero_is_noop (body : Nat → Nat) (init : Nat) :
    loop_repeat body init 0 = init := rfl

/-- Repeat(1) = single application. -/
theorem loopnode_repeat_one_is_region (body : Nat → Nat) (init : Nat) :
    loop_repeat body init 1 = body init := rfl

/-! ## 11. Concrete Examples -/

example : loop_repeat (· * 2) 1 3 = 8 := by native_decide
example : loop_repeat (· - 1) 5 5 = 0 := by native_decide
example : loop_repeat id 42 1000 = 42 := by native_decide
example : converge_fuel id 7 0 5 = (7, 1) := by native_decide
example : loop_nested (· + 1) (· * 2) 3 2 0 = 21 := by native_decide
example : feedback_signature [⟨0, 1⟩, ⟨1, 0⟩] =
    (0 ^^^ (0 * 1000003 + 1)) ^^^ (1 * 1000003 + 0) := by native_decide
example : loop_pipeline_time 10 10 5 = 50 := by native_decide
example : loop_sequential_time 10 10 5 = 100 := by native_decide
example : loop_sequential_time 10 10 5 - loop_pipeline_time 10 10 5 = 50 := by native_decide
example : loop_pipeline_time 10 5 4 = 40 := by native_decide
example : loop_sequential_time 10 5 4 = 60 := by native_decide
example : loopterm_hash (.Repeat 5) ≠ loopterm_hash (.Until 5) := by native_decide
example : loopterm_hash (.Repeat 3) ≠ loopterm_hash (.Repeat 7) := by native_decide

/-! ## Summary

**Key theorems (zero sorry):**
- `loop_repeat_compose`: loop fission correct (m+n = n after m)
- `loop_repeat_fixed`: fixed points stable under iteration
- `loop_pipeline_le_sequential`: pipelining never hurts
- `loop_contraction_monotone_decreasing`: contraction → non-increasing sequence
- `loop_contraction_reaches_zero`: strict contraction → reaches 0
- `converge_iters_le_fuel`: convergence respects fuel bounds
- `converge_at_fixed_point`: fixed points converge immediately
- `unroll_eq_repeat`: unrolled = looped (definitional)
- `loopnode_merkle_body_sensitive`: different body → different hash
- `loop_nested_compose`: nested loop splitting correct
-/

end Crucible
