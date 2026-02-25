import Mathlib.Tactic

namespace Crucible

/-!
# Crucible.SymbolTable — Per-Symbol Metadata

Models SymbolTable.h: per-symbol metadata storage for the Graph IR's
symbolic shape reasoning. Maps SymbolId to (kind, hint, range, flags).

C++ structs:
  SymKind (uint8_t): SIZE, FLOAT, UNBACKED_INT, UNBACKED_FLOAT
  SymFlags (uint8_t): IS_SIZE_LIKE (1<<0), HAS_HINT (1<<1), IS_BACKED (1<<2)
  SymbolEntry (32 bytes):
    int64_t hint          -- concrete value from tracing (INT64_MIN = no hint)
    int64_t range_lower   -- lower bound
    int64_t range_upper   -- upper bound
    SymKind kind          -- symbol origin type
    uint8_t sym_flags     -- SymFlags bitfield
    uint16_t expr_flags   -- ExprFlags (IS_INTEGER, IS_POSITIVE, etc.)
    uint32_t _pad

  SymbolTable: vector<SymbolEntry> indexed by SymbolId (uint32_t).

Key specification properties:
- Range consistency: lower <= upper for every symbol
- Range containment: if a symbol has a hint, hint is within [lower, upper]
- Tighten monotonicity: tighten_range only narrows, never widens
- Symbol uniqueness: each SymbolId maps to exactly one entry (vector indexing)
- Default ranges: SIZE kind defaults to [2, +inf), UNBACKED_INT to [-inf, +inf)
-/

/-! ## Symbol origin kind

C++: `enum class SymKind : uint8_t`.
Mirrors `torch.utils._sympy.symbol.SymT`.
Determines naming prefix and default range assumptions. -/
inductive SymKind where
  | SIZE           -- s0, s1, ...; integer, typically >= 2
  | FLOAT          -- zf0, zf1, ...; real
  | UNBACKED_INT   -- u0, u1, ...; integer, no concrete hint
  | UNBACKED_FLOAT -- zuf0, zuf1, ...; real, no concrete hint
  deriving DecidableEq, Repr

/-! ## Per-symbol flags

C++: `struct SymFlags` with static constexpr bit constants.
Separate from ExprFlags on Expr nodes. -/
structure SymFlags where
  isSizeLike : Bool  -- can assume >= 2 in size-oblivious mode (bit 0)
  hasHint    : Bool  -- hint field is valid                    (bit 1)
  isBacked   : Bool  -- symbol originates from real tensor     (bit 2)
  deriving DecidableEq, Repr

/-- Default flags: no hint, not size-like, not backed.
    C++: `sym_flags = 0` in SymbolEntry NSDMI. -/
def SymFlags.default : SymFlags :=
  { isSizeLike := false, hasHint := false, isBacked := false }

/-! ## Expression flags (relevant subset for SymbolTable)

C++: `struct ExprFlags` in Ops.h, 13 bits.
SymbolTable stamps these onto Expr nodes created for this symbol.
We model the flags relevant to range reasoning as individual bools. -/
structure SymExprFlags where
  isInteger     : Bool  -- bit 0
  isReal        : Bool  -- bit 1
  isFinite      : Bool  -- bit 2
  isPositive    : Bool  -- bit 3
  isNegative    : Bool  -- bit 4
  isNonnegative : Bool  -- bit 5
  isNonpositive : Bool  -- bit 6
  isZero        : Bool  -- bit 7
  isEven        : Bool  -- bit 8
  isOdd         : Bool  -- bit 9
  deriving DecidableEq, Repr

/-! ## Symbol range

We model integer ranges over `Int` (arbitrary precision).
C++ uses int64_t with sentinel values:
  kIntPosInf = INT64_MAX
  kIntNegInf = INT64_MIN + 1
  kNoHint    = INT64_MIN

The Lean model uses `Option Int` for the hint (None = no hint)
and plain `Int` for bounds, avoiding sentinel encodings. -/
structure SymRange where
  lower : Int  -- lower bound (inclusive)
  upper : Int  -- upper bound (inclusive)
  hle   : lower ≤ upper  -- C++: maintained by tighten_range (only narrows)
  deriving Repr

/-! ## Symbol entry

