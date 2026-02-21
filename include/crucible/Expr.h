#pragma once

#include <crucible/Ops.h>
#include <crucible/Platform.h>
#include <crucible/Types.h>

#include <bit>
#include <cassert>
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
  Op op = Op::INTEGER;         // 1 byte  — node type
  uint8_t nargs = 0;           // 1 byte  — number of children (0-255)
  uint16_t flags = 0;          // 2 bytes — ExprFlags bitfield
  SymbolId symbol_id;          // 4 bytes — unique id for symbols (SymbolId{} for non-symbols)
  uint64_t hash = 0;           // 8 bytes — precomputed hash for intern table
  int64_t payload = 0;         // 8 bytes — integer value, or bitcast double, or symbol name ptr
  const Expr** args = nullptr; // 8 bytes — pointer to arena-allocated array of children
                               // ──────────
                               // 32 bytes total

  // ---- Payload accessors ----

  [[nodiscard]] constexpr int64_t as_int() const {
    return payload;
  }

  [[nodiscard]] double as_float() const {
    return std::bit_cast<double>(payload);
  }

  [[nodiscard]] const char* as_symbol_name() const {
    return std::bit_cast<const char*>(payload);
  }

  // ---- Flag queries (branchless, single AND instruction) ----

  [[nodiscard]] constexpr bool is_integer() const { return flags & ExprFlags::IS_INTEGER; }
  [[nodiscard]] constexpr bool is_real() const { return flags & ExprFlags::IS_REAL; }
  [[nodiscard]] constexpr bool is_finite() const { return flags & ExprFlags::IS_FINITE; }
  [[nodiscard]] constexpr bool is_positive() const { return flags & ExprFlags::IS_POSITIVE; }
  [[nodiscard]] constexpr bool is_negative() const { return flags & ExprFlags::IS_NEGATIVE; }
  [[nodiscard]] constexpr bool is_nonnegative() const { return flags & ExprFlags::IS_NONNEGATIVE; }
  [[nodiscard]] constexpr bool is_nonpositive() const { return flags & ExprFlags::IS_NONPOSITIVE; }
  [[nodiscard]] constexpr bool is_zero() const { return flags & ExprFlags::IS_ZERO; }
  [[nodiscard]] constexpr bool is_even() const { return flags & ExprFlags::IS_EVEN; }
  [[nodiscard]] constexpr bool is_odd() const { return flags & ExprFlags::IS_ODD; }
  [[nodiscard]] constexpr bool is_number() const { return flags & ExprFlags::IS_NUMBER; }
  [[nodiscard]] constexpr bool is_symbol() const { return flags & ExprFlags::IS_SYMBOL; }
  [[nodiscard]] constexpr bool is_boolean() const { return flags & ExprFlags::IS_BOOLEAN; }

  // ---- Structural queries ----

  [[nodiscard]] constexpr bool is_atom() const { return nargs == 0; }

  [[nodiscard]] constexpr bool is_one() const {
    return op == Op::INTEGER && payload == 1;
  }

  [[nodiscard]] constexpr bool is_neg_one() const {
    return op == Op::INTEGER && payload == -1;
  }

  [[nodiscard]] constexpr bool is_zero_int() const {
    return op == Op::INTEGER && payload == 0;
  }

  // ---- Child access ----

  [[nodiscard]] const Expr* arg(uint8_t i) const CRUCIBLE_LIFETIMEBOUND {
    assert(i < nargs && "Expr::arg() index out of bounds");
    assert(args && "Expr::arg() called on atom with no args");
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

// wyhash-style 64-bit mix: one 128-bit multiply, XOR halves.
// ~2 instructions on x86-64 (mulq + xor). Superior avalanche to
// XOR-shift chains when inputs are already somewhat random (pointers).
// Used on the intern() hot path where every nanosecond matters.
inline uint64_t wymix(uint64_t a, uint64_t b) {
#ifdef __SIZEOF_INT128__
  __uint128_t full = static_cast<__uint128_t>(a) * b;
  return static_cast<uint64_t>(full) ^ static_cast<uint64_t>(full >> 64);
#else
  // Fallback: fmix64 when 128-bit multiply unavailable
  return fmix64(a ^ b);
#endif
}

} // namespace detail

} // namespace crucible
