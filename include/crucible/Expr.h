#pragma once

#include <crucible/Ops.h>

#include <bit>
#include <cstdint>

namespace crucible {

// Immutable, interned expression node.
//
// All Expr nodes are arena-allocated by ExprPool and globally interned:
//   - Same structure → same pointer (identity equality)
//   - Comparison: a == b is pointer comparison (~1ns)
//   - Hash: precomputed at construction for intern table only
//   - Lifetime: arena-managed, freed when ExprPool is destroyed
//
// 32 bytes on 64-bit systems. Fits in half a cache line.
struct Expr {
  Op op;               // 1 byte  — node type
  uint8_t nargs;       // 1 byte  — number of children (0-255)
  uint16_t flags;      // 2 bytes — ExprFlags bitfield
  uint32_t symbol_id;  // 4 bytes — unique id for symbols (0 for non-symbols)
  uint64_t hash;       // 8 bytes — precomputed hash for intern table
  int64_t payload;     // 8 bytes — integer value, or bitcast double, or symbol name ptr
  const Expr** args;   // 8 bytes — pointer to arena-allocated array of children
                       // ──────────
                       // 32 bytes total

  // ---- Payload accessors ----

  int64_t as_int() const {
    return payload;
  }

  double as_float() const {
    return std::bit_cast<double>(payload);
  }

  const char* as_symbol_name() const {
    return std::bit_cast<const char*>(payload);
  }

  // ---- Flag queries (branchless, single AND instruction) ----

  bool is_integer() const { return flags & ExprFlags::IS_INTEGER; }
  bool is_real() const { return flags & ExprFlags::IS_REAL; }
  bool is_finite() const { return flags & ExprFlags::IS_FINITE; }
  bool is_positive() const { return flags & ExprFlags::IS_POSITIVE; }
  bool is_negative() const { return flags & ExprFlags::IS_NEGATIVE; }
  bool is_nonnegative() const { return flags & ExprFlags::IS_NONNEGATIVE; }
  bool is_nonpositive() const { return flags & ExprFlags::IS_NONPOSITIVE; }
  bool is_zero() const { return flags & ExprFlags::IS_ZERO; }
  bool is_even() const { return flags & ExprFlags::IS_EVEN; }
  bool is_odd() const { return flags & ExprFlags::IS_ODD; }
  bool is_number() const { return flags & ExprFlags::IS_NUMBER; }
  bool is_symbol() const { return flags & ExprFlags::IS_SYMBOL; }
  bool is_boolean() const { return flags & ExprFlags::IS_BOOLEAN; }

  // ---- Structural queries ----

  bool is_atom() const { return nargs == 0; }

  bool is_one() const {
    return op == Op::INTEGER && payload == 1;
  }

  bool is_neg_one() const {
    return op == Op::INTEGER && payload == -1;
  }

  bool is_zero_int() const {
    return op == Op::INTEGER && payload == 0;
  }

  // ---- Child access ----

  const Expr* arg(uint8_t i) const {
    return args[i];
  }
};

// Compile-time check: Expr must be 32 bytes for cache efficiency.
static_assert(sizeof(Expr) == 32, "Expr must be exactly 32 bytes");

namespace detail {

// MurmurHash3 64-bit finalizer — proven avalanche properties.
// Shared by ExprPool (structural hashing) and ExprMap (pointer hashing).
constexpr uint64_t fmix64(uint64_t k) {
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccdULL;
  k ^= k >> 33;
  k *= 0xc4ceb9fe1a85ec53ULL;
  k ^= k >> 33;
  return k;
}

} // namespace detail

} // namespace crucible
