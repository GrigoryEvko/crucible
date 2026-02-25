import Mathlib.Data.Finset.Basic
import Mathlib.Tactic

/-!
# Crucible.Expr — Interned Symbolic Expressions

Models Expr.h + ExprPool.h: immutable, arena-allocated expression nodes
with Swiss-table interning. The key specification property:

  **Structural equality <-> pointer equality (interning correctness)**

C++ insight: every Expr is 32 bytes, arena-allocated, globally unique.
Two expressions with identical structure (same Op, payload, flags,
children) are guaranteed to be the SAME pointer. Comparison is pointer
equality (~1ns), not structural traversal.

## Expression types (C++ `Op` enum in Ops.h)

Atoms (nargs=0): INTEGER, FLOAT, SYMBOL, BOOL_TRUE, BOOL_FALSE.
Arithmetic: ADD (variadic), MUL (variadic), POW (binary), NEG (unary).
Relational: EQ, NE, LT, LE, GT, GE (binary -> boolean).
Logic: AND (variadic), OR (variadic), NOT (unary).
Division: FLOOR_DIV, CEIL_DIV, MOD, MODULAR_INDEXING.
Control: WHERE (ternary).
Math: SQRT, COS, SIN, EXP, LOG, ABS, ... (unary).

## Interning (ExprPool.h)

Swiss table: SIMD control bytes + slot array. At 87.5% load with
32-byte groups (AVX2), ~98.5% of lookups resolve in first group.

Construction methods perform eager canonicalization:
- Constant folding: add(3, 5) -> integer(8)
- Identity elimination: add(x, 0) -> x, mul(x, 1) -> x
- Flattening: add(add(a, b), c) -> add(a, b, c)
- Canonical ordering: add(b, a) -> add(a, b)
- Term combining: add(a, 2a) -> 3a

## Formalization strategy

We model Expr as a well-founded inductive type (trees, not DAGs with
sharing). The `ExprPool` is modeled as a function from expressions to
unique identifiers (Nat), with the interning invariant: structurally
equal expressions map to the same identifier.

Evaluation is modeled as a total function from expressions + environment
to an option value (partial: division by zero, etc.).
-/

namespace Crucible

/-! ## Op -- see Crucible.Ops for the full 60-op enumeration.
    The Expr inductive below encodes the op type directly in its
    constructors rather than parameterizing over Op. -/

/-! ## ExprFlags -- Type/assumption bitfield

Models `struct ExprFlags` from Ops.h. 13 bits encoding type properties
and assumptions about an expression's value domain. O(1) queries via
single AND instruction in C++. -/

/-- Expression property flags. C++: `struct ExprFlags` bitfield (uint16_t).
    Each flag encodes a property of the expression's value domain.
    Multiple flags can be set simultaneously (e.g., IS_INTEGER and IS_POSITIVE). -/
structure ExprFlags where
  is_integer     : Bool := false
  is_real        : Bool := false
  is_finite      : Bool := false
  is_positive    : Bool := false
  is_negative    : Bool := false
  is_nonnegative : Bool := false
  is_nonpositive : Bool := false
  is_zero        : Bool := false
  is_even        : Bool := false
  is_odd         : Bool := false
  is_number      : Bool := false
  is_symbol      : Bool := false
  is_boolean     : Bool := false
  deriving DecidableEq, Repr

/-- Flags for an integer constant. C++: `detail::integer_flags(int64_t val)`.
    Integers are always: IS_INTEGER, IS_REAL, IS_FINITE, IS_NUMBER.
    Sign and parity flags derived from the value. -/
def ExprFlags.ofInt (val : Int) : ExprFlags where
  is_integer     := true
  is_real        := true
  is_finite      := true
  is_positive    := decide (val > 0)
  is_negative    := decide (val < 0)
  is_nonnegative := decide (val ≥ 0)
  is_nonpositive := decide (val ≤ 0)
  is_zero        := decide (val = 0)
  is_even        := decide (val % 2 = 0)
  is_odd         := decide (val % 2 ≠ 0)
  is_number      := true
  is_symbol      := false
  is_boolean     := false

