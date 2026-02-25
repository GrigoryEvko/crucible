import Mathlib.Tactic

/-!
# Crucible.Detector — Iteration Boundary Detection

Models IterationDetector.h: K=5 signature matching with two-match confirmation.

Algorithm: sequential matching with cached expected value.
  Phase 1: build K=5 signature from first K ops.
  Phase 2: sequential match — one comparison per op (~1ns).

C++ struct layout (128 bytes = 2 cache lines):
  Cache line 0 (hot, touched every call):
    [0..7]   expected_hash_ (SchemaHash, pre-cached)
    [8..47]  signature[5] (SchemaHash array)
    [48]     match_pos_ (uint8_t, 0..K-1)
    [49]     confirmed (bool)
    [52..55] ops_since_boundary
    [56..59] signature_len
  Cache line 1 (cold, touched only at boundaries):
    [64..67] boundaries_detected
    [68..71] last_completed_len

Two-match confirmation prevents false positives from warmup:
  First K-match  → candidate (signature locked, not confirmed)
  Second K-match → confirmed iteration boundary (returns true)

Hot path (steady state, no match): ~1ns.
  inc [ops_since_boundary]     ; 1 cycle, no dependency
  cmp [expected_hash_], incoming ; 1 load (L1d) + 1 cmp
  jne .done                    ; well-predicted: taken (mismatch)
-/
namespace Crucible

/-- Iteration detector state. Models IterationDetector (128 bytes, 2 cache lines).
    Signature is a fixed-length list of K schema hashes.

    The C++ field `expected_hash_` is not modeled — it is a cache of
    `signature[match_pos]` used to avoid array indexing on the hot path.
    The Lean model computes `signature.getD match_pos 0` on each check call,
    which is semantically identical. -/
structure Detector where
  K : Nat                    -- signature length (default 5)
  signature : List Nat       -- first K schema hashes (length ≤ K)
  signature_len : Nat        -- hashes collected (0..K)
  match_pos : Nat            -- position in sequential match (0..K-1)
  confirmed : Bool           -- true after first full K-match
  ops_since_boundary : Nat   -- ops since last detected boundary
  boundaries_detected : Nat  -- total confirmed boundaries
  last_completed_len : Nat   -- length of last completed iteration

/-- Initial detector state. C++: all fields zero/false/empty. -/
def Detector.init (K : Nat := 5) : Detector where
  K := K
  signature := []
  signature_len := 0
  match_pos := 0
  confirmed := false
  ops_since_boundary := 0
  boundaries_detected := 0
  last_completed_len := 0

/-- Phase 1: Build signature from first K ops.
    C++: `build_signature_(schema_hash)`.
    Called exactly K times total, then never again.

    Note: does NOT increment ops_since_boundary — caller (check) already did it.
    C++ build_signature_() only appends to signature[] and bumps signature_len.
    When the signature is complete, C++ primes expected_hash_ = signature[0];
    this is implicit in the Lean model (computed on the fly from getD). -/
def Detector.buildSignature (d : Detector) (hash : Nat) : Detector × Bool :=
  ({ d with
    signature := d.signature ++ [hash]
    signature_len := d.signature_len + 1 }, false)

/-- Phase 2: Boundary handler when K consecutive hashes matched.
    C++: `on_match_()`.
    First match -> candidate (confirmed=true, no boundary yet).
    Second+ match -> confirmed boundary (returns true).
    In both cases: match_pos_ = 0, ops_since_boundary = K.

    C++ uses `std::sub_sat(ops_since_boundary, K)` for last_completed_len;
    Nat subtraction is already saturating. -/
def Detector.onMatch (d : Detector) : Detector × Bool :=
  if ¬d.confirmed then
    -- First match — candidate, not yet confirmed.
    ({ d with
      match_pos := 0
      confirmed := true
      ops_since_boundary := d.K }, false)
  else
    -- Second+ match — confirmed iteration boundary.
    let completed := d.ops_since_boundary - d.K
    ({ d with
      match_pos := 0
      ops_since_boundary := d.K
      boundaries_detected := d.boundaries_detected + 1
      last_completed_len := completed }, true)

