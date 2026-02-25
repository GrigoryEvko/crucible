import Mathlib.Tactic

/-!
# Crucible.Mode — CrucibleContext State Machine

Models CrucibleContext.h + ReplayEngine.h: foreground state machine
for Tier 1 compiled replay.

C++ has TWO modes (not four):
  `enum class ContextMode : uint8_t { RECORD, COMPILED };`
  `enum class ReplayStatus : uint8_t { MATCH, DIVERGED, COMPLETE };`

State transitions:
  RECORD → COMPILED   via activate(region)
  COMPILED → RECORD   via deactivate() (divergence or explicit stop)

Within COMPILED: advance(schema_hash, shape_hash) per op:
  MATCH    → cursor advances, pre-cache next entry
  DIVERGED → guard failed (schema or shape mismatch)
  COMPLETE → all ops matched, auto-reset for next iteration

C++ layout: CrucibleContext = 120 bytes.
  [0..63]   ReplayEngine (expected hashes, cursor, end, current, slot_table, ops, pool)
  [64]      ContextMode mode_
  [68..71]  compiled_iterations_
  [72..75]  diverged_count_
  [80..87]  active_region_
  [88..119] PoolAllocator

ReplayEngine = 64 bytes (one cache line):
  [0..7]   expected_schema_ (pre-cached guard)
  [8..15]  expected_shape_  (pre-cached guard)
  [16..23] cursor_          (current position in ops array)
  [24..31] end_             (one past last op)
  [32..39] current_         (last matched entry, survives reset)
  [40..47] slot_table_      (pool slot pointer table)
  [48..55] ops_             (base pointer for reset)
  [56..63] pool_            (diagnostics only)

The design doc's 4-state model maps to:
  Inactive  = no CrucibleContext exists (not modeled here)
  Recording = ContextMode::RECORD
  Compiled  = ContextMode::COMPILED + ReplayStatus::MATCH
  Diverged  = ContextMode::COMPILED + ReplayStatus::DIVERGED → deactivate → RECORD

Named `ReplayCtx` (not `Context`) to avoid collision with `Crucible.Context`
in Effects.lean, which models the effect system's capability context.
-/
namespace Crucible

/-- Runtime execution mode. C++: `enum class ContextMode : uint8_t`. -/
inductive ContextMode where
  | RECORD     -- foreground records + executes eagerly
  | COMPILED   -- foreground replays with pre-allocated outputs
  deriving DecidableEq, Repr

/-- Result of one advance() step. C++: `enum class ReplayStatus : uint8_t`. -/
inductive ReplayStatus where
  | MATCH      -- op matches compiled trace, outputs ready in pool
  | DIVERGED   -- guard failed — schema or shape mismatch
  | COMPLETE   -- all ops in region consumed (iteration done)
  deriving DecidableEq, Repr

/-- CrucibleContext + ReplayEngine state. Models the 120-byte C++ struct.
    Abstracts away ReplayEngine's cursor-based internals into
    (cursor, num_ops) pair. Named `ReplayCtx` to avoid collision with
    `Crucible.Context` in Effects.lean (which models fx::Bg/Init/Test). -/
structure ReplayCtx where
  mode : ContextMode
  cursor : Nat              -- ReplayEngine cursor_ position (ops matched so far)
  num_ops : Nat             -- ReplayEngine end_ - ops_ (0 when not compiled)
  compiled_iterations : Nat -- CrucibleContext::compiled_iterations_
  diverged_count : Nat      -- CrucibleContext::diverged_count_
  deriving Repr

/-- Well-formedness predicate capturing C++ invariants.
    RECORD mode: cursor and num_ops are both 0 (no engine state).
    COMPILED mode: cursor < num_ops (C++ assert: cursor_ < end_),
    which implies num_ops > 0 (no empty regions). -/
def ReplayCtx.WellFormed (ctx : ReplayCtx) : Prop :=
  match ctx.mode with
  | .RECORD   => ctx.cursor = 0 ∧ ctx.num_ops = 0
  | .COMPILED => ctx.cursor < ctx.num_ops

/-- Initial state: recording mode, no compiled state.
    C++: default-constructed CrucibleContext. -/
def ReplayCtx.init : ReplayCtx where
  mode := .RECORD
  cursor := 0
  num_ops := 0
  compiled_iterations := 0
  diverged_count := 0