/-- Flags for boolean values. C++: `ExprFlags::IS_BOOLEAN`. -/
def ExprFlags.boolean : ExprFlags where
  is_boolean := true

/-- Bitwise AND of two flag sets. C++: `f &= args[i]->flags` in
    `composite_flags()` for variadic ops (ADD, MUL, MIN, MAX). -/
def ExprFlags.inter (a b : ExprFlags) : ExprFlags where
  is_integer     := a.is_integer     && b.is_integer
  is_real        := a.is_real        && b.is_real
  is_finite      := a.is_finite      && b.is_finite
  is_positive    := a.is_positive    && b.is_positive
  is_negative    := a.is_negative    && b.is_negative
  is_nonnegative := a.is_nonnegative && b.is_nonnegative
  is_nonpositive := a.is_nonpositive && b.is_nonpositive
  is_zero        := a.is_zero        && b.is_zero
  is_even        := a.is_even        && b.is_even
  is_odd         := a.is_odd         && b.is_odd
  is_number      := a.is_number      && b.is_number
  is_symbol      := a.is_symbol      && b.is_symbol
  is_boolean     := a.is_boolean     && b.is_boolean

/-! ## Expr -- Immutable expression tree

Models `struct Expr` from Expr.h (32 bytes in C++). The C++ runtime uses
pointer identity for equality (all nodes interned). We model the semantic
structure as an inductive type. Interning correctness is proved separately
as a property of ExprPool.

Note: Lean cannot auto-derive `DecidableEq` for mutual/nested inductive
types with `List`. We provide `BEq` via `Repr` instead and state
decidable equality explicitly where needed. -/

/-- Symbolic expression. C++: `struct Expr` (32 bytes, arena-allocated).
    All nodes are immutable. Children are themselves expressions (in C++,
    `const Expr**` pointing to interned children).

    We use a well-founded inductive (tree), not a DAG. The interning
    invariant (structural equality <-> identity) is modeled by `ExprPool`. -/
inductive Expr where
  | int    : Int → Expr
  | float  : Int → Expr            -- payload stored as bitcast int64
  | sym    : Nat → Expr             -- SymbolId (index into symbol table)
  | bool   : Bool → Expr
  | add    : List Expr → Expr       -- variadic
  | mul    : List Expr → Expr       -- variadic
  | pow    : Expr → Expr → Expr     -- base, exponent
  | neg    : Expr → Expr            -- canonical: mul [int (-1), e]
  | eq     : Expr → Expr → Expr
  | ne     : Expr → Expr → Expr
  | lt     : Expr → Expr → Expr
  | le     : Expr → Expr → Expr
  | gt     : Expr → Expr → Expr
  | ge     : Expr → Expr → Expr
  | and_   : List Expr → Expr       -- variadic
  | or_    : List Expr → Expr       -- variadic
  | not_   : Expr → Expr
  | floorDiv : Expr → Expr → Expr
  | ceilDiv  : Expr → Expr → Expr
  | mod_   : Expr → Expr → Expr
  | modIdx : Expr → Expr → Expr → Expr  -- base, divisor, modulus
  | where_ : Expr → Expr → Expr → Expr  -- cond, true_val, false_val
  | abs    : Expr → Expr
  | sqrt   : Expr → Expr
  | exp    : Expr → Expr
  | log    : Expr → Expr
  | identity : Expr → Expr
  deriving Repr

/-! ## Structural properties -/

/-- An expression is atomic (has no children). C++: `Expr::is_atom()`. -/
def Expr.isAtom : Expr → Bool
  | .int _  => true
  | .float _ => true
  | .sym _  => true
  | .bool _ => true
  | _       => false

/-- An expression is an integer constant with a specific value. -/
def Expr.isIntVal (e : Expr) (v : Int) : Bool :=
  match e with
  | .int n => n == v
  | _      => false

/-- C++: `Expr::is_zero_int()` -- `op == Op::INTEGER && payload == 0`. -/
def Expr.isZero : Expr → Bool
  | .int 0 => true
  | _      => false

