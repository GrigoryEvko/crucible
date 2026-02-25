import Mathlib.Data.Fin.Basic
import Mathlib.Tactic

/-!
# Crucible.Ops -- Symbolic Expression Operations

Models `Ops.h`: the 60 symbolic ops used in the Graph IR (`Expr.h`)
for shape/dimension computation. These are NOT compute kernels (those
are `CKernel.h`). These are the node types of an interned expression
DAG where shapes like `batch * (hidden + 1)` are represented as trees
of `Op` nodes.

Two orthogonal systems in Crucible:
- `Ops.h` = symbolic math on dimensions (modeled here)
- `CKernel.h` = device-agnostic compute kernel identity

## C++ design

```cpp
enum class Op : uint8_t { INTEGER, FLOAT, ..., NEG, NUM_OPS };
```

60 named ops (ordinals 0..59), `NUM_OPS = 60` is the sentinel.
Each op has a fixed arity (atoms=0, unary=1, binary=2, ternary=3)
except variadic ops (ADD, MUL, AND, OR, MIN, MAX,
IS_NON_OVERLAPPING_AND_DENSE) which take N >= 2 children.

## Key properties formalized

- **Arity classification**: every op has a well-defined arity category
- **Result type**: every op produces integer, real, or boolean output
  (from `composite_flags` in `ExprPool.h`)
- **Commutativity**: ADD, MUL, EQ, NE, AND, OR, MIN, MAX,
  BITWISE_AND, BITWISE_OR, BITWISE_XOR are commutative
- **Associativity**: ADD, MUL, AND, OR, MIN, MAX are associative
  (this is WHY they can be variadic with flattening)
- **Ordinal injectivity**: each op has a distinct `uint8_t` ordinal
- **Decidable equality**: ops are a finite type with decidable eq
-/

namespace Crucible

/-! ## Op enumeration

Every expression node type in the symbolic math engine.
Mirrors `enum class Op : uint8_t` from Ops.h.
Organized by category, preserving C++ declaration order. -/

/-- Symbolic expression operation. C++: `enum class Op : uint8_t`.
    60 ops for shape/dimension symbolic computation.
    Each op is a node type in the interned expression DAG. -/
