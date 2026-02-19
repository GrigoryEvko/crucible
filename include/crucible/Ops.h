#pragma once

#include <cstdint>

namespace crucible {

// Every expression node type in the symbolic math engine.
// Mirrors the inductor's symbolic operations (SymPy function types,
// arithmetic, relational, logic). Organized by category.
enum class Op : uint8_t {
  // ---- Atoms (nargs=0) ----
  INTEGER,       // payload: int64_t
  FLOAT,         // payload: double (bitcast to int64_t)
  SYMBOL,        // payload: symbol_id (index into symbol table)
  BOOL_TRUE,     // no payload
  BOOL_FALSE,    // no payload

  // ---- Arithmetic (nargs=2 for binary, nargs=N for variadic) ----
  ADD,           // variadic, flattened, sorted
  MUL,           // variadic, flattened, sorted
  POW,           // binary: base, exp

  // ---- Relational (nargs=2) ----
  EQ,
  NE,
  LT,
  LE,
  GT,
  GE,

  // ---- Logic ----
  AND,           // variadic
  OR,            // variadic
  NOT,           // unary

  // ---- Division / Modular (nargs=2 unless noted) ----
  FLOOR_DIV,     // base // divisor
  CLEAN_DIV,     // floor_div where no rounding occurs
  CEIL_DIV,      // ceil(base / divisor)
  INT_TRUE_DIV,  // integer true division → float
  FLOAT_TRUE_DIV,// float true division
  MOD,           // nonneg modulus
  PYTHON_MOD,    // Python-style modulus (sign follows divisor)
  MODULAR_INDEXING, // nargs=3: (base // divisor) % modulus

  // ---- Rounding / Type conversion (nargs=1 unless noted) ----
  CEIL_TO_INT,
  FLOOR_TO_INT,
  TRUNC_TO_FLOAT,
  TRUNC_TO_INT,
  ROUND_TO_INT,
  ROUND_DECIMAL, // nargs=2: (number, ndigits)
  TO_FLOAT,

  // ---- Shift (nargs=2) ----
  LSHIFT,
  RSHIFT,

  // ---- Power variants (nargs=2) ----
  POW_BY_NATURAL,// integer power
  FLOAT_POW,     // float power

  // ---- Control flow (nargs=3) ----
  WHERE,         // (cond, true_val, false_val)

  // ---- Identity (nargs=1) ----
  IDENTITY,      // prevents expansion, semantically transparent

  // ---- Min / Max (variadic) ----
  MIN,
  MAX,

  // ---- Indicator (variadic) ----
  IS_NON_OVERLAPPING_AND_DENSE,

  // ---- Opaque unary math functions (nargs=1) ----
  SQRT,
  COS,
  COSH,
  SIN,
  SINH,
  TAN,
  TANH,
  ASIN,
  ACOS,
  ATAN,
  EXP,
  LOG,
  ASINH,
  LOG2,

  // ---- Unary math (nargs=1) ----
  ABS,

  // ---- Bitwise (nargs=2) ----
  BITWISE_AND,
  BITWISE_OR,
  BITWISE_XOR,

  // ---- Negation (unary, used internally) ----
  NEG,

  NUM_OPS // sentinel — must be last
};

// Flags bitfield for Expr::flags.
// Encodes type/assumption properties as bits for O(1) queries.
struct ExprFlags {
  static constexpr uint16_t IS_INTEGER     = 1 << 0;
  static constexpr uint16_t IS_REAL        = 1 << 1;
  static constexpr uint16_t IS_FINITE      = 1 << 2;
  static constexpr uint16_t IS_POSITIVE    = 1 << 3;
  static constexpr uint16_t IS_NEGATIVE    = 1 << 4;
  static constexpr uint16_t IS_NONNEGATIVE = 1 << 5;
  static constexpr uint16_t IS_NONPOSITIVE = 1 << 6;
  static constexpr uint16_t IS_ZERO        = 1 << 7;
  static constexpr uint16_t IS_EVEN        = 1 << 8;
  static constexpr uint16_t IS_ODD         = 1 << 9;
  static constexpr uint16_t IS_NUMBER      = 1 << 10;
  static constexpr uint16_t IS_SYMBOL      = 1 << 11;
  static constexpr uint16_t IS_BOOLEAN     = 1 << 12;
};

} // namespace crucible