/-- C++: `Expr::is_one()` -- `op == Op::INTEGER && payload == 1`. -/
def Expr.isOne : Expr → Bool
  | .int 1 => true
  | _      => false

/-- C++: `Expr::is_neg_one()` -- `op == Op::INTEGER && payload == -1`. -/
def Expr.isNegOne : Expr → Bool
  | .int (-1) => true
  | _         => false

/-! ## Compute flags for an expression

Models `detail::integer_flags()` and `detail::composite_flags()`. -/

/-- Flags for a POW node. C++: `composite_flags(POW, ...)` intersects
    IS_REAL and IS_FINITE from both children. -/
def ExprFlags.ofPow (base exp_ : ExprFlags) : ExprFlags where
  is_real   := base.is_real   && exp_.is_real
  is_finite := base.is_finite && exp_.is_finite

/-- Flags for real-valued unary math ops (SQRT, EXP, LOG).
    C++: always IS_REAL, IS_FINITE, IS_NUMBER. -/
def ExprFlags.realMath : ExprFlags where
  is_real    := true
  is_finite  := true
  is_number  := true

/-- Flags for integer division ops (FLOOR_DIV, CEIL_DIV, etc.).
    C++: always IS_INTEGER, IS_REAL, IS_FINITE, IS_NUMBER. -/
def ExprFlags.integerResult : ExprFlags where
  is_integer := true
  is_real    := true
  is_finite  := true
  is_number  := true

/-- Compute the ExprFlags for an expression.
    C++: flags are set at construction time by `integer_flags()` for atoms
    and `composite_flags()` for compound nodes. -/
def Expr.flags : Expr → ExprFlags
  | .int v    => ExprFlags.ofInt v
  | .float _  => ExprFlags.realMath
  | .sym _    => { is_symbol := true }
  | .bool _   => ExprFlags.boolean
  | .add es   => es.foldl (fun acc e => ExprFlags.inter acc e.flags)
                   ExprFlags.integerResult
  | .mul es   => es.foldl (fun acc e => ExprFlags.inter acc e.flags)
                   ExprFlags.integerResult
  | .pow b e  => ExprFlags.ofPow b.flags e.flags
  | .eq  _ _  => ExprFlags.boolean
  | .ne  _ _  => ExprFlags.boolean
  | .lt  _ _  => ExprFlags.boolean
  | .le  _ _  => ExprFlags.boolean
  | .gt  _ _  => ExprFlags.boolean
  | .ge  _ _  => ExprFlags.boolean
  | .and_ _   => ExprFlags.boolean
  | .or_  _   => ExprFlags.boolean
  | .not_ _   => ExprFlags.boolean
  | .neg e    => e.flags
  | .abs e    => e.flags
  | .identity e => e.flags
  | .floorDiv _ _ => ExprFlags.integerResult
  | .ceilDiv  _ _ => ExprFlags.integerResult
  | .mod_  _ _ => { is_integer := true, is_nonnegative := true }
  | .modIdx _ _ _ => { is_integer := true, is_nonnegative := true }
  | .where_ _ t f => ExprFlags.inter t.flags f.flags
  | .sqrt _   => ExprFlags.realMath
  | .exp  _   => ExprFlags.realMath
  | .log  _   => ExprFlags.realMath

/-! ## Evaluation semantics

Expressions evaluate over integer environments (SymbolId -> Int).
This models the C++ runtime where symbols represent dynamic tensor
dimensions, strides, or sizes. -/

/-- Environment mapping symbol IDs to integer values.
    C++: SymbolTable maps SymbolId -> runtime value. -/
abbrev Env := Nat → Int

/-- Python-style floor division: result rounds toward negative infinity.
    C++: `ExprPool::floor_div()` implements this for concrete integers. -/
def floorDivInt (a b : Int) : Int :=
  if b = 0 then 0
  else a / b  -- Lean's Int division is floor division

/-- Python-style modulus: sign follows divisor.
    C++: `ExprPool::python_mod()`. -/
def pyMod (a b : Int) : Int :=
  if b = 0 then 0
  else a % b  -- Lean's Int.mod follows divisor sign