inductive Op where
  -- Atoms (nargs=0): leaf nodes with payload
  | INTEGER           -- payload: int64_t literal
  | FLOAT             -- payload: double (bitcast to int64_t)
  | SYMBOL            -- payload: symbol_id (index into SymbolTable)
  | BOOL_TRUE         -- no payload
  | BOOL_FALSE        -- no payload
  -- Arithmetic (binary or variadic)
  | ADD               -- variadic, flattened, sorted (commutative + associative)
  | MUL               -- variadic, flattened, sorted (commutative + associative)
  | POW               -- binary: base^exp
  -- Relational (binary, always boolean result)
  | EQ                -- equal
  | NE                -- not equal
  | LT                -- less than
  | LE                -- less or equal
  | GT                -- greater than
  | GE                -- greater or equal
  -- Logic (variadic/unary, always boolean result)
  | AND               -- variadic (commutative + associative)
  | OR                -- variadic (commutative + associative)
  | NOT               -- unary
  -- Division / Modular (binary except MODULAR_INDEXING=ternary)
  | FLOOR_DIV         -- base // divisor
  | CLEAN_DIV         -- floor_div where no rounding occurs
  | CEIL_DIV          -- ceil(base / divisor)
  | INT_TRUE_DIV      -- integer true division (result is float)
  | FLOAT_TRUE_DIV    -- float true division
  | MOD               -- nonneg modulus
  | PYTHON_MOD        -- Python-style modulus (sign follows divisor)
  | MODULAR_INDEXING  -- ternary: (base // divisor) % modulus
  -- Rounding / Type conversion (unary except ROUND_DECIMAL=binary)
  | CEIL_TO_INT
  | FLOOR_TO_INT
  | TRUNC_TO_FLOAT
  | TRUNC_TO_INT
  | ROUND_TO_INT
  | ROUND_DECIMAL     -- binary: (number, ndigits)
  | TO_FLOAT
  -- Shift (binary)
  | LSHIFT
  | RSHIFT
  -- Power variants (binary)
  | POW_BY_NATURAL    -- integer power (always integer result)
  | FLOAT_POW         -- float power (always real result)
  -- Control flow (ternary)
  | WHERE             -- (cond, true_val, false_val)
  -- Identity (unary)
  | IDENTITY          -- prevents expansion, semantically transparent
  -- Min / Max (variadic, commutative + associative)
  | MIN
  | MAX
  -- Indicator (variadic)
  | IS_NON_OVERLAPPING_AND_DENSE
  -- Opaque unary math functions
  | SQRT | COS | COSH | SIN | SINH | TAN | TANH
  | ASIN | ACOS | ATAN | EXP | LOG | ASINH | LOG2
  -- Unary math
  | ABS
  -- Bitwise (binary, commutative)
  | BITWISE_AND
  | BITWISE_OR
  | BITWISE_XOR
  -- Negation (unary, internal)
  | NEG
  deriving DecidableEq, Repr, Inhabited

/-! ## Arity

C++: `Expr::nargs` (uint8_t). Atoms have 0 children, unary ops have 1,
binary 2, ternary 3. Variadic ops (ADD, MUL, AND, OR, MIN, MAX,
IS_NON_OVERLAPPING_AND_DENSE) accept N >= 2 children.
ExprPool flattens and sorts variadic ops at construction time. -/

/-- Arity category for symbolic ops.
    C++ stores nargs per-node; we classify per-op. -/
inductive Arity where
  | atom                     -- nargs=0: INTEGER, FLOAT, SYMBOL, BOOL_TRUE/FALSE
  | unary                    -- nargs=1: NOT, rounding, trig, ABS, NEG, IDENTITY
  | binary                   -- nargs=2: POW, relational, division, shift, bitwise
  | ternary                  -- nargs=3: WHERE, MODULAR_INDEXING
  | variadic (minArgs : Nat) -- nargs>=minArgs: ADD, MUL, AND, OR, MIN, MAX
  deriving DecidableEq, Repr

/-- Classify each op by its arity.
    Matches C++ ExprPool construction methods (which enforce nargs). -/
def Op.arity : Op -> Arity
  -- Atoms
  | .INTEGER | .FLOAT | .SYMBOL | .BOOL_TRUE | .BOOL_FALSE => .atom
  -- Variadic (C++: flattened, sorted, min 2 children)
  | .ADD | .MUL | .AND | .OR | .MIN | .MAX => .variadic 2
  | .IS_NON_OVERLAPPING_AND_DENSE => .variadic 1
  -- Unary
  | .NOT | .IDENTITY | .NEG | .ABS => .unary
  | .CEIL_TO_INT | .FLOOR_TO_INT | .TRUNC_TO_FLOAT | .TRUNC_TO_INT => .unary
  | .ROUND_TO_INT | .TO_FLOAT => .unary
  | .SQRT | .COS | .COSH | .SIN | .SINH | .TAN | .TANH => .unary
  | .ASIN | .ACOS | .ATAN | .EXP | .LOG | .ASINH | .LOG2 => .unary
  -- Binary
  | .POW | .EQ | .NE | .LT | .LE | .GT | .GE => .binary
  | .FLOOR_DIV | .CLEAN_DIV | .CEIL_DIV => .binary
  | .INT_TRUE_DIV | .FLOAT_TRUE_DIV | .MOD | .PYTHON_MOD => .binary
  | .ROUND_DECIMAL => .binary
  | .LSHIFT | .RSHIFT => .binary
  | .POW_BY_NATURAL | .FLOAT_POW => .binary
  | .BITWISE_AND | .BITWISE_OR | .BITWISE_XOR => .binary
  -- Ternary
  | .WHERE | .MODULAR_INDEXING => .ternary

/-! ## Result type classification

From `composite_flags` in ExprPool.h: every composite expression has
a well-defined result type category. This determines which ExprFlags
bits are set on the output node. -/

/-- Result type category of a symbolic expression.
    C++: determined by `composite_flags()` in ExprPool.h. -/
inductive ResultType where
  | integer   -- IS_INTEGER | IS_REAL | IS_FINITE | IS_NUMBER
  | real      -- IS_REAL | IS_FINITE | IS_NUMBER (float result)
  | boolean   -- IS_BOOLEAN
  | inherited -- result type depends on children (ADD, MUL, POW, WHERE, etc.)
  | none      -- atoms (type from payload, not from op alone)
  deriving DecidableEq, Repr

/-- Classify each op's result type.
    Mirrors the switch statement in `composite_flags()`.
    Atoms get `.none` because their type comes from payload/flags. -/
def Op.resultType : Op -> ResultType
  -- Atoms: type is payload-dependent, not op-dependent
  | .INTEGER | .FLOAT | .SYMBOL | .BOOL_TRUE | .BOOL_FALSE => .none
  -- Always integer result (from composite_flags)
  | .FLOOR_DIV | .CLEAN_DIV | .CEIL_DIV => .integer
  | .MOD | .PYTHON_MOD | .MODULAR_INDEXING => .integer
  | .LSHIFT | .RSHIFT => .integer
  | .CEIL_TO_INT | .FLOOR_TO_INT | .TRUNC_TO_INT | .ROUND_TO_INT => .integer
  | .BITWISE_AND | .BITWISE_OR | .BITWISE_XOR => .integer
  | .POW_BY_NATURAL => .integer
  | .IS_NON_OVERLAPPING_AND_DENSE => .integer
  -- Always real result (float)
  | .INT_TRUE_DIV | .FLOAT_TRUE_DIV => .real
  | .TO_FLOAT | .TRUNC_TO_FLOAT | .FLOAT_POW | .ROUND_DECIMAL => .real
  | .SQRT | .COS | .COSH | .SIN | .SINH | .TAN | .TANH => .real
  | .ASIN | .ACOS | .ATAN | .EXP | .LOG | .ASINH | .LOG2 => .real
  -- Always boolean result
  | .EQ | .NE | .LT | .LE | .GT | .GE => .boolean
  | .AND | .OR | .NOT => .boolean
  -- Inherited from children
  | .ADD | .MUL | .MIN | .MAX => .inherited
  | .POW | .WHERE | .IDENTITY | .NEG | .ABS => .inherited

/-! ## Commutativity

Variadic ops that are "flattened, sorted" in C++ are commutative.
EQ, NE are symmetric. Bitwise AND/OR/XOR are commutative.
This property justifies the canonical sorting in ExprPool.

We model binary commutativity as: swapping the two operands
produces the same semantic result. For variadic ops, any
permutation of the argument list produces the same result. -/

/-- Predicate: op is commutative (operand order does not affect result).
    C++: ExprPool sorts children of commutative variadic ops
    for canonical form (identity-based interning requires this). -/
def Op.isCommutative : Op -> Prop
  | .ADD | .MUL => True
  | .EQ | .NE => True
  | .AND | .OR => True
  | .MIN | .MAX => True
  | .BITWISE_AND | .BITWISE_OR | .BITWISE_XOR => True
  | _ => False

instance (op : Op) : Decidable op.isCommutative := by
  unfold Op.isCommutative; cases op <;> exact inferInstance

/-- Predicate: op is associative (grouping does not affect result).
    Combined with commutativity, this justifies variadic flattening:
    `add(add(a, b), c)` flattens to `add(a, b, c)` because
    `(a + b) + c = a + (b + c)`. -/
def Op.isAssociative : Op -> Prop
  | .ADD | .MUL => True
  | .AND | .OR => True
  | .MIN | .MAX => True
  | .BITWISE_AND | .BITWISE_OR | .BITWISE_XOR => True
  | _ => False

instance (op : Op) : Decidable op.isAssociative := by
  unfold Op.isAssociative; cases op <;> exact inferInstance

/-- Predicate: op is variadic (accepts N >= minArgs children).
    Variadic ops are flattened at construction: `add(add(a,b),c) -> add(a,b,c)`.
    This is sound because all variadic ops are associative. -/
def Op.isVariadic : Op -> Prop
  | .ADD | .MUL | .AND | .OR | .MIN | .MAX => True
  | .IS_NON_OVERLAPPING_AND_DENSE => True
  | _ => False

instance (op : Op) : Decidable op.isVariadic := by
  unfold Op.isVariadic; cases op <;> exact inferInstance

/-! ## Ordinals

C++: `enum class Op : uint8_t` assigns consecutive ordinals 0..59.
`NUM_OPS = 60` is the sentinel (not modeled as an Op constructor).
The ordinal function is injective (no two ops share an ordinal). -/

/-- Map each op to its C++ ordinal (uint8_t value, range 0..59).
    Mirrors the implicit numbering from `enum class Op : uint8_t`.
    C++: `NUM_OPS = 60` is the sentinel. -/
def Op.toOrdinal : Op -> Fin 60
  | .INTEGER       => ⟨ 0, by omega⟩
  | .FLOAT         => ⟨ 1, by omega⟩
  | .SYMBOL        => ⟨ 2, by omega⟩
  | .BOOL_TRUE     => ⟨ 3, by omega⟩
  | .BOOL_FALSE    => ⟨ 4, by omega⟩
  | .ADD           => ⟨ 5, by omega⟩
  | .MUL           => ⟨ 6, by omega⟩
  | .POW           => ⟨ 7, by omega⟩
  | .EQ            => ⟨ 8, by omega⟩
  | .NE            => ⟨ 9, by omega⟩
  | .LT            => ⟨10, by omega⟩
  | .LE            => ⟨11, by omega⟩
  | .GT            => ⟨12, by omega⟩
  | .GE            => ⟨13, by omega⟩
  | .AND           => ⟨14, by omega⟩
  | .OR            => ⟨15, by omega⟩
  | .NOT           => ⟨16, by omega⟩
  | .FLOOR_DIV     => ⟨17, by omega⟩
  | .CLEAN_DIV     => ⟨18, by omega⟩
  | .CEIL_DIV      => ⟨19, by omega⟩
  | .INT_TRUE_DIV  => ⟨20, by omega⟩
  | .FLOAT_TRUE_DIV => ⟨21, by omega⟩
  | .MOD           => ⟨22, by omega⟩
  | .PYTHON_MOD    => ⟨23, by omega⟩
  | .MODULAR_INDEXING => ⟨24, by omega⟩
  | .CEIL_TO_INT   => ⟨25, by omega⟩
  | .FLOOR_TO_INT  => ⟨26, by omega⟩
  | .TRUNC_TO_FLOAT => ⟨27, by omega⟩
  | .TRUNC_TO_INT  => ⟨28, by omega⟩
  | .ROUND_TO_INT  => ⟨29, by omega⟩
  | .ROUND_DECIMAL => ⟨30, by omega⟩
  | .TO_FLOAT      => ⟨31, by omega⟩
  | .LSHIFT        => ⟨32, by omega⟩
  | .RSHIFT        => ⟨33, by omega⟩
  | .POW_BY_NATURAL => ⟨34, by omega⟩
  | .FLOAT_POW     => ⟨35, by omega⟩
  | .WHERE         => ⟨36, by omega⟩
  | .IDENTITY      => ⟨37, by omega⟩
  | .MIN           => ⟨38, by omega⟩
  | .MAX           => ⟨39, by omega⟩
  | .IS_NON_OVERLAPPING_AND_DENSE => ⟨40, by omega⟩
  | .SQRT          => ⟨41, by omega⟩
  | .COS           => ⟨42, by omega⟩
  | .COSH          => ⟨43, by omega⟩
  | .SIN           => ⟨44, by omega⟩
  | .SINH          => ⟨45, by omega⟩
  | .TAN           => ⟨46, by omega⟩
  | .TANH          => ⟨47, by omega⟩
  | .ASIN          => ⟨48, by omega⟩
  | .ACOS          => ⟨49, by omega⟩
  | .ATAN          => ⟨50, by omega⟩
  | .EXP           => ⟨51, by omega⟩
  | .LOG           => ⟨52, by omega⟩
  | .ASINH         => ⟨53, by omega⟩
  | .LOG2          => ⟨54, by omega⟩
  | .ABS           => ⟨55, by omega⟩
  | .BITWISE_AND   => ⟨56, by omega⟩
  | .BITWISE_OR    => ⟨57, by omega⟩
  | .BITWISE_XOR   => ⟨58, by omega⟩
  | .NEG           => ⟨59, by omega⟩

/-- Inverse: ordinal back to Op. Total because all 60 values map to an Op. -/
def Op.fromOrdinal : Fin 60 -> Op
  | ⟨ 0, _⟩ => .INTEGER
  | ⟨ 1, _⟩ => .FLOAT
  | ⟨ 2, _⟩ => .SYMBOL
  | ⟨ 3, _⟩ => .BOOL_TRUE
  | ⟨ 4, _⟩ => .BOOL_FALSE
  | ⟨ 5, _⟩ => .ADD
  | ⟨ 6, _⟩ => .MUL
  | ⟨ 7, _⟩ => .POW
  | ⟨ 8, _⟩ => .EQ
  | ⟨ 9, _⟩ => .NE
  | ⟨10, _⟩ => .LT
  | ⟨11, _⟩ => .LE
  | ⟨12, _⟩ => .GT
  | ⟨13, _⟩ => .GE
  | ⟨14, _⟩ => .AND
  | ⟨15, _⟩ => .OR
  | ⟨16, _⟩ => .NOT
  | ⟨17, _⟩ => .FLOOR_DIV
  | ⟨18, _⟩ => .CLEAN_DIV
  | ⟨19, _⟩ => .CEIL_DIV
  | ⟨20, _⟩ => .INT_TRUE_DIV
  | ⟨21, _⟩ => .FLOAT_TRUE_DIV
  | ⟨22, _⟩ => .MOD
  | ⟨23, _⟩ => .PYTHON_MOD
  | ⟨24, _⟩ => .MODULAR_INDEXING
  | ⟨25, _⟩ => .CEIL_TO_INT
  | ⟨26, _⟩ => .FLOOR_TO_INT
  | ⟨27, _⟩ => .TRUNC_TO_FLOAT
  | ⟨28, _⟩ => .TRUNC_TO_INT
  | ⟨29, _⟩ => .ROUND_TO_INT
  | ⟨30, _⟩ => .ROUND_DECIMAL
  | ⟨31, _⟩ => .TO_FLOAT
  | ⟨32, _⟩ => .LSHIFT
  | ⟨33, _⟩ => .RSHIFT
  | ⟨34, _⟩ => .POW_BY_NATURAL
  | ⟨35, _⟩ => .FLOAT_POW
  | ⟨36, _⟩ => .WHERE
  | ⟨37, _⟩ => .IDENTITY
  | ⟨38, _⟩ => .MIN
  | ⟨39, _⟩ => .MAX
  | ⟨40, _⟩ => .IS_NON_OVERLAPPING_AND_DENSE
  | ⟨41, _⟩ => .SQRT
  | ⟨42, _⟩ => .COS
  | ⟨43, _⟩ => .COSH
  | ⟨44, _⟩ => .SIN
  | ⟨45, _⟩ => .SINH
  | ⟨46, _⟩ => .TAN
  | ⟨47, _⟩ => .TANH
  | ⟨48, _⟩ => .ASIN
  | ⟨49, _⟩ => .ACOS
  | ⟨50, _⟩ => .ATAN
  | ⟨51, _⟩ => .EXP
  | ⟨52, _⟩ => .LOG
  | ⟨53, _⟩ => .ASINH
  | ⟨54, _⟩ => .LOG2
  | ⟨55, _⟩ => .ABS
  | ⟨56, _⟩ => .BITWISE_AND
  | ⟨57, _⟩ => .BITWISE_OR
  | ⟨58, _⟩ => .BITWISE_XOR
  | ⟨59, _⟩ => .NEG
  | ⟨n + 60, h⟩ => absurd h (by omega)

/-! ## Ordinal roundtrip and injectivity -/

/-- fromOrdinal is a left inverse of toOrdinal.
    C++: casting from Op to uint8_t and back is identity. -/
theorem Op.fromOrdinal_toOrdinal (op : Op) :
    Op.fromOrdinal (Op.toOrdinal op) = op := by
  cases op <;> rfl

/-- toOrdinal is a left inverse of fromOrdinal.
    Every valid ordinal roundtrips. -/
theorem Op.toOrdinal_fromOrdinal (n : Fin 60) :
    Op.toOrdinal (Op.fromOrdinal n) = n := by
  refine Fin.ext ?_
  fin_cases n <;> rfl

/-- toOrdinal is injective: distinct ops have distinct ordinals.
    C++: `enum class` guarantees this for the underlying uint8_t. -/
theorem Op.toOrdinal_injective (a b : Op) (h : a.toOrdinal = b.toOrdinal) :
    a = b := by
  have := congrArg Op.fromOrdinal h
  rwa [Op.fromOrdinal_toOrdinal, Op.fromOrdinal_toOrdinal] at this

/-! ## ExprFlags -- Type/assumption property bits

C++: `struct ExprFlags` with 13 bit flags (uint16_t).
Used for O(1) queries on expression nodes. The flags form a
lattice under bitwise AND (intersection of properties). -/

/-- Expression property flag. C++: `ExprFlags` bit positions.
    Each flag is a boolean property of an expression node. -/
inductive ExprFlag where
  | IS_INTEGER     -- value is an integer
  | IS_REAL        -- value is a real number
  | IS_FINITE      -- value is finite (not inf/nan)
  | IS_POSITIVE    -- value > 0
  | IS_NEGATIVE    -- value < 0
  | IS_NONNEGATIVE -- value >= 0
  | IS_NONPOSITIVE -- value <= 0
  | IS_ZERO        -- value = 0
  | IS_EVEN        -- integer value is even
  | IS_ODD         -- integer value is odd
  | IS_NUMBER      -- is a concrete number (not symbolic)
  | IS_SYMBOL      -- is a symbolic variable
  | IS_BOOLEAN     -- is a boolean value
  deriving DecidableEq, Repr

/-- Map each flag to its C++ bit position (1 << n). -/
def ExprFlag.bitPosition : ExprFlag -> Fin 13
  | .IS_INTEGER     => ⟨ 0, by omega⟩
  | .IS_REAL        => ⟨ 1, by omega⟩
  | .IS_FINITE      => ⟨ 2, by omega⟩
  | .IS_POSITIVE    => ⟨ 3, by omega⟩
  | .IS_NEGATIVE    => ⟨ 4, by omega⟩
  | .IS_NONNEGATIVE => ⟨ 5, by omega⟩
  | .IS_NONPOSITIVE => ⟨ 6, by omega⟩
  | .IS_ZERO        => ⟨ 7, by omega⟩
  | .IS_EVEN        => ⟨ 8, by omega⟩
  | .IS_ODD         => ⟨ 9, by omega⟩
  | .IS_NUMBER      => ⟨10, by omega⟩
  | .IS_SYMBOL      => ⟨11, by omega⟩
  | .IS_BOOLEAN     => ⟨12, by omega⟩

/-- Flag bit positions are injective (no two flags share a bit). -/
theorem ExprFlag.bitPosition_injective (a b : ExprFlag)
    (h : a.bitPosition = b.bitPosition) : a = b := by
  cases a <;> cases b <;> simp [bitPosition, Fin.ext_iff] at h <;> rfl

/-! ## Algebraic properties

The key structural properties that justify ExprPool's normalization.
Variadic ops are flattened because they are associative.
Variadic ops are sorted because they are commutative.
These two properties together make the canonical form unique. -/

/-- The six variadic ops that are both commutative and associative.
    These are the ops that ExprPool flattens AND sorts. -/
def Op.isCommAssocVariadic : Op -> Prop
  | .ADD | .MUL | .AND | .OR | .MIN | .MAX => True
  | _ => False

instance (op : Op) : Decidable op.isCommAssocVariadic := by
  unfold Op.isCommAssocVariadic; cases op <;> exact inferInstance

/-- Every commutative-associative variadic op is variadic. -/
theorem commAssocVariadic_implies_variadic (op : Op)
    (h : op.isCommAssocVariadic) : op.isVariadic := by
  cases op <;> simp [Op.isCommAssocVariadic] at h <;> simp [Op.isVariadic]

/-- Every commutative-associative variadic op is commutative. -/
theorem commAssocVariadic_implies_commutative (op : Op)
    (h : op.isCommAssocVariadic) : op.isCommutative := by
  cases op <;> simp [Op.isCommAssocVariadic] at h <;> simp [Op.isCommutative]

/-- Every commutative-associative variadic op is associative. -/
theorem commAssocVariadic_implies_associative (op : Op)
    (h : op.isCommAssocVariadic) : op.isAssociative := by
  cases op <;> simp [Op.isCommAssocVariadic] at h <;> simp [Op.isAssociative]

/-- Every associative op is also commutative.
    (True in this system because all our associative ops happen to be
    commutative -- ADD, MUL, AND, OR, MIN, MAX, BITWISE_AND/OR/XOR.) -/
theorem associative_implies_commutative (op : Op) (ha : op.isAssociative) :
    op.isCommutative := by
  cases op <;> simp [Op.isAssociative] at ha <;> simp [Op.isCommutative]

/-- Atoms have no children. C++: `Expr::is_atom() { return nargs == 0; }` -/
theorem atom_arity_zero (op : Op) (h : op.arity = .atom) :
    op ∈ [.INTEGER, .FLOAT, .SYMBOL, .BOOL_TRUE, .BOOL_FALSE] := by
  cases op <;> simp [Op.arity] at h <;> simp

/-- Boolean-result ops are exactly the relational and logical ops. -/
theorem boolean_result_ops (op : Op) (h : op.resultType = .boolean) :
    op ∈ [.EQ, .NE, .LT, .LE, .GT, .GE, .AND, .OR, .NOT] := by
  cases op <;> simp [Op.resultType] at h <;> simp

/-! ## Semantic interpretation (integer fragment)

We model the semantic meaning of ops over integers to state and
prove algebraic laws. This is not a full evaluator (floats, symbols,
and booleans are out of scope). It captures the integer fragment
that dominates shape computation.

We use `Int` for the value domain (matching C++ `int64_t` payload),
but restrict bitwise ops to `Nat` since `Int` bitwise ops in Lean
require care. -/

/-- Binary integer semantics for the arithmetic/division fragment.
    Models the integer-valued ops used in shape expressions.
    Partial: returns `none` for division by zero or non-integer ops. -/
def Op.evalIntBin (op : Op) (a b : Int) : Option Int :=
  match op with
  | .ADD => some (a + b)
  | .MUL => some (a * b)
  | .FLOOR_DIV => if b = 0 then none else some (a / b)
  | .CEIL_DIV => if b = 0 then none else some (-((-a) / b))
  | .MOD => if b = 0 then none else some (a % b)
  | .POW_BY_NATURAL => if b < 0 then none else some (a ^ b.toNat)
  | .LSHIFT => if b < 0 then none else some (a * 2 ^ b.toNat)
  | .RSHIFT => if b < 0 then none else some (a / 2 ^ b.toNat)
  | _ => none

/-- ADD is commutative over integers. -/
theorem add_comm_int (a b : Int) :
    Op.evalIntBin .ADD a b = Op.evalIntBin .ADD b a := by
  simp [Op.evalIntBin, Int.add_comm]

/-- MUL is commutative over integers. -/
theorem mul_comm_int (a b : Int) :
    Op.evalIntBin .MUL a b = Op.evalIntBin .MUL b a := by
  simp [Op.evalIntBin, Int.mul_comm]

/-- ADD is associative over integers. -/
theorem add_assoc_int (a b c : Int) :
    (Op.evalIntBin .ADD a b).bind (Op.evalIntBin .ADD · c) =
    (Op.evalIntBin .ADD b c).bind (Op.evalIntBin .ADD a ·) := by
  simp [Op.evalIntBin, Int.add_assoc]

/-- MUL is associative over integers. -/
theorem mul_assoc_int (a b c : Int) :
    (Op.evalIntBin .MUL a b).bind (Op.evalIntBin .MUL · c) =
    (Op.evalIntBin .MUL b c).bind (Op.evalIntBin .MUL a ·) := by
  simp [Op.evalIntBin, Int.mul_assoc]

/-- ADD identity: a + 0 = a. C++: `add(x, 0) -> x` in ExprPool. -/
theorem add_zero_int (a : Int) :
    Op.evalIntBin .ADD a 0 = some a := by
  simp [Op.evalIntBin]

/-- MUL identity: a * 1 = a. C++: `mul(x, 1) -> x` in ExprPool. -/
theorem mul_one_int (a : Int) :
    Op.evalIntBin .MUL a 1 = some a := by
  simp [Op.evalIntBin]

/-- MUL absorbing: a * 0 = 0. C++: `mul(x, 0) -> integer(0)` in ExprPool. -/
theorem mul_zero_int (a : Int) :
    Op.evalIntBin .MUL a 0 = some 0 := by
  simp [Op.evalIntBin]

/-! ## Bitwise commutativity over Nat

Bitwise ops in the C++ Ops.h operate on integers. We prove
commutativity over Nat where the Lean API is cleanest. -/

/-- Binary Nat semantics for bitwise ops. -/
def Op.evalNatBitwise (op : Op) (a b : Nat) : Option Nat :=
  match op with
  | .BITWISE_AND => some (a &&& b)
  | .BITWISE_OR  => some (a ||| b)
  | .BITWISE_XOR => some (a ^^^ b)
  | _ => none

/-- BITWISE_AND is commutative over Nat. -/
theorem bitwise_and_comm_nat (a b : Nat) :
    Op.evalNatBitwise .BITWISE_AND a b =
    Op.evalNatBitwise .BITWISE_AND b a := by
  simp [Op.evalNatBitwise, Nat.and_comm]

/-- BITWISE_OR is commutative over Nat. -/
theorem bitwise_or_comm_nat (a b : Nat) :
    Op.evalNatBitwise .BITWISE_OR a b =
    Op.evalNatBitwise .BITWISE_OR b a := by
  simp [Op.evalNatBitwise, Nat.or_comm]

/-- BITWISE_XOR is commutative over Nat. -/
theorem bitwise_xor_comm_nat (a b : Nat) :
    Op.evalNatBitwise .BITWISE_XOR a b =
    Op.evalNatBitwise .BITWISE_XOR b a := by
  simp [Op.evalNatBitwise, Nat.xor_comm]

/-! ## Relational complement pairs

C++ ExprPool normalizes `NOT(LT(a,b))` to `GE(a,b)` etc.
These pairings are structural properties of the op set. -/

/-- Relational complement: the op that negates a comparison. -/
def Op.relComplement : Op -> Option Op
  | .LT => some .GE
  | .LE => some .GT
  | .GT => some .LE
  | .GE => some .LT
  | .EQ => some .NE
  | .NE => some .EQ
  | _ => none

/-- Double negation: complementing twice returns the original.
    C++: `NOT(NOT(LT(a,b)))` normalizes back to `LT(a,b)`. -/
theorem relComplement_involution (op : Op) (cop : Op)
    (h : op.relComplement = some cop) :
    cop.relComplement = some op := by
  cases op <;> simp [Op.relComplement] at h <;> subst h <;> rfl

/-- Complement preserves the relational category.
    The complement of a relational op is always relational. -/
theorem relComplement_boolean (op cop : Op)
    (h : op.relComplement = some cop) :
    cop.resultType = .boolean := by
  cases op <;> simp [Op.relComplement] at h <;> subst h <;> rfl

/-! ## GT/GE as flipped LT/LE

C++ ExprPool may normalize `GT(a,b)` to `LT(b,a)` and `GE(a,b)` to `LE(b,a)`.
This is a structural symmetry of the relational ops. -/

/-- Relational evaluation over integers. Returns a boolean result. -/
def Op.evalRelBin (op : Op) (a b : Int) : Option Bool :=
  match op with
  | .EQ => some (decide (a = b))
  | .NE => some (decide (a ≠ b))
  | .LT => some (decide (a < b))
  | .LE => some (decide (a ≤ b))
  | .GT => some (decide (a > b))
  | .GE => some (decide (a ≥ b))
  | _ => none

/-- GT(a,b) = LT(b,a). Justifies canonicalization. -/
theorem gt_eq_lt_flip (a b : Int) :
    Op.evalRelBin .GT a b = Op.evalRelBin .LT b a := by
  simp [Op.evalRelBin]

/-- GE(a,b) = LE(b,a). Justifies canonicalization. -/
theorem ge_eq_le_flip (a b : Int) :
    Op.evalRelBin .GE a b = Op.evalRelBin .LE b a := by
  simp [Op.evalRelBin]

/-- EQ is commutative (symmetric). -/
theorem eq_comm_int (a b : Int) :
    Op.evalRelBin .EQ a b = Op.evalRelBin .EQ b a := by
  simp only [Op.evalRelBin]
  congr 1; simp [eq_comm]

/-- NE is commutative (symmetric). -/
theorem ne_comm_int (a b : Int) :
    Op.evalRelBin .NE a b = Op.evalRelBin .NE b a := by
  simp only [Op.evalRelBin]
  congr 1; simp [ne_comm]

/-! ## Arity structural theorems -/

/-- IDENTITY is semantically transparent (unary).
    C++: `identity(x)` prevents expansion but `eval(identity(x)) = eval(x)`. -/
theorem identity_is_unary : Op.IDENTITY.arity = .unary := rfl

/-- NEG is the arithmetic negation (unary). -/
theorem neg_is_unary : Op.NEG.arity = .unary := rfl

/-- WHERE is ternary: (condition, true_branch, false_branch). -/
theorem where_is_ternary : Op.WHERE.arity = .ternary := rfl

/-- MODULAR_INDEXING is ternary: (base // divisor) % modulus. -/
theorem modular_indexing_is_ternary : Op.MODULAR_INDEXING.arity = .ternary := rfl

end Crucible