C++: `struct SymbolEntry` (32 bytes, trivially relocatable).
One entry per registered symbol. -/
structure SymbolEntry where
  kind      : SymKind
  range     : SymRange
  hint      : Option Int     -- None = kNoHint (INT64_MIN sentinel in C++)
  symFlags  : SymFlags
  exprFlags : SymExprFlags
  deriving Repr

/-- Predicate: a symbol entry has well-formed hint (if present, within range). -/
def SymbolEntry.hintInRange (e : SymbolEntry) : Prop :=
  match e.hint with
  | none   => True
  | some h => e.range.lower ≤ h ∧ h ≤ e.range.upper

/-- Predicate: a SIZE-kind entry defaults to lower >= 2.
    C++: `e.range_lower = 2` for `SymKind::SIZE`. -/
def SymbolEntry.sizeKindValid (e : SymbolEntry) : Prop :=
  e.kind = SymKind.SIZE → e.range.lower ≥ 2

/-- Predicate: expression flags are consistent with range bounds.
    If isPositive, then lower > 0. If isNonnegative, then lower >= 0. -/
def SymbolEntry.flagsConsistent (e : SymbolEntry) : Prop :=
  (e.exprFlags.isPositive → e.range.lower > 0) ∧
  (e.exprFlags.isNonnegative → e.range.lower ≥ 0) ∧
  (e.exprFlags.isNegative → e.range.upper < 0) ∧
  (e.exprFlags.isNonpositive → e.range.upper ≤ 0) ∧
  (e.exprFlags.isZero → e.range.lower = 0 ∧ e.range.upper = 0)

/-! ## SymbolTable

C++: `class SymbolTable` with `std::vector<SymbolEntry> entries_`.
Indexed by SymbolId (uint32_t wrapper). We model it as a list
with well-formedness predicates. -/
structure SymbolTable where
  entries : List SymbolEntry

/-- SymbolId: index into the table. C++: `CRUCIBLE_STRONG_ID(SymbolId)`. -/
abbrev SymbolId := Fin

/-- Look up a symbol. C++: `operator[](SymbolId id)`. -/
def SymbolTable.get (st : SymbolTable) (id : Fin st.entries.length) : SymbolEntry :=
  st.entries[id]

/-- Number of registered symbols. C++: `size()`. -/
def SymbolTable.size (st : SymbolTable) : Nat :=
  st.entries.length

/-! ## Well-formedness

A SymbolTable is well-formed when every entry satisfies range consistency,
hint containment, and flag consistency. -/
def SymbolTable.WellFormed (st : SymbolTable) : Prop :=
  ∀ (i : Fin st.entries.length),
    let e := st.entries[i]
    e.hintInRange ∧ e.flagsConsistent

/-! ## Operations -/

/-- Add a symbol. C++: `SymbolId add(SymKind, uint16_t, bool)`.
    Returns (new_table, new_id). -/
def SymbolTable.add (st : SymbolTable) (e : SymbolEntry) :
    SymbolTable × Fin (st.entries.length + 1) :=
  (⟨st.entries ++ [e]⟩, ⟨st.entries.length, by omega⟩)

/-- Set hint. C++: `set_hint(SymbolId id, int64_t hint)`. -/
def SymbolTable.setHint (st : SymbolTable) (id : Fin st.entries.length)
    (hint : Int) : SymbolTable :=
  let e := st.entries[id]
  ⟨st.entries.set id
    { e with hint := some hint, symFlags := { e.symFlags with hasHint := true } }⟩

/-! ## Tighten range -- the core range-narrowing operation

C++: `tighten_range(SymbolId id, int64_t lower, int64_t upper)`.
Only narrows, never widens:
  if lower > e.range_lower then e.range_lower = lower
  if upper < e.range_upper then e.range_upper = upper
-/

/-- Tighten a range by intersecting with [newLower, newUpper].
    Returns the tightened range, or None if the intersection is empty. -/
def SymRange.tighten (r : SymRange) (newLower newUpper : Int) : Option SymRange :=
  if hle : max r.lower newLower ≤ min r.upper newUpper then
    some ⟨max r.lower newLower, min r.upper newUpper, hle⟩
  else
    none

/-- Tighten the range of a symbol in the table. -/
def SymbolTable.tightenRange (st : SymbolTable) (id : Fin st.entries.length)
    (lo hi : Int) : Option SymbolTable :=
  match (st.entries[id]).range.tighten lo hi with
  | none => none
  | some r' => some ⟨st.entries.set id { st.entries[id] with range := r' }⟩