/-- Check one op hash. Main entry point, called once per drained op.
    C++: `IterationDetector::check(SchemaHash schema_hash)`.

    Returns (new_state, boundary_detected).

    Three paths:
    1. Building signature (first K ops): collect hash, return false.
    2. No match (steady state): increment ops_since_boundary, return false.
    3. Sequential match advancing: if K consecutive match, call onMatch.

    The C++ hot path is ~1ns: one increment + one comparison + one branch.
    The Lean model faithfully captures the state transitions but not the
    performance characteristics (expected_hash_ caching, cache line layout). -/
def Detector.check (d : Detector) (hash : Nat) : Detector × Bool :=
  -- C++: ops_since_boundary++ is the FIRST instruction, unconditionally.
  let d_inc := { d with ops_since_boundary := d.ops_since_boundary + 1 }
  -- Phase 1: still building signature
  if d.signature_len < d.K then
    d_inc.buildSignature hash
  else
    -- Phase 2: sequential matching
    -- C++ compares against expected_hash_ (cached). We compute from signature.
    let expected := d.signature.getD d.match_pos 0
    if hash ≠ expected then
      -- Mismatch. Check if this hash starts a NEW match (overlapping patterns).
      -- C++: handles the case where the mismatching hash is actually
      -- signature[0], which would start a new match sequence.
      if d.match_pos ≠ 0 then
        if hash = d.signature.getD 0 0 then
          ({ d_inc with match_pos := 1 }, false)
        else
          ({ d_inc with match_pos := 0 }, false)
      else
        (d_inc, false)
    else
      -- Match — advance position
      let next := d.match_pos + 1
      if next ≥ d.K then
        d_inc.onMatch
      else
        ({ d_inc with match_pos := next }, false)

/-- Reset detector to initial state. C++: `IterationDetector::reset()`.
    Called by BackgroundThread when divergence-recovery clears stale data. -/
def Detector.reset (d : Detector) : Detector :=
  { Detector.init d.K with K := d.K }