/-- Activate: RECORD → COMPILED. Binds to a compiled region.
    C++: `CrucibleContext::activate(region)`.
    Precondition: region has a valid plan (region->plan != null)
    and at least one op (C++ init sets cursor_ = ops_, end_ = ops_ + num_ops,
    then asserts ops_ != end_ before first advance).
    Calls pool_.init(region->plan), engine_.init(region, &pool_).
    If already COMPILED, deactivates first — but since we overwrite
    all fields, the result is identical either way. -/
def ReplayCtx.activate (ctx : ReplayCtx) (region_ops : Nat)
    (_h : 0 < region_ops) : ReplayCtx :=
  { mode := .COMPILED
    cursor := 0
    num_ops := region_ops
    compiled_iterations := ctx.compiled_iterations
    diverged_count := ctx.diverged_count }

/-- Deactivate: COMPILED → RECORD.
    C++: `CrucibleContext::deactivate()`.
    Called on divergence, explicit stop, or region switch.
    Destroys pool, clears active_region_. -/
def ReplayCtx.deactivate (ctx : ReplayCtx) : ReplayCtx :=
  { mode := .RECORD
    cursor := 0
    num_ops := 0
    compiled_iterations := ctx.compiled_iterations
    diverged_count := ctx.diverged_count }

/-- Advance one op in COMPILED mode. Models the hot path.
    C++: CrucibleContext::advance() delegates to ReplayEngine::advance().

    Precondition (enforced by C++ assert): mode = COMPILED, cursor < num_ops.

    Guard checks (two L1d comparisons at fixed struct offsets):
      1. schema_hash vs expected_schema_ (op identity)
      2. shape_hash vs expected_shape_ (tensor geometry)

    On match: stores current_, increments cursor_, pre-caches NEXT entry.
    On last match: returns COMPLETE, compiled_iterations_++, engine_.reset().
    On mismatch: returns DIVERGED, diverged_count_++.

    schema_match/shape_match are Bool: true iff hash equals expected. -/
def ReplayCtx.advance (ctx : ReplayCtx)
    (schema_match shape_match : Bool) : ReplayCtx × ReplayStatus :=
  if ¬schema_match then
    -- Guard 1 failed: schema mismatch (op identity changed)
    ({ ctx with diverged_count := ctx.diverged_count + 1 }, .DIVERGED)
  else if ¬shape_match then
    -- Guard 2 failed: shape mismatch (tensor geometry changed)
    ({ ctx with diverged_count := ctx.diverged_count + 1 }, .DIVERGED)
  else if ctx.cursor + 1 = ctx.num_ops then
    -- Last op matched → COMPLETE. C++: compiled_iterations_++; engine_.reset();
    -- Auto-reset cursor for next iteration (current_ survives for output_ptr).
    ({ ctx with cursor := 0,
                compiled_iterations := ctx.compiled_iterations + 1 }, .COMPLETE)
  else
    -- Match, not last → advance cursor.
    -- C++: ++cursor_; expected_schema_ = cursor_->schema_hash;
    ({ ctx with cursor := ctx.cursor + 1 }, .MATCH)

/-! ## Well-Formedness -/

/-- Initial state is well-formed. -/
theorem ReplayCtx.init_wf : ReplayCtx.init.WellFormed := by
  simp [ReplayCtx.init, ReplayCtx.WellFormed]

/-- Activate produces a well-formed state.
    0 < region_ops guarantees cursor (0) < num_ops (region_ops). -/
theorem ReplayCtx.activate_wf (ctx : ReplayCtx) (n : Nat) (h : 0 < n) :
    (ctx.activate n h).WellFormed := by
  simp [ReplayCtx.activate, ReplayCtx.WellFormed, h]

/-- Deactivate produces a well-formed state. -/
theorem ReplayCtx.deactivate_wf (ctx : ReplayCtx) :
    ctx.deactivate.WellFormed := by
  simp [ReplayCtx.deactivate, ReplayCtx.WellFormed]

/-- Advance preserves well-formedness when called in COMPILED mode.
    C++ asserts cursor_ < end_ before every advance().
    On MATCH: cursor increments but stays < num_ops.
    On COMPLETE: cursor resets to 0, mode stays COMPILED, 0 < num_ops.
    On DIVERGED: cursor unchanged, still < num_ops. -/