/-- Set size-like flag. C++: `set_size_like(SymbolId id)`. -/
def SymbolTable.setSizeLike (st : SymbolTable) (id : Fin st.entries.length) :
    SymbolTable :=
  ⟨st.entries.set id
    { st.entries[id] with
      symFlags := { (st.entries[id]).symFlags with isSizeLike := true } }⟩

/-! ## Query operations -/

/-- Is the symbol guaranteed positive? C++: `is_positive(SymbolId id)`. -/
def SymbolTable.isPositive (st : SymbolTable) (id : Fin st.entries.length) : Bool :=
  st.entries[id].range.lower > 0

/-- Is the symbol guaranteed nonnegative? C++: `is_nonnegative(SymbolId id)`. -/
def SymbolTable.isNonnegative (st : SymbolTable) (id : Fin st.entries.length) : Bool :=
  st.entries[id].range.lower ≥ 0

/-- Does the symbol's range lie within [lo, hi]?
    C++: `range_contains(SymbolId id, int64_t lo, int64_t hi)`. -/
def SymbolTable.rangeContains (st : SymbolTable) (id : Fin st.entries.length)
    (lo hi : Int) : Bool :=
  st.entries[id].range.lower ≥ lo && st.entries[id].range.upper ≤ hi

/-! ## Theorems -/

/-- Tightened range is a subrange of the original.
    Proves `tighten_range` only narrows, never widens. -/
theorem SymRange.tighten_subrange (r : SymRange) (lo hi : Int)
    (r' : SymRange) (htight : r.tighten lo hi = some r') :
    r.lower ≤ r'.lower ∧ r'.upper ≤ r.upper := by
  simp only [SymRange.tighten] at htight
  split at htight
  · simp only [Option.some.injEq] at htight
    cases htight
    exact ⟨le_max_left _ _, min_le_left _ _⟩
  · exact absurd htight (by simp)

/-- Tightened range is contained within [lo, hi].
    The new bounds respect the requested constraint. -/
theorem SymRange.tighten_within (r : SymRange) (lo hi : Int)
    (r' : SymRange) (htight : r.tighten lo hi = some r') :
    lo ≤ r'.lower ∧ r'.upper ≤ hi := by
  simp only [SymRange.tighten] at htight
  split at htight
  · simp only [Option.some.injEq] at htight
    cases htight
    exact ⟨le_max_right _ _, min_le_right _ _⟩
  · exact absurd htight (by simp)

/-- Tightened range preserves range validity (lower <= upper). -/
theorem SymRange.tighten_valid (r : SymRange) (lo hi : Int)
    (r' : SymRange) (_ : r.tighten lo hi = some r') :
    r'.lower ≤ r'.upper := r'.hle

/-- Tightening is idempotent when range already within [lo, hi]. -/
theorem SymRange.tighten_idempotent (r : SymRange) (lo hi : Int)
    (hlo : lo ≤ r.lower) (hhi : r.upper ≤ hi) :
    r.tighten lo hi = some r := by
  simp only [SymRange.tighten, max_eq_left hlo, min_eq_left hhi]
  simp [r.hle]

/-- Adding a symbol increases size by exactly one. -/
theorem SymbolTable.add_size (st : SymbolTable) (e : SymbolEntry) :
    (st.add e).1.size = st.size + 1 := by
  simp [SymbolTable.add, SymbolTable.size]

/-- Adding preserves existing entries. -/
theorem SymbolTable.add_preserves (st : SymbolTable) (e : SymbolEntry)
    (i : Nat) (hi : i < st.entries.length) :
    have : i < (st.add e).1.entries.length := by simp [SymbolTable.add]; omega
    (st.add e).1.entries[i] = st.entries[i] := by
  simp only [SymbolTable.add]
  rw [List.getElem_append_left (by exact hi)]

/-- The newly added symbol has the expected entry. -/
theorem SymbolTable.add_gets (st : SymbolTable) (e : SymbolEntry) :
    have : st.entries.length < (st.add e).1.entries.length := by
      simp [SymbolTable.add]
    (st.add e).1.entries[st.entries.length] = e := by
  simp only [SymbolTable.add]
  rw [List.getElem_append_right (by omega)]
  simp