/-- Apply a sequence of hashes. Models draining a batch of ring entries. -/
def Detector.checkAll : Detector → List Nat → Detector × List Bool
  | d, [] => (d, [])
  | d, h :: hs =>
    let (d', boundary) := d.check h
    let (d'', rest) := d'.checkAll hs
    (d'', boundary :: rest)

/-! ## Structural Lemmas -/

@[simp] theorem checkAll_nil (d : Detector) :
    d.checkAll [] = (d, []) := rfl

@[simp] theorem checkAll_cons (d : Detector) (h : Nat) (hs : List Nat) :
    d.checkAll (h :: hs) =
    let (d', b) := d.check h
    let (d'', bs) := d'.checkAll hs
    (d'', b :: bs) := rfl

theorem checkAll_append_fst (d : Detector) (xs ys : List Nat) :
    (d.checkAll (xs ++ ys)).1 = ((d.checkAll xs).1.checkAll ys).1 := by
  induction xs generalizing d with
  | nil => simp
  | cons x xs ih =>
    simp only [List.cons_append, Detector.checkAll]
    exact ih (d.check x).1

theorem checkAll_append_snd (d : Detector) (xs ys : List Nat) :
    (d.checkAll (xs ++ ys)).2 = (d.checkAll xs).2 ++ ((d.checkAll xs).1.checkAll ys).2 := by
  induction xs generalizing d with
  | nil => simp
  | cons x xs ih =>
    simp only [List.cons_append, Detector.checkAll]
    congr 1
    exact ih (d.check x).1

/-! ## Core Properties -/

/-- Check is total: defined for every (state, hash) pair. -/
theorem check_total (d : Detector) (h : Nat) :
    ∃ d' b, d.check h = (d', b) := ⟨(d.check h).1, (d.check h).2, rfl⟩

/-- Check is deterministic: same input produces same output. -/
theorem check_det (d : Detector) (h : Nat) :
    ∀ r₁ r₂, d.check h = r₁ → d.check h = r₂ → r₁ = r₂ := by
  intros r₁ r₂ h₁ h₂; rw [← h₁, ← h₂]

/-- Reset produces a valid initial state. -/
theorem reset_is_init (d : Detector) :
    (d.reset).signature_len = 0 ∧
    (d.reset).match_pos = 0 ∧
    (d.reset).confirmed = false ∧
    (d.reset).boundaries_detected = 0 := by
  simp [Detector.reset, Detector.init]

/-- buildSignature preserves ops_since_boundary: only check() increments it. -/
theorem buildSignature_preserves_ops (d : Detector) (hash : Nat) :
    (d.buildSignature hash).1.ops_since_boundary = d.ops_since_boundary := by
  simp [Detector.buildSignature]

/-- buildSignature always returns false. -/
theorem buildSignature_no_boundary (d : Detector) (hash : Nat) :
    (d.buildSignature hash).2 = false := by
  simp [Detector.buildSignature]

/-- onMatch sets ops_since_boundary to K. -/
theorem onMatch_sets_ops (d : Detector) :
    (d.onMatch).1.ops_since_boundary = d.K := by
  simp [Detector.onMatch]; split <;> rfl

/-- check always returns false during build phase. -/
theorem check_build_no_boundary (d : Detector) (hash : Nat)
    (hBuild : d.signature_len < d.K) :
    (d.check hash).2 = false := by
  simp [Detector.check, hBuild, Detector.buildSignature]

/-- check preserves K (it is never modified). -/
theorem check_preserves_K (d : Detector) (hash : Nat) :
    (d.check hash).1.K = d.K := by
  unfold Detector.check
  simp only
  split
  case isTrue _ => simp [Detector.buildSignature]
  case isFalse _ =>
    split
    case isTrue _ =>
      split
      case isTrue _ => split <;> rfl
      case isFalse _ => rfl
    case isFalse _ =>
      split
      case isTrue _ => simp [Detector.onMatch]; split <;> rfl
      case isFalse _ => rfl

/-- checkAll preserves K. -/
theorem checkAll_preserves_K (d : Detector) (stream : List Nat) :
    (d.checkAll stream).1.K = d.K := by
  induction stream generalizing d with
  | nil => simp
  | cons h hs ih =>
    simp only [checkAll_cons]
    rw [ih (d.check h).1, check_preserves_K]

/-! ## ops_since_boundary Behavior

The ops_since_boundary counter has two modes:
- Normal: incremented by 1 (build, mismatch, partial match advance)
- Reset: set to K by onMatch (candidate confirmation or boundary detection)

This is NOT monotonically increasing — onMatch resets the counter.
The theorem `check_ops_result` gives the precise characterization. -/

/-- After check, ops_since_boundary is either d.ops + 1 or d.K. -/
theorem check_ops_result (d : Detector) (hash : Nat) :
    (d.check hash).1.ops_since_boundary = d.ops_since_boundary + 1 ∨
    (d.check hash).1.ops_since_boundary = d.K := by
  unfold Detector.check
  simp only
  split
  case isTrue _ =>
    left; simp [Detector.buildSignature]
  case isFalse _ =>
    split
    case isTrue _ =>
      left
      split
      case isTrue _ => split <;> rfl
      case isFalse _ => rfl
    case isFalse _ =>
      split
      case isTrue _ =>
        right; simp [Detector.onMatch]; split <;> rfl
      case isFalse _ =>
        left; rfl

/-- Boundary detection requires confirmed=true. Before the first full
    K-match, check always returns false. -/
theorem no_boundary_before_confirmed (d : Detector) (hash : Nat)
    (hNotConf : ¬d.confirmed) :
    (d.check hash).2 = false := by
  unfold Detector.check
  simp only
  split
  case isTrue _ => simp [Detector.buildSignature]
  case isFalse _ =>
    split
    case isTrue _ =>
      split
      case isTrue _ => split <;> rfl
      case isFalse _ => rfl
    case isFalse _ =>
      split
      case isTrue _ => simp [Detector.onMatch, hNotConf]
      case isFalse _ => rfl

/-- boundaries_detected never decreases through check. -/
theorem check_boundaries_nondecreasing (d : Detector) (hash : Nat) :
    (d.check hash).1.boundaries_detected ≥ d.boundaries_detected := by
  unfold Detector.check
  simp only
  split
  case isTrue _ => simp [Detector.buildSignature]
  case isFalse _ =>
    split
    case isTrue _ =>
      split
      case isTrue _ => split <;> simp
      case isFalse _ => simp
    case isFalse _ =>
      split
      case isTrue _ =>
        simp [Detector.onMatch]
        split <;> simp_all
      case isFalse _ => simp

/-- boundaries_detected never decreases through checkAll. -/
theorem checkAll_boundaries_nondecreasing (d : Detector) (stream : List Nat) :
    (d.checkAll stream).1.boundaries_detected ≥ d.boundaries_detected := by
  induction stream generalizing d with
  | nil => simp
  | cons h hs ih =>
    simp only [checkAll_cons]
    calc ((d.check h).1.checkAll hs).1.boundaries_detected
        ≥ (d.check h).1.boundaries_detected := ih (d.check h).1
      _ ≥ d.boundaries_detected := check_boundaries_nondecreasing d h

/-! ## Build Phase — Proved for General K

The build phase processes the first K ops, appending each hash to the
signature. After completion, signature_len = K, match_pos = 0,
confirmed = false. This is a prerequisite for the matching phase. -/

/-- Generalized build: feeding `remaining` hashes to a detector that has
    already collected some prefix of its signature. -/
theorem build_phase_general (d : Detector) (remaining : List Nat)
    (hSigLen : d.signature_len = d.signature.length)
    (hBuild : d.signature_len + remaining.length ≤ d.K)
    (hConf : d.confirmed = false)
    (hMatch : d.match_pos = 0)
    (hBound : d.boundaries_detected = 0) :
    (d.checkAll remaining).1.signature = d.signature ++ remaining ∧
    (d.checkAll remaining).1.signature_len = d.signature_len + remaining.length ∧
    (d.checkAll remaining).1.match_pos = 0 ∧
    (d.checkAll remaining).1.confirmed = false ∧
    (d.checkAll remaining).1.ops_since_boundary = d.ops_since_boundary + remaining.length ∧
    (d.checkAll remaining).1.boundaries_detected = 0 ∧
    (d.checkAll remaining).1.K = d.K ∧
    (d.checkAll remaining).2 = List.replicate remaining.length false := by
  induction remaining generalizing d with
  | nil =>
    refine ⟨?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩ <;> simp [Detector.checkAll]
    · exact hMatch
    · exact hConf
    · exact hBound
  | cons h hs ih =>
    simp only [List.length_cons] at hBuild
    have hLt : d.signature_len < d.K := by omega
    have hCheck : d.check h = ({ d with
      signature := d.signature ++ [h]
      signature_len := d.signature_len + 1
      ops_since_boundary := d.ops_since_boundary + 1 }, false) := by
      simp [Detector.check, hLt, Detector.buildSignature]
    let d₁ : Detector := (d.check h).1
    have hd₁_eq : d₁ = { d with
      signature := d.signature ++ [h]
      signature_len := d.signature_len + 1
      ops_since_boundary := d.ops_since_boundary + 1 } := by
      simp [d₁, hCheck]
    have hd₁_siglen : d₁.signature_len = d.signature_len + 1 := by rw [hd₁_eq]
    have hd₁_sig : d₁.signature = d.signature ++ [h] := by rw [hd₁_eq]
    have hd₁_ops : d₁.ops_since_boundary = d.ops_since_boundary + 1 := by rw [hd₁_eq]
    have hd₁_K : d₁.K = d.K := by rw [hd₁_eq]
    have hCA_fst : (d.checkAll (h :: hs)).1 = (d₁.checkAll hs).1 := by
      simp [Detector.checkAll, d₁]
    have hCA_snd : (d.checkAll (h :: hs)).2 = false :: (d₁.checkAll hs).2 := by
      simp [Detector.checkAll, d₁, hCheck]
    have ih_result := ih d₁
      (by rw [hd₁_siglen, hd₁_sig, List.length_append, List.length_singleton, hSigLen])
      (by rw [hd₁_siglen, hd₁_K]; omega)
      (by rw [hd₁_eq]; exact hConf)
      (by rw [hd₁_eq]; exact hMatch)
      (by rw [hd₁_eq]; exact hBound)
    obtain ⟨hSig, hSL, hMP, hC, hOps, hBD, hK', hBs⟩ := ih_result
    refine ⟨?_, ?_, ?_, ?_, ?_, ?_, ?_, ?_⟩
    · rw [hCA_fst, hSig, hd₁_sig, List.append_assoc]; rfl
    · rw [hCA_fst, hSL, hd₁_siglen]; simp [List.length_cons]; omega
    · rw [hCA_fst]; exact hMP
    · rw [hCA_fst]; exact hC
    · rw [hCA_fst, hOps, hd₁_ops]; simp [List.length_cons]; ring
    · rw [hCA_fst]; exact hBD
    · rw [hCA_fst, hK', hd₁_K]
    · rw [hCA_snd, hBs]; simp [List.replicate_succ]

/-- After feeding `sig` to `Detector.init K`, the detector has
    signature = sig, signature_len = K, ready for matching. -/
theorem after_build (sig : List Nat) (hLen : sig.length = K) (_hK : 0 < K) :
    ((Detector.init K).checkAll sig).1.signature = sig ∧
    ((Detector.init K).checkAll sig).1.signature_len = K ∧
    ((Detector.init K).checkAll sig).1.match_pos = 0 ∧
    ((Detector.init K).checkAll sig).1.confirmed = false ∧
    ((Detector.init K).checkAll sig).1.ops_since_boundary = K ∧
    ((Detector.init K).checkAll sig).1.boundaries_detected = 0 ∧
    ((Detector.init K).checkAll sig).1.K = K ∧
    ((Detector.init K).checkAll sig).2 = List.replicate K false := by
  have h := build_phase_general (Detector.init K) sig
    (by simp [Detector.init]) (by simp [Detector.init, hLen]) (by simp [Detector.init])
    (by simp [Detector.init]) (by simp [Detector.init])
  simp only [Detector.init] at h ⊢
  obtain ⟨hSig, hSL, hMP, hC, hOps, hBD, hK', hBs⟩ := h
  exact ⟨hSig, by omega, hMP, hC, by omega, hBD, hK', by rw [hBs, hLen]⟩

/-! ## Two-Match Detection

The key correctness property: after feeding the signature three times
(build + first match + second match), at least one boundary is detected.

### Concrete instances (K=1..5) — proved by computation

Each instance feeds 3K hashes through the detector and verifies that
`boundaries_detected = 1` and at least one `true` appears in the output.
`native_decide` performs the full state machine execution at compile time. -/

theorem two_match_detects_boundary_K1 :
    let sig := [42]
    let stream := sig ++ sig ++ sig
    let d₀ := Detector.init 1
    let (d_final, boundaries) := d₀.checkAll stream
    boundaries.any id = true ∧ d_final.boundaries_detected = 1 := by
  native_decide

theorem two_match_detects_boundary_K2 :
    let sig := [10, 20]
    let stream := sig ++ sig ++ sig
    let d₀ := Detector.init 2
    let (d_final, boundaries) := d₀.checkAll stream
    boundaries.any id = true ∧ d_final.boundaries_detected = 1 := by
  native_decide

theorem two_match_detects_boundary_K3 :
    let sig := [1, 2, 3]
    let stream := sig ++ sig ++ sig
    let d₀ := Detector.init 3
    let (d_final, boundaries) := d₀.checkAll stream
    boundaries.any id = true ∧ d_final.boundaries_detected = 1 := by
  native_decide

/-- K=5 is the actual C++ constant. -/
theorem two_match_detects_boundary_K5 :
    let sig := [11, 22, 33, 44, 55]
    let stream := sig ++ sig ++ sig
    let d₀ := Detector.init 5
    let (d_final, boundaries) := d₀.checkAll stream
    boundaries.any id = true ∧ d_final.boundaries_detected = 1 := by
  native_decide

/-! ### Matching phase helper

When the detector has `signature = sig` and `signature_len = K`, feeding
elements from the signature advances `match_pos`. We prove this for the
case where we feed `sig.drop pos` (the remaining suffix starting at
the current match position). -/

/-- A single check during matching phase: when the hash matches the
    expected value and we haven't reached K yet, match_pos advances. -/
theorem check_match_advance (d : Detector) (hash : Nat)
    (hSigLen : ¬(d.signature_len < d.K))
    (hMatch : hash = d.signature.getD d.match_pos 0)
    (hNotDone : ¬(d.match_pos + 1 ≥ d.K)) :
    (d.check hash).1.match_pos = d.match_pos + 1 ∧
    (d.check hash).1.signature = d.signature ∧
    (d.check hash).1.signature_len = d.signature_len ∧
    (d.check hash).1.K = d.K ∧
    (d.check hash).1.confirmed = d.confirmed ∧
    (d.check hash).1.ops_since_boundary = d.ops_since_boundary + 1 ∧
    (d.check hash).1.boundaries_detected = d.boundaries_detected ∧
    (d.check hash).2 = false := by
  unfold Detector.check
  simp only [hSigLen, ite_false]
  subst hMatch
  simp only [ne_eq, not_true_eq_false, ite_false, hNotDone, ite_false]
  exact ⟨trivial, trivial, trivial, trivial, trivial, trivial, trivial, trivial⟩

/-- A single check during matching phase: when the hash matches and we
    reach K, onMatch fires. -/
theorem check_match_complete (d : Detector) (hash : Nat)
    (hSigLen : ¬(d.signature_len < d.K))
    (hMatch : hash = d.signature.getD d.match_pos 0)
    (hDone : d.match_pos + 1 ≥ d.K) :
    (d.check hash).1 = ({ d with ops_since_boundary := d.ops_since_boundary + 1 }).onMatch.1 ∧
    (d.check hash).2 = ({ d with ops_since_boundary := d.ops_since_boundary + 1 }).onMatch.2 := by
  unfold Detector.check
  simp only [hSigLen, ite_false]
  subst hMatch
  simp only [ne_eq, not_true_eq_false, ite_false, hDone, ite_true]
  exact ⟨trivial, trivial⟩

/-- Generalized matching: feeding `suffix` to a detector where each
    element of suffix matches the next expected signature element.
    Precondition: suffix = sig.drop match_pos, signature_len ≥ K.

    After processing, onMatch fires (match_pos resets to 0, confirmed/boundaries update).
    All intermediate results are false. -/
theorem match_suffix_gen (d : Detector) (suffix : List Nat)
    (hNotBuild : ¬(d.signature_len < d.K))
    (hSuffix : suffix = d.signature.drop d.match_pos)
    (hPosLen : d.match_pos + suffix.length = d.K)
    (hK : 0 < d.K)
    (hSigSz : d.signature.length = d.K)
    (hPosLt : d.match_pos < d.K)
    (hNonEmpty : suffix ≠ []) :
    (d.checkAll suffix).1.match_pos = 0 ∧
    (d.checkAll suffix).1.signature = d.signature ∧
    (d.checkAll suffix).1.signature_len = d.signature_len ∧
    (d.checkAll suffix).1.K = d.K ∧
    (d.confirmed = false →
      (d.checkAll suffix).1.confirmed = true ∧
      (d.checkAll suffix).1.boundaries_detected = d.boundaries_detected) ∧
    (d.confirmed = true →
      (d.checkAll suffix).1.boundaries_detected = d.boundaries_detected + 1) := by
  induction suffix generalizing d with
  | nil => exact absurd rfl hNonEmpty
  | cons sh st ih =>
    have hPosInBounds : d.match_pos < d.signature.length := by omega
    -- sh is the element at d.match_pos in d.signature
    have hSh : sh = d.signature.getD d.match_pos 0 := by
      have hdrop := hSuffix
      rw [List.drop_eq_getElem_cons hPosInBounds] at hdrop
      have heq := (List.cons.inj hdrop).1
      rw [heq]; unfold List.getD
      simp [hPosInBounds]
    -- st is the remainder after match_pos + 1
    have hSt : st = d.signature.drop (d.match_pos + 1) := by
      rw [List.drop_eq_getElem_cons hPosInBounds] at hSuffix
      exact (List.cons.inj hSuffix).2
    simp only [List.length_cons] at hPosLen
    by_cases hLast : st = []
    · -- Last element: match_pos + 1 = K, onMatch fires
      have hDone : d.match_pos + 1 ≥ d.K := by
        have : st.length = 0 := by rw [hLast]; simp
        omega
      have ⟨hCF, hCS⟩ := check_match_complete d sh hNotBuild (by rw [hSh]) hDone
      simp only [Detector.checkAll, hLast, checkAll_nil]
      constructor
      · rw [hCF]; simp [Detector.onMatch]; split <;> rfl
      constructor
      · rw [hCF]; simp [Detector.onMatch]; split <;> rfl
      constructor
      · rw [hCF]; simp [Detector.onMatch]; split <;> rfl
      constructor
      · rw [hCF]; simp [Detector.onMatch]; split <;> rfl
      constructor
      · intro hNC; rw [hCF]; simp [Detector.onMatch, hNC]
      · intro hC; rw [hCF]; simp [Detector.onMatch, hC]
    · -- Not last: advance match_pos, recurse
      have hNotDone : ¬(d.match_pos + 1 ≥ d.K) := by
        have : st.length > 0 := by
          cases st with
          | nil => exact absurd rfl hLast
          | cons _ _ => simp
        omega
      have ⟨hAMP, hASig, hASL, hAK, hAConf, hAOps, hABD, hABool⟩ :=
        check_match_advance d sh hNotBuild (by rw [hSh]) hNotDone
      set d₁ := (d.check sh).1
      have hCA_fst : (d.checkAll (sh :: st)).1 = (d₁.checkAll st).1 := by
        simp [Detector.checkAll, d₁]
      -- Apply IH to d₁
      have ih_result := ih d₁
        (by rw [hASL, hAK]; exact hNotBuild)
        (by rw [hAMP, hASig]; exact hSt)
        (by rw [hAMP, hAK]; omega)
        (by rw [hAK]; exact hK)
        (by rw [hASig, hAK]; exact hSigSz)
        (by rw [hAMP, hAK]; omega)
        hLast
      obtain ⟨ihMP, ihSig, ihSL, ihK, ihNC, ihC⟩ := ih_result
      refine ⟨?_, ?_, ?_, ?_, ?_, ?_⟩
      · rw [hCA_fst]; exact ihMP
      · rw [hCA_fst, ihSig]; exact hASig
      · rw [hCA_fst, ihSL]; exact hASL
      · rw [hCA_fst, ihK]; exact hAK
      · intro hNC
        rw [hCA_fst]
        have hNC₁ : d₁.confirmed = false := by rw [hAConf]; exact hNC
        obtain ⟨hc, hbd⟩ := ihNC hNC₁
        exact ⟨hc, by rw [hbd, hABD]⟩
      · intro hC
        rw [hCA_fst]
        have hC₁ : d₁.confirmed = true := by rw [hAConf]; exact hC
        rw [ihC hC₁, hABD]

/-- Full matching: feeding the entire signature to a ready detector triggers onMatch. -/
theorem full_match (d : Detector) (sig : List Nat)
    (hSigLen : d.signature_len = d.K)
    (hPos : d.match_pos = 0)
    (hSig : d.signature = sig)
    (hLen : sig.length = d.K)
    (hK : 0 < d.K) :
    (d.checkAll sig).1.match_pos = 0 ∧
    (d.checkAll sig).1.K = d.K ∧
    (d.checkAll sig).1.signature = sig ∧
    (d.checkAll sig).1.signature_len = d.K ∧
    (d.confirmed = false →
      (d.checkAll sig).1.confirmed = true ∧
      (d.checkAll sig).1.boundaries_detected = d.boundaries_detected) ∧
    (d.confirmed = true →
      (d.checkAll sig).1.boundaries_detected = d.boundaries_detected + 1) := by
  have hNonEmpty : sig ≠ [] := by intro h; rw [h] at hLen; simp at hLen; omega
  have hSuffix : sig = d.signature.drop d.match_pos := by rw [hSig, hPos]; simp
  have hms := match_suffix_gen d sig
    (by omega) hSuffix (by rw [hPos]; simp [hLen]) hK
    (by rw [hSig]; exact hLen) (by rw [hPos]; exact hK) hNonEmpty
  obtain ⟨hMP, hMSig, hMSL, hMK, hMNC, hMC⟩ := hms
  exact ⟨hMP, hMK, by rw [hMSig, hSig], by rw [hMSL, hSigLen], hMNC, hMC⟩

/-! ### General theorem

Composes after_build + full_match (first) + full_match (second). -/

theorem two_match_detects_boundary (K : Nat) (hK : 0 < K)
    (sig : List Nat) (hLen : sig.length = K)
    (stream : List Nat) (hStream : stream = sig ++ sig ++ sig) :
    let d₀ := Detector.init K
    let (d_final, boundaries) := d₀.checkAll stream
    boundaries.any id = true ∨ d_final.boundaries_detected > 0 := by
  -- Split stream into three segments
  subst hStream
  right
  -- After build phase: d₁ has signature = sig, signature_len = K, confirmed = false
  have hBuild := after_build sig hLen hK
  obtain ⟨hB_sig, hB_sl, hB_mp, hB_conf, _, hB_bd, hB_K, _⟩ := hBuild
  -- After first match: d₂ has confirmed = true, boundaries_detected = 0
  set d₁ := ((Detector.init K).checkAll sig).1
  have hLen₁ : sig.length = d₁.K := by rw [hLen, hB_K]
  have hFM1 := full_match d₁ sig
    (by rw [hB_sl, hB_K]) (by rw [hB_mp]) (by rw [hB_sig]) hLen₁ (by rw [hB_K]; exact hK)
  obtain ⟨hM1_mp, hM1_K, hM1_sig, hM1_sl, hM1_notconf, hM1_conf⟩ := hFM1
  have hM1_confirmed := (hM1_notconf hB_conf).1
  have hM1_bd := (hM1_notconf hB_conf).2
  -- After second match: d₃ has boundaries_detected = 1
  set d₂ := (d₁.checkAll sig).1
  have hLen₂ : sig.length = d₂.K := by rw [hLen, hM1_K, hB_K]
  have hd₂K : d₂.K = d₁.K := hM1_K
  have hFM2 := full_match d₂ sig
    (by rw [hM1_sl, hd₂K]) (by rw [hM1_mp]) (by rw [hM1_sig]) hLen₂
    (by rw [hd₂K, hB_K]; exact hK)
  obtain ⟨_, _, _, _, _, hM2_conf⟩ := hFM2
  have hM2_bd := hM2_conf hM1_confirmed
  -- boundaries_detected after all three segments
  rw [checkAll_append_fst, checkAll_append_fst]
  show ((d₁.checkAll sig).1.checkAll sig).1.boundaries_detected > 0
  rw [show (d₁.checkAll sig).1 = d₂ from rfl]
  rw [hM2_bd, hM1_bd, hB_bd]
  omega

end Crucible