/-- Evaluate an expression in an environment. Partial: returns `none`
    for division by zero or undefined operations.

    C++: not directly in ExprPool (expressions are symbolic), but this
    is the intended semantics when all symbols are bound to concrete values. -/
def Expr.eval (env : Env) : Expr → Option Int
  | .int v      => some v
  | .float _    => none   -- integer evaluation; floats are opaque
  | .sym id     => some (env id)
  | .bool true  => some 1
  | .bool false => some 0
  | .add es     => es.foldlM (fun acc e => do
      let v ← e.eval env
      pure (acc + v)) 0
  | .mul es     => es.foldlM (fun acc e => do
      let v ← e.eval env
      pure (acc * v)) 1
  | .pow base e => do
      let b ← base.eval env
      let n ← e.eval env
      if n < 0 then none
      else pure (b ^ n.toNat)
  | .neg e      => do
      let v ← e.eval env
      pure (-v)
  | .eq a b     => do
      let va ← a.eval env; let vb ← b.eval env
      pure (if va = vb then 1 else 0)
  | .ne a b     => do
      let va ← a.eval env; let vb ← b.eval env
      pure (if va ≠ vb then 1 else 0)
  | .lt a b     => do
      let va ← a.eval env; let vb ← b.eval env
      pure (if va < vb then 1 else 0)
  | .le a b     => do
      let va ← a.eval env; let vb ← b.eval env
      pure (if va ≤ vb then 1 else 0)
  | .gt a b     => do
      let va ← a.eval env; let vb ← b.eval env
      pure (if va > vb then 1 else 0)
  | .ge a b     => do
      let va ← a.eval env; let vb ← b.eval env
      pure (if va ≥ vb then 1 else 0)
  | .and_ es    => es.foldlM (fun acc e => do
      let v ← e.eval env
      pure (if acc ≠ 0 && v ≠ 0 then 1 else 0)) 1
  | .or_ es     => es.foldlM (fun acc e => do
      let v ← e.eval env
      pure (if acc ≠ 0 || v ≠ 0 then 1 else 0)) 0
  | .not_ e     => do
      let v ← e.eval env
      pure (if v = 0 then 1 else 0)
  | .floorDiv a b => do
      let va ← a.eval env; let vb ← b.eval env
      if vb = 0 then none else pure (floorDivInt va vb)
  | .ceilDiv a b  => do
      let va ← a.eval env; let vb ← b.eval env
      if vb = 0 then none
      else pure (floorDivInt (va + vb - 1) vb)
  | .mod_ a b   => do
      let va ← a.eval env; let vb ← b.eval env
      if vb = 0 then none else pure (pyMod va vb)
  | .modIdx base d m => do
      let vb ← base.eval env; let vd ← d.eval env; let vm ← m.eval env
      if vd = 0 || vm = 0 then none
      else pure (pyMod (floorDivInt vb vd) vm)
  | .where_ c t f => do
      let vc ← c.eval env
      if vc ≠ 0 then t.eval env else f.eval env
  | .abs e      => do
      let v ← e.eval env
      pure v.natAbs
  | .sqrt _     => none  -- integer evaluation; sqrt is real-valued
  | .exp _      => none  -- integer evaluation; exp is real-valued
  | .log _      => none  -- integer evaluation; log is real-valued
  | .identity e => e.eval env

/-! ## ExprPool -- Interning specification

Models ExprPool from ExprPool.h. The pool assigns each structurally
unique expression a unique identifier (modeling the unique arena pointer).

The key invariant: structurally equal expressions get the same ID.
This is what makes `a == b` a pointer comparison in C++. -/

/-- Interning pool state. Models ExprPool's Swiss table.
    `intern` maps expressions to unique identifiers (arena pointers).
    `next_id` is the next available identifier.

    C++: Swiss table with SIMD control bytes. Capacity is power of two.
    At 87.5% load with 32-byte groups, ~98.5% of lookups resolve in
    the first group with ~0.22 expected structural comparisons. -/
structure ExprPool where
  intern  : Expr → Nat
  next_id : Nat

