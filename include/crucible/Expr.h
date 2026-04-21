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
//
// ── Immutability ──
// All member fields are `const`.  Once an Expr is constructed via the
// full-argument constructor below, no code path can mutate its
// structure — the TypeSafe axiom from CLAUDE.md extends from "silent
// parameter swap" to "silent node rewrite".  The intern-table protocol
// depends on Expr instances never changing after publication: a hash
// mutation would orphan the slot in the Swiss table, a payload
// mutation would corrupt structural equality in CSE.
//
// Arena storage: ExprPool allocates raw bytes from the Arena and
// placement-news an Expr into them using the full-args constructor.
// Arena never calls destructors; Expr is trivially destructible.
struct Expr {
  const Op op = Op::INTEGER;          // 1 byte  — node type
  const uint8_t nargs = 0;            // 1 byte  — number of children (0-255)
  const uint16_t flags = 0;           // 2 bytes — ExprFlags bitfield
  const SymbolId symbol_id;           // 4 bytes — unique id for symbols (SymbolId{} for non-symbols)
  const uint64_t hash = 0;            // 8 bytes — precomputed hash for intern table
  const int64_t payload = 0;          // 8 bytes — integer value, or bitcast double, or symbol name ptr
  const Expr* const* const args = nullptr;  // 8 bytes — pointer to arena-allocated array of children
                                            // ──────────
                                            // 32 bytes total

  // Default constructor: yields a valid "integer 0" atom.  Value-init
  // via NSDMI keeps default-constructed instances well-defined per the
  // InitSafe axiom.  Primarily used by Arena zero-fill paths and by
  // tests that build placeholder Exprs.
  constexpr Expr() noexcept = default;

  // Full-args constructor: the only way to create a non-default Expr.
  // ExprPool::make_ uses this via placement-new into arena storage.
  // hash_ and payload_ may legitimately be 0 (zero integer, zero-valued
  // flag set).  args_ may be null iff nargs_ == 0.
  constexpr Expr(
      Op           op_,
      uint8_t      nargs_,
      uint16_t     flags_,
      SymbolId     symbol_id_,
      uint64_t     hash_,
      int64_t      payload_,
      const Expr* const* args_) noexcept
      : op(op_)
      , nargs(nargs_)
      , flags(flags_)
      , symbol_id(symbol_id_)
      , hash(hash_)
      , payload(payload_)
      , args(args_)
  {}

  // Copy/move: deleted because const fields make assignment impossible
  // and copying an interned Expr would break intern-table identity
  // (two pointers referencing equivalent structures must be THE SAME
  // pointer, not distinct copies).  Move is likewise nonsensical since
  // the target is an identity-interned pointer.
  Expr(const Expr&)            = delete("interned Exprs have identity equality; copying would break intern");
  Expr& operator=(const Expr&) = delete("fields are const");
  Expr(Expr&&)                 = delete("interned Exprs are arena-pinned");
  Expr& operator=(Expr&&)      = delete("fields are const");

  // ---- Payload accessors ----

  [[nodiscard]] constexpr int64_t as_int() const {
    return payload;
  }

  [[nodiscard]] double as_float() const {
    return std::bit_cast<double>(payload);
  }

  [[nodiscard]] const char* as_symbol_name() const noexcept CRUCIBLE_LIFETIMEBOUND {
    return std::bit_cast<const char*>(payload);
  }

  // ---- Flag queries (branchless, single AND instruction) ----

  [[nodiscard, gnu::pure]] constexpr bool is_integer() const { return flags & ExprFlags::IS_INTEGER; }
  [[nodiscard, gnu::pure]] constexpr bool is_real() const { return flags & ExprFlags::IS_REAL; }
  [[nodiscard, gnu::pure]] constexpr bool is_finite() const { return flags & ExprFlags::IS_FINITE; }
  [[nodiscard, gnu::pure]] constexpr bool is_positive() const { return flags & ExprFlags::IS_POSITIVE; }
  [[nodiscard, gnu::pure]] constexpr bool is_negative() const { return flags & ExprFlags::IS_NEGATIVE; }
  [[nodiscard, gnu::pure]] constexpr bool is_nonnegative() const { return flags & ExprFlags::IS_NONNEGATIVE; }
  [[nodiscard, gnu::pure]] constexpr bool is_nonpositive() const { return flags & ExprFlags::IS_NONPOSITIVE; }
  [[nodiscard, gnu::pure]] constexpr bool is_zero() const { return flags & ExprFlags::IS_ZERO; }
  [[nodiscard, gnu::pure]] constexpr bool is_even() const { return flags & ExprFlags::IS_EVEN; }
  [[nodiscard, gnu::pure]] constexpr bool is_odd() const { return flags & ExprFlags::IS_ODD; }
  [[nodiscard, gnu::pure]] constexpr bool is_number() const { return flags & ExprFlags::IS_NUMBER; }
  [[nodiscard, gnu::pure]] constexpr bool is_symbol() const { return flags & ExprFlags::IS_SYMBOL; }
  [[nodiscard, gnu::pure]] constexpr bool is_boolean() const { return flags & ExprFlags::IS_BOOLEAN; }

  // ---- Structural queries ----

  [[nodiscard, gnu::pure]] constexpr bool is_atom() const { return nargs == 0; }

  [[nodiscard, gnu::pure]] constexpr bool is_one() const {
    return op == Op::INTEGER && payload == 1;
  }

  [[nodiscard, gnu::pure]] constexpr bool is_neg_one() const {
    return op == Op::INTEGER && payload == -1;
  }

  [[nodiscard, gnu::pure]] constexpr bool is_zero_int() const {
    return op == Op::INTEGER && payload == 0;
  }

  // ---- Child access ----

  [[nodiscard]] const Expr* arg(uint8_t i) const CRUCIBLE_LIFETIMEBOUND
      pre (i < nargs)
      pre (args != nullptr)
  {
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

// Per-dimension mixing constants for XOR-fold content hash.
// sizes[d] uses kDimMix[d], strides[d] uses kDimMix[d + 8].
//
// Weyl sequence: k[i] = (i+1) * phi, phi = golden ratio constant.
// Well-distributed, all distinct, coprime to 2^64. Independent
// multiplies break the serial wymix chain: ndim XOR-folds (1 cy each)
// + 1 wymix (~5 cy) instead of ndim wymix calls (~5 cy each serial).
inline constexpr uint64_t kDimMix[16] = {
    0x9E3779B97F4A7C15ULL *  1, 0x9E3779B97F4A7C15ULL *  2,
    0x9E3779B97F4A7C15ULL *  3, 0x9E3779B97F4A7C15ULL *  4,
    0x9E3779B97F4A7C15ULL *  5, 0x9E3779B97F4A7C15ULL *  6,
    0x9E3779B97F4A7C15ULL *  7, 0x9E3779B97F4A7C15ULL *  8,
    0x9E3779B97F4A7C15ULL *  9, 0x9E3779B97F4A7C15ULL * 10,
    0x9E3779B97F4A7C15ULL * 11, 0x9E3779B97F4A7C15ULL * 12,
    0x9E3779B97F4A7C15ULL * 13, 0x9E3779B97F4A7C15ULL * 14,
    0x9E3779B97F4A7C15ULL * 15, 0x9E3779B97F4A7C15ULL * 16,
};

} // namespace detail

} // namespace crucible