theorem ReplayCtx.advance_wf (ctx : ReplayCtx) (sm shm : Bool)
    (hmode : ctx.mode = .COMPILED) (hwf : ctx.WellFormed) :
    (ctx.advance sm shm).1.WellFormed := by
  simp [ReplayCtx.WellFormed, hmode] at hwf
  unfold ReplayCtx.advance
  split
  · simp [ReplayCtx.WellFormed, hmode, hwf]
  · split
    · simp [ReplayCtx.WellFormed, hmode, hwf]
    · split
      · rename_i _ _ hLast
        simp [ReplayCtx.WellFormed, hmode]
        omega
      · rename_i _ _ hNotLast
        simp [ReplayCtx.WellFormed, hmode]
        omega

/-! ## State Machine Properties -/

/-- Advance is deterministic: same inputs → same output.
    Trivially true for any pure function, but worth stating explicitly
    since the C++ implementation uses mutable state + atomics. -/
theorem advance_det (ctx : ReplayCtx) (sm shm : Bool) :
    ∀ r₁ r₂, ctx.advance sm shm = r₁ → ctx.advance sm shm = r₂ → r₁ = r₂ := by
  intros r₁ r₂ h₁ h₂; rw [← h₁, ← h₂]

/-- Activate always produces COMPILED mode. -/
theorem activate_compiled (ctx : ReplayCtx) (n : Nat) (h : 0 < n) :
    (ctx.activate n h).mode = .COMPILED := rfl

/-- Deactivate always produces RECORD mode. -/
theorem deactivate_record (ctx : ReplayCtx) :
    ctx.deactivate.mode = .RECORD := rfl

/-- On MATCH, cursor advances by exactly 1.
    C++: `++cursor_` in ReplayEngine::advance(). -/
theorem advance_match_cursor (ctx : ReplayCtx)
    (hNotLast : ctx.cursor + 1 ≠ ctx.num_ops) :
    (ctx.advance true true).2 = .MATCH ∧
    (ctx.advance true true).1.cursor = ctx.cursor + 1 := by
  simp [ReplayCtx.advance, hNotLast]

/-- On COMPLETE, compiled_iterations increases by 1 and cursor resets.
    C++: `compiled_iterations_++; engine_.reset();` -/
theorem advance_complete (ctx : ReplayCtx)
    (hLast : ctx.cursor + 1 = ctx.num_ops) :
    (ctx.advance true true).2 = .COMPLETE ∧
    (ctx.advance true true).1.compiled_iterations = ctx.compiled_iterations + 1 ∧
    (ctx.advance true true).1.cursor = 0 := by
  simp [ReplayCtx.advance, hLast]

/-- On schema mismatch, DIVERGED and diverged_count increases. -/
theorem advance_schema_diverged (ctx : ReplayCtx) (shm : Bool) :
    (ctx.advance false shm).2 = .DIVERGED ∧
    (ctx.advance false shm).1.diverged_count = ctx.diverged_count + 1 := by
  simp [ReplayCtx.advance]

/-- On shape mismatch (schema matched), DIVERGED and diverged_count increases. -/
theorem advance_shape_diverged (ctx : ReplayCtx) :
    (ctx.advance true false).2 = .DIVERGED ∧
    (ctx.advance true false).1.diverged_count = ctx.diverged_count + 1 := by
  simp [ReplayCtx.advance]

/-- Advance never changes mode. Mode changes only via activate/deactivate.
    C++: advance() returns status but doesn't touch mode_. -/
theorem advance_preserves_mode (ctx : ReplayCtx) (sm shm : Bool) :
    (ctx.advance sm shm).1.mode = ctx.mode := by
  unfold ReplayCtx.advance
  split
  · rfl
  · split
    · rfl
    · split <;> rfl

/-- Advance never changes num_ops. The region pointer doesn't change.
    C++: advance() modifies cursor_ but not end_ or ops_. -/
theorem advance_preserves_num_ops (ctx : ReplayCtx) (sm shm : Bool) :
    (ctx.advance sm shm).1.num_ops = ctx.num_ops := by
  unfold ReplayCtx.advance
  split
  · rfl
  · split
    · rfl
    · split <;> rfl

/-- Advance never changes diverged_count on a successful match. -/
theorem advance_match_preserves_diverged (ctx : ReplayCtx)
    (hNotLast : ctx.cursor + 1 ≠ ctx.num_ops) :
    (ctx.advance true true).1.diverged_count = ctx.diverged_count := by
  simp [ReplayCtx.advance, hNotLast]

/-- Divergence recovery: deactivate after any advance returns to RECORD.
    The system self-heals — no permanent failure state. -/