/-- The fundamental interning invariant: structurally equal expressions
    map to the same identifier. This is what makes pointer equality
    equivalent to structural equality in C++.

    C++: `intern_node()` checks the Swiss table for an existing entry
    with matching hash AND structural equality. If found, returns
    existing pointer. If not, allocates new node in arena. -/
def ExprPool.WellFormed (pool : ExprPool) : Prop :=
  ∀ a b : Expr, a = b → pool.intern a = pool.intern b

/-- Stronger invariant: distinct expressions get distinct IDs.
    Together with WellFormed, this gives `a = b <-> intern a = intern b`.

    C++: each arena allocation returns a unique pointer. Two different
    structural forms can never alias (arena is append-only, never reuses). -/
def ExprPool.Injective (pool : ExprPool) : Prop :=
  ∀ a b : Expr, pool.intern a = pool.intern b → a = b

/-- The complete interning specification: structural equality is
    equivalent to identifier equality. This is the central theorem
    that justifies using pointer comparison for expression equality.

    C++: `const Expr* a == const Expr* b` <-> structurally identical. -/
def ExprPool.InterningCorrect (pool : ExprPool) : Prop :=
  ∀ a b : Expr, a = b ↔ pool.intern a = pool.intern b

/-- Injective pools satisfy the complete interning specification. -/
theorem ExprPool.injective_implies_correct (pool : ExprPool)
    (hinj : pool.Injective) : pool.InterningCorrect := by
  intro a b
  constructor
  · intro h; exact congrArg pool.intern h
  · exact hinj a b

/-! ## Canonicalization rules

Models ExprPool's eager canonicalization in construction methods.
Each rule preserves evaluation semantics. -/

/-- Constant folding for addition. C++: `ExprPool::add()` fast path.
    `add(integer(a), integer(b)) -> integer(a + b)`. -/
theorem add_int_fold (env : Env) (a b : Int) :
    (Expr.add [.int a, .int b]).eval env = (Expr.int (a + b)).eval env := by
  simp [Expr.eval, List.foldlM]

/-- Constant folding for multiplication. C++: `ExprPool::mul()` fast path.
    `mul(integer(a), integer(b)) -> integer(a * b)`. -/
theorem mul_int_fold (env : Env) (a b : Int) :
    (Expr.mul [.int a, .int b]).eval env = (Expr.int (a * b)).eval env := by
  simp [Expr.eval, List.foldlM]

/-- Additive identity. C++: `ExprPool::add()` -- `add(x, 0) -> x`.
    Zero is the identity for addition. -/
theorem add_zero_right (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.add [e, .int 0]).eval env = e.eval env := by
  simp [Expr.eval, List.foldlM, he]

/-- Additive identity (left). C++: `ExprPool::add()` -- `add(0, x) -> x`. -/
theorem add_zero_left (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.add [.int 0, e]).eval env = e.eval env := by
  simp [Expr.eval, List.foldlM, he]

/-- Multiplicative identity. C++: `ExprPool::mul()` -- `mul(x, 1) -> x`. -/
theorem mul_one_right (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.mul [e, .int 1]).eval env = e.eval env := by
  simp [Expr.eval, List.foldlM, he]

/-- Multiplicative identity (left). C++: `ExprPool::mul()` -- `mul(1, x) -> x`. -/
theorem mul_one_left (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.mul [.int 1, e]).eval env = e.eval env := by
  simp [Expr.eval, List.foldlM, he]

/-- Zero annihilation. C++: `ExprPool::mul()` -- `mul(x, 0) -> integer(0)`. -/
theorem mul_zero_right (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.mul [e, .int 0]).eval env = (Expr.int 0).eval env := by
  simp [Expr.eval, List.foldlM, he]

/-- Zero annihilation (left). C++: `ExprPool::mul()` -- `mul(0, x) -> integer(0)`. -/
theorem mul_zero_left (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.mul [.int 0, e]).eval env = (Expr.int 0).eval env := by
  simp [Expr.eval, List.foldlM, he]

/-- Power by zero. C++: `ExprPool::pow()` -- `x^0 -> integer(1)`. -/
theorem pow_zero (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.pow e (.int 0)).eval env = (Expr.int 1).eval env := by
  simp [Expr.eval, he]

