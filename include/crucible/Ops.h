#pragma once

#include <cstdint>

namespace crucible {

// The set mirrors the symbolic operations of the frontend whose traces this
// records, so both the names and their semantics follow that source rather
// than being chosen here.
enum class Op : uint8_t {
    // Atoms, no children.
    INTEGER,  // payload: the value
    FLOAT,  // payload: the value's bits
    SYMBOL,  // payload: an index into the symbol table
    BOOL_TRUE,  // no payload
    BOOL_FALSE,  // no payload

    // Arithmetic.
    ADD,  // variadic, flattened, and sorted
    MUL,  // variadic, flattened, and sorted
    POW,  // base, exponent

    // Relational, two children each.
    EQ,
    NE,
    LT,
    LE,
    GT,
    GE,

    // Logic.
    AND,  // variadic
    OR,  // variadic
    NOT,  // unary

    // Division and modulus, two children unless noted.
    FLOOR_DIV,
    CLEAN_DIV,  // a floor division known to divide exactly
    CEIL_DIV,
    INT_TRUE_DIV,  // integer operands, real result
    FLOAT_TRUE_DIV,
    MOD,  // result is never negative
    PYTHON_MOD,  // result takes the sign of the divisor
    MODULAR_INDEXING,  // three children: a floor division taken modulo the third

    // Rounding and conversion, one child unless noted.
    CEIL_TO_INT,
    FLOOR_TO_INT,
    TRUNC_TO_FLOAT,
    TRUNC_TO_INT,
    ROUND_TO_INT,
    ROUND_DECIMAL,  // two children: the number and a digit count
    TO_FLOAT,

    // Shifts, two children each.
    LSHIFT,
    RSHIFT,

    // Powers, two children each.
    POW_BY_NATURAL,  // integer exponent
    FLOAT_POW,

    WHERE,  // three children: condition, value if true, value if false

    IDENTITY,  // one child, transparent, and a barrier against expansion

    // Variadic.
    MIN,
    MAX,

    IS_NON_OVERLAPPING_AND_DENSE,  // variadic

    // Transcendental, one child each, treated as opaque.
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

    ABS,  // one child

    // Bitwise, two children each.
    BITWISE_AND,
    BITWISE_OR,
    BITWISE_XOR,

    NEG,  // one child, produced internally

    NUM_OPS  // sentinel, and must stay last
};

// Lookup tables sized by the sentinel, and every place that round-trips an
// operation through a single byte, depend on this. Growing past it means
// widening the underlying type and auditing each of those places.
static_assert(static_cast<unsigned>(Op::NUM_OPS) <= 256, "the underlying type is uint8_t, so NUM_OPS must fit in it");

struct ExprFlags {
    static constexpr uint16_t IS_INTEGER = 1 << 0;
    static constexpr uint16_t IS_REAL = 1 << 1;
    static constexpr uint16_t IS_FINITE = 1 << 2;
    static constexpr uint16_t IS_POSITIVE = 1 << 3;
    static constexpr uint16_t IS_NEGATIVE = 1 << 4;
    static constexpr uint16_t IS_NONNEGATIVE = 1 << 5;
    static constexpr uint16_t IS_NONPOSITIVE = 1 << 6;
    static constexpr uint16_t IS_ZERO = 1 << 7;
    static constexpr uint16_t IS_EVEN = 1 << 8;
    static constexpr uint16_t IS_ODD = 1 << 9;
    static constexpr uint16_t IS_NUMBER = 1 << 10;
    static constexpr uint16_t IS_SYMBOL = 1 << 11;
    static constexpr uint16_t IS_BOOLEAN = 1 << 12;
};

}  // namespace crucible