theorem diverged_recovers (ctx : ReplayCtx) (sm shm : Bool) :
    (ctx.advance sm shm).1.deactivate.mode = .RECORD := rfl

/-- Round-trip: activate then deactivate preserves counters. -/
theorem activate_deactivate_preserves (ctx : ReplayCtx) (n : Nat) (h : 0 < n) :
    let ctx' := (ctx.activate n h).deactivate
    ctx'.compiled_iterations = ctx.compiled_iterations ∧
    ctx'.diverged_count = ctx.diverged_count := by
  simp [ReplayCtx.activate, ReplayCtx.deactivate]

/-! ## Iteration Execution -/

/-- Happy path: advance through `remaining` ops, all matching.
    Takes remaining steps as parameter; each step calls advance(true, true).
    Returns the final status when all steps match or early exit on non-MATCH. -/
def runRegion (ctx : ReplayCtx) (remaining : Nat) : ReplayCtx × ReplayStatus :=
  match remaining with
  | 0 => (ctx, .COMPLETE)
  | n + 1 =>
    let (ctx', status) := ctx.advance true true
    match status with
    | .MATCH => runRegion ctx' n
    | other => (ctx', other)

/-- Generalized: running (num_ops - cursor) matching advances from a
    well-formed COMPILED state completes with compiled_iterations + 1.
    Induction on remaining = num_ops - cursor. -/
private theorem runRegion_aux (ctx : ReplayCtx) (remaining : Nat)
    (hrem : remaining = ctx.num_ops - ctx.cursor)
    (hwf : ctx.cursor < ctx.num_ops) :
    let (ctx', status) := runRegion ctx remaining
    status = .COMPLETE ∧
    ctx'.compiled_iterations = ctx.compiled_iterations + 1 ∧
    ctx'.mode = ctx.mode ∧
    ctx'.cursor = 0 ∧
    ctx'.num_ops = ctx.num_ops ∧
    ctx'.diverged_count = ctx.diverged_count := by
  induction remaining generalizing ctx with
  | zero => omega
  | succ n ih =>
    simp only [runRegion, ReplayCtx.advance]
    by_cases hLast : ctx.cursor + 1 = ctx.num_ops
    · -- Last op: COMPLETE. n must be 0 since remaining = num_ops - cursor = 1.
      simp only [hLast, ite_true]
      exact ⟨rfl, rfl, rfl, rfl, rfl, rfl⟩
    · -- Not last: MATCH, recurse with cursor + 1
      simp only [hLast, ite_false]
      exact ih { ctx with cursor := ctx.cursor + 1 }
        (by simp; omega) (by simp; omega)

/-- Running a full region from cursor 0 returns COMPLETE and increments
    compiled_iterations by exactly 1. Models one complete compiled iteration.
    C++: activate sets cursor=0, then N calls to advance(match, match). -/
theorem runRegion_complete (ctx : ReplayCtx)
    (hmode : ctx.mode = .COMPILED)
    (hcursor : ctx.cursor = 0)
    (hn : 0 < ctx.num_ops) :
    let (ctx', status) := runRegion ctx ctx.num_ops
    status = .COMPLETE ∧
    ctx'.compiled_iterations = ctx.compiled_iterations + 1 ∧
    ctx'.mode = .COMPILED ∧
    ctx'.cursor = 0 := by
  have h := runRegion_aux ctx ctx.num_ops (by omega) (by omega)
  obtain ⟨h1, h2, h3, h4, _, _⟩ := h
  exact ⟨h1, h2, hmode ▸ h3, h4⟩

/-- Full iteration: activate with N ops, run N advances, all match.
    compiled_iterations increments by exactly 1. -/
theorem full_iteration_complete (ctx : ReplayCtx) (n : Nat) (hn : 0 < n) :
    let ctx_active := ctx.activate n hn
    let (ctx', status) := runRegion ctx_active n
    status = .COMPLETE ∧
    ctx'.compiled_iterations = ctx.compiled_iterations + 1 := by
  have := runRegion_complete (ctx.activate n hn) rfl rfl (by simp [ReplayCtx.activate, hn])
  simp only [ReplayCtx.activate] at this ⊢
  exact ⟨this.1, this.2.1⟩

/-- Stop from any mode returns to RECORD via deactivate. Clean shutdown. -/
theorem stop_resets (ctx : ReplayCtx) :
    ctx.deactivate.mode = .RECORD := rfl

end Crucible