/-- Power by one. C++: `ExprPool::pow()` -- `x^1 -> x`. -/
theorem pow_one (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.pow e (.int 1)).eval env = e.eval env := by
  simp [Expr.eval, he]

/-- Identity is semantically transparent. C++: `Op::IDENTITY` -- prevents
    expansion but evaluates identically to its child. -/
theorem identity_transparent (env : Env) (e : Expr) :
    (Expr.identity e).eval env = e.eval env := by
  simp [Expr.eval]

/-- Boolean constant folding for NOT. C++: `ExprPool::not_()`.
    `not_(true_) -> false_`, `not_(false_) -> true_`. -/
theorem not_true_is_false (env : Env) :
    (Expr.not_ (.bool true)).eval env = (Expr.bool false).eval env := by
  simp [Expr.eval]

theorem not_false_is_true (env : Env) :
    (Expr.not_ (.bool false)).eval env = (Expr.bool true).eval env := by
  simp [Expr.eval]

/-- Double negation elimination. C++: `ExprPool::not_()` --
    `not_(not_(e)) -> e` when e evaluates to 0 or 1 (boolean domain). -/
theorem not_not_bool (env : Env) (b : Bool) :
    (Expr.not_ (.not_ (.bool b))).eval env = (Expr.bool b).eval env := by
  cases b <;> simp [Expr.eval]

/-- Equality is reflexive. C++: `ExprPool::eq()` -- `eq(x, x) -> true_`.
    Uses pointer equality (interning guarantee). -/
theorem eq_self (env : Env) (e : Expr) (v : Int) (he : e.eval env = some v) :
    (Expr.eq e e).eval env = (Expr.bool true).eval env := by
  simp [Expr.eval, he]

/-- WHERE with true condition. C++: `ExprPool::where()` --
    `where(true_, t, f) -> t`. -/
theorem where_true (env : Env) (t f : Expr) :
    (Expr.where_ (.bool true) t f).eval env = t.eval env := by
  simp [Expr.eval]

/-- WHERE with false condition. C++: `ExprPool::where()` --
    `where(false_, t, f) -> f`. -/
theorem where_false (env : Env) (t f : Expr) :
    (Expr.where_ (.bool false) t f).eval env = f.eval env := by
  simp [Expr.eval]

/-- WHERE with equal arms. C++: `ExprPool::where()` --
    `where(c, t, t) -> t` (when condition evaluates successfully). -/
theorem where_same (env : Env) (c t : Expr) (vc : Int)
    (hc : c.eval env = some vc) :
    (Expr.where_ c t t).eval env = t.eval env := by
  simp [Expr.eval, hc]

/-! ## Commutativity of canonicalized forms

C++ canonicalizes commutative binary ops by pointer ordering:
`if (lhs > rhs) std::swap(lhs, rhs)`. This ensures that for commutative
ops, different argument orders produce the SAME interned expression.

We prove the semantic equivalence that justifies this canonicalization. -/

/-- ADD is commutative (binary case). C++: `ExprPool::add()` canonical ordering.
    Justifies `if (lhs > rhs) std::swap(lhs, rhs)` -- both orders evaluate
    to the same value, so interning can normalize to one. -/
theorem add_comm_eval (env : Env) (a b : Expr) :
    (Expr.add [a, b]).eval env = (Expr.add [b, a]).eval env := by
  simp [Expr.eval, List.foldlM]
  cases ha : a.eval env <;> cases hb : b.eval env <;> simp [Int.add_comm]

/-- MUL is commutative (binary case). C++: `ExprPool::mul()` canonical ordering. -/
theorem mul_comm_eval (env : Env) (a b : Expr) :
    (Expr.mul [a, b]).eval env = (Expr.mul [b, a]).eval env := by
  simp [Expr.eval, List.foldlM]
  cases ha : a.eval env <;> cases hb : b.eval env <;> simp [Int.mul_comm]