/-- Setting hint preserves table size. -/
theorem SymbolTable.setHint_size (st : SymbolTable) (id : Fin st.entries.length)
    (hint : Int) : (st.setHint id hint).size = st.size := by
  simp [SymbolTable.setHint, SymbolTable.size]

/-- Setting hint marks HAS_HINT flag true. -/
theorem SymbolTable.setHint_hasHint (st : SymbolTable) (id : Fin st.entries.length)
    (hint : Int) :
    have : id.val < (st.setHint id hint).entries.length := by
      simp [SymbolTable.setHint]
    ((st.setHint id hint).entries[id.val]).symFlags.hasHint = true := by
  simp [SymbolTable.setHint]

/-- Setting hint stores the hint value. -/
theorem SymbolTable.setHint_value (st : SymbolTable) (id : Fin st.entries.length)
    (hint : Int) :
    have : id.val < (st.setHint id hint).entries.length := by
      simp [SymbolTable.setHint]
    ((st.setHint id hint).entries[id.val]).hint = some hint := by
  simp [SymbolTable.setHint]

/-- rangeContains is true iff the symbol's range is enclosed in [lo, hi]. -/
theorem SymbolTable.rangeContains_spec (st : SymbolTable)
    (id : Fin st.entries.length) (lo hi : Int) :
    st.rangeContains id lo hi = true ↔
      st.entries[id].range.lower ≥ lo ∧ st.entries[id].range.upper ≤ hi := by
  simp [SymbolTable.rangeContains, Bool.and_eq_true, decide_eq_true_eq]

/-- isPositive is true iff lower bound > 0. -/
theorem SymbolTable.isPositive_spec (st : SymbolTable)
    (id : Fin st.entries.length) :
    st.isPositive id = true ↔ st.entries[id].range.lower > 0 := by
  simp [SymbolTable.isPositive, decide_eq_true_eq]

/-- isNonnegative is true iff lower bound >= 0. -/
theorem SymbolTable.isNonnegative_spec (st : SymbolTable)
    (id : Fin st.entries.length) :
    st.isNonnegative id = true ↔ st.entries[id].range.lower ≥ 0 := by
  simp [SymbolTable.isNonnegative, decide_eq_true_eq]

/-- Positive implies nonnegative. -/
theorem SymbolTable.positive_implies_nonneg (st : SymbolTable)
    (id : Fin st.entries.length) (hp : st.isPositive id = true) :
    st.isNonnegative id = true := by
  rw [isPositive_spec] at hp
  rw [isNonnegative_spec]
  omega

/-- If a well-formed table has a hint, the hint is within range. -/
theorem SymbolTable.hint_in_range (st : SymbolTable) (hwf : st.WellFormed)
    (id : Fin st.entries.length) (v : Int)
    (hhint : st.entries[id].hint = some v) :
    st.entries[id].range.lower ≤ v ∧ v ≤ st.entries[id].range.upper := by
  have h := (hwf id).1
  unfold SymbolEntry.hintInRange at h
  rw [hhint] at h
  exact h

/-- Tightening preserves table size. -/
theorem SymbolTable.tightenRange_size (st : SymbolTable)
    (id : Fin st.entries.length) (lo hi : Int)
    (st' : SymbolTable) (htight : st.tightenRange id lo hi = some st') :
    st'.size = st.size := by
  simp only [SymbolTable.tightenRange] at htight
  split at htight
  · simp at htight
  · simp only [Option.some.injEq] at htight
    subst htight
    simp [SymbolTable.size]

/-- setSizeLike preserves table size. -/
theorem SymbolTable.setSizeLike_size (st : SymbolTable)
    (id : Fin st.entries.length) :
    (st.setSizeLike id).size = st.size := by
  simp [SymbolTable.setSizeLike, SymbolTable.size]

/-- setSizeLike sets the flag to true. -/
theorem SymbolTable.setSizeLike_flag (st : SymbolTable)
    (id : Fin st.entries.length) :
    have : id.val < (st.setSizeLike id).entries.length := by
      simp [SymbolTable.setSizeLike]
    ((st.setSizeLike id).entries[id.val]).symFlags.isSizeLike = true := by
  simp [SymbolTable.setSizeLike]

end Crucible