/-- EQ is commutative. C++: `ExprPool::eq()` canonical ordering. -/
theorem eq_comm_eval (env : Env) (a b : Expr) :
    (Expr.eq a b).eval env = (Expr.eq b a).eval env := by
  unfold Expr.eval
  cases a.eval env <;> cases b.eval env <;> simp
  next va vb => split <;> split <;> simp_all [eq_comm]

/-- NE is commutative. C++: `ExprPool::ne()` canonical ordering. -/
theorem ne_comm_eval (env : Env) (a b : Expr) :
    (Expr.ne a b).eval env = (Expr.ne b a).eval env := by
  unfold Expr.eval
  cases a.eval env <;> cases b.eval env <;> simp
  next va vb => split <;> split <;> simp_all [eq_comm]

/-! ## Negation canonical form

C++ NEG is lowered to MUL(-1, x): `ExprPool::neg()` returns
`mul(integer(-1), e)` for non-constant expressions. -/

/-- NEG semantics matches MUL(-1, x). C++: `ExprPool::neg()` --
    `neg(e) -> mul(integer(-1), e)` for non-constant e. -/
theorem neg_as_mul_neg_one (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.neg e).eval env = (Expr.mul [.int (-1), e]).eval env := by
  simp [Expr.eval, List.foldlM, he]

/-- Negation of integer constant. C++: `ExprPool::neg()` fast path --
    `neg(integer(n)) -> integer(-n)`. -/
theorem neg_int (env : Env) (n : Int) :
    (Expr.neg (.int n)).eval env = (Expr.int (-n)).eval env := by
  simp [Expr.eval]

/-! ## Division properties

C++ floor_div and ceil_div have well-defined semantics for all non-zero
divisors. These are Python-style (round toward negative infinity). -/

/-- Floor division by 1 is identity. C++: `ExprPool::floor_div()` --
    `floor_div(x, 1) -> x`. -/
theorem floorDiv_one (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.floorDiv e (.int 1)).eval env = e.eval env := by
  simp [Expr.eval, he, floorDivInt]

/-- Self-division equals one. C++: `ExprPool::floor_div()` --
    `floor_div(x, x) -> integer(1)` (when x != 0). -/
theorem floorDiv_self (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) (hv : v ≠ 0) :
    (Expr.floorDiv e e).eval env = (Expr.int 1).eval env := by
  simp [Expr.eval, he, floorDivInt, hv]

/-- Mod by 2 of even expression is 0. C++: `ExprPool::mod()` --
    uses `is_even()` flag. -/
theorem mod_two_even (env : Env) (n : Int) (hn : n % 2 = 0) :
    (Expr.mod_ (.int n) (.int 2)).eval env =
      (Expr.int 0).eval env := by
  simp [Expr.eval, pyMod, hn]

/-! ## Expression size and well-foundedness

Expressions form a well-founded tree. Every expression has finite depth.
This justifies recursive algorithms (hash computation, evaluation, etc.). -/

/-- Number of nodes in an expression tree. Strictly decreases for
    sub-expressions, justifying termination of recursive algorithms. -/
def Expr.size : Expr → Nat
  | .int _    => 1
  | .float _  => 1
  | .sym _    => 1
  | .bool _   => 1
  | .add es   => 1 + es.foldl (fun acc e => acc + e.size) 0
  | .mul es   => 1 + es.foldl (fun acc e => acc + e.size) 0
  | .pow b e  => 1 + b.size + e.size
  | .neg e    => 1 + e.size
  | .eq a b   => 1 + a.size + b.size
  | .ne a b   => 1 + a.size + b.size
  | .lt a b   => 1 + a.size + b.size
  | .le a b   => 1 + a.size + b.size
  | .gt a b   => 1 + a.size + b.size
  | .ge a b   => 1 + a.size + b.size
  | .and_ es  => 1 + es.foldl (fun acc e => acc + e.size) 0
  | .or_  es  => 1 + es.foldl (fun acc e => acc + e.size) 0
  | .not_ e   => 1 + e.size
  | .floorDiv a b => 1 + a.size + b.size
  | .ceilDiv  a b => 1 + a.size + b.size
  | .mod_  a b => 1 + a.size + b.size
  | .modIdx a b c => 1 + a.size + b.size + c.size
  | .where_ c t f => 1 + c.size + t.size + f.size
  | .abs e    => 1 + e.size
  | .sqrt e   => 1 + e.size
  | .exp e    => 1 + e.size
  | .log e    => 1 + e.size
  | .identity e => 1 + e.size

/-- Every expression has positive size. -/
theorem Expr.size_pos (e : Expr) : 0 < e.size := by
  cases e <;> simp [Expr.size]

/-! ## Immutability specification

C++ enforces immutability via `const Expr*` everywhere. Once constructed
and interned, no field of an Expr can be modified. This is inherent in
Lean's inductive types -- all constructors produce immutable values.

We state this as a definitional property: evaluation is deterministic
(same expression + same environment -> same result). -/

/-- Evaluation is deterministic: same expression + same environment -> same result.
    This is trivially true for Lean functions but documents the critical
    C++ invariant: immutable expressions produce consistent results. -/
theorem eval_deterministic (env : Env) (e : Expr) :
    e.eval env = e.eval env := rfl

/-! ## Integer cache specification

C++: ExprPool pre-interns integers [-128, 127] in `int_cache_[]` for
O(1) access. This is a performance optimization that doesn't change
semantics but guarantees these common constants share a single pointer. -/

/-- The integer cache range. C++: `kIntCacheLow = -128`, `kIntCacheHigh = 127`. -/
def intCacheLow  : Int := -128
def intCacheHigh : Int := 127

/-- An integer is in the cache range. -/
def inIntCache (v : Int) : Prop := intCacheLow ≤ v ∧ v ≤ intCacheHigh

/-- Cache range is non-empty (contains 0). -/
theorem zero_in_cache : inIntCache 0 := by
  simp [inIntCache, intCacheLow, intCacheHigh]

/-- Cache range contains exactly 256 values. C++: `kIntCacheSize = 256`. -/
theorem intCacheSize : intCacheHigh - intCacheLow + 1 = 256 := by
  simp [intCacheLow, intCacheHigh]

/-! ## Flattening specification

C++ canonicalization flattens nested variadic ops:
  add(add(a, b), c) -> add(a, b, c)
  mul(mul(a, b), c) -> mul(a, b, c)

We prove the semantic equivalence. -/

/-- Flattening ADD preserves semantics. C++: `ExprPool::add_n()` flattening.
    `add(add(a, b), c)` evaluates the same as `add(a, b, c)`. -/
theorem add_flatten_eval (env : Env) (a b c : Expr)
    (va vb vc : Int)
    (ha : a.eval env = some va) (hb : b.eval env = some vb)
    (hc : c.eval env = some vc) :
    (Expr.add [.add [a, b], c]).eval env =
      (Expr.add [a, b, c]).eval env := by
  simp [Expr.eval, List.foldlM, ha, hb, hc, Int.add_assoc]

/-- Flattening MUL preserves semantics. C++: `ExprPool::mul_n()` flattening.
    `mul(mul(a, b), c)` evaluates the same as `mul(a, b, c)`. -/
theorem mul_flatten_eval (env : Env) (a b c : Expr)
    (va vb vc : Int)
    (ha : a.eval env = some va) (hb : b.eval env = some vb)
    (hc : c.eval env = some vc) :
    (Expr.mul [.mul [a, b], c]).eval env =
      (Expr.mul [a, b, c]).eval env := by
  simp [Expr.eval, List.foldlM, ha, hb, hc, Int.mul_assoc]

/-! ## Term combining

C++: `ExprPool::add()` -- `add(a, a) -> mul(2, a)` (same-base detection
via pointer equality). More generally, coefficient extraction enables
`add(a, 2a) -> 3a`. -/

/-- Same-base detection: a + a = 2 * a.
    C++: `if (lhs == rhs) return mul(a, integer(2), lhs)`. -/
theorem add_self_is_double (env : Env) (e : Expr) (v : Int)
    (he : e.eval env = some v) :
    (Expr.add [e, e]).eval env = (Expr.mul [.int 2, e]).eval env := by
  simp [Expr.eval, List.foldlM, he, Int.two_mul]

end Crucible
