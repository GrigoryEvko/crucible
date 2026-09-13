#pragma once

#include <crucible/Ops.h>
#include <crucible/Platform.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>
#include <crucible/safety/Tagged.h>
#include <crucible/Types.h>

#include <bit>
#include <cassert>
#include <cstdint>

namespace crucible {

// An interned expression node. Two nodes with the same structure are the
// same object, so equality is pointer equality.
//
// Every field is const because interning depends on a published node never
// changing: a changed hash orphans its slot in the intern table, and a
// changed payload breaks the structural equality that deduplication rests on.
//
// Nodes live in an arena, which never runs destructors. This type is
// trivially destructible.
struct Expr {
    const Op op = Op::INTEGER;
    const uint8_t nargs = 0;
    const uint16_t flags = 0;
    const SymbolId symbol_id;  // default-valued on a node that is not a symbol
    // This hash is process-local. The intern table mixes the addresses of the
    // child array into it, so the same structure hashes differently in
    // another process. It must never be persisted, shared between processes,
    // or folded into a content hash. The tag is what makes a cross-family use
    // fail to compile rather than silently produce an unstable key.
    const ::crucible::safety::Tagged<std::uint64_t, hash_family::FamilyB> hash{std::uint64_t{0}};
    const int64_t payload = 0;  // an integer, a double's bits, or a name pointer
    const Expr* const* const args = nullptr;

    // The default node is the integer zero.
    constexpr Expr() noexcept = default;

    // Zero is a legitimate hash and a legitimate payload. A null child array
    // is legitimate too, but only for a node with no children.
    constexpr Expr(Op op_, uint8_t nargs_, uint16_t flags_, SymbolId symbol_id_, uint64_t hash_, int64_t payload_,
                   const Expr* const* args_) noexcept
        pre(::crucible::decide::implies(::crucible::decide::positive(nargs_), args_ != nullptr))
        : op(op_), nargs(nargs_), flags(flags_), symbol_id(symbol_id_), hash(hash_), payload(payload_), args(args_) {}

    Expr(const Expr&) = delete("interned Exprs have identity equality; copying would break intern");
    Expr& operator=(const Expr&) = delete("fields are const");
    Expr(Expr&&) = delete("interned Exprs are arena-pinned");
    Expr& operator=(Expr&&) = delete("fields are const");

    [[nodiscard]] constexpr int64_t as_int() const { return payload; }

    [[nodiscard]] double as_float() const { return std::bit_cast<double>(payload); }

    [[nodiscard]] const char* as_symbol_name() const noexcept CRUCIBLE_LIFETIMEBOUND {
        return std::bit_cast<const char*>(payload);
    }

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

    [[nodiscard, gnu::pure]] constexpr bool is_atom() const { return nargs == 0; }

    [[nodiscard, gnu::pure]] constexpr bool is_one() const { return op == Op::INTEGER && payload == 1; }

    [[nodiscard, gnu::pure]] constexpr bool is_neg_one() const { return op == Op::INTEGER && payload == -1; }

    [[nodiscard, gnu::pure]] constexpr bool is_zero_int() const { return op == Op::INTEGER && payload == 0; }

    [[nodiscard]] const Expr* arg(uint8_t i) const CRUCIBLE_LIFETIMEBOUND {
        // The first guard is not redundant with the second. On a node with no
        // children the subtraction below wraps to the largest uint8_t and the
        // range check then admits every index.
        CRUCIBLE_PRE(nargs > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<std::uint8_t>(i, 0u, static_cast<std::uint8_t>(nargs - 1u)));
        CRUCIBLE_PRE(args != nullptr);
        return args[i];
    }
};

static_assert(sizeof(Expr) == 32, "Expr must be exactly 32 bytes");

static_assert(std::is_same_v<decltype(std::declval<Expr>().hash),
                             const ::crucible::safety::Tagged<std::uint64_t, hash_family::FamilyB>>,
              "Expr::hash must stay a const Tagged<uint64_t, hash_family::FamilyB>");
static_assert(sizeof(::crucible::safety::Tagged<std::uint64_t, hash_family::FamilyB>) == sizeof(std::uint64_t),
              "Tagged<uint64_t, hash_family::FamilyB> must stay the width of its payload, or the Expr "
              "layout changes");

namespace detail {

constexpr uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// One wide multiply with the halves folded together. It avalanches better
// than a chain of shifts and xors when the inputs already carry some entropy,
// which is the case for the addresses this mixes.
inline uint64_t wymix(uint64_t a, uint64_t b) {
#ifdef __SIZEOF_INT128__
    __uint128_t full = static_cast<__uint128_t>(a) * b;
    return static_cast<uint64_t>(full) ^ static_cast<uint64_t>(full >> 64);
#else
    return fmix64(a ^ b);
#endif
}

// A distinct constant per lane, so an extent and a stride at the same index
// cannot cancel when the fold xors them together. The first eight are for
// extents and the second eight for strides. Each is an odd multiple of the
// same seed, so all sixteen are distinct and coprime to the word size.
//
// Folding with xor rather than mixing each term keeps the sixteen products
// independent of one another, leaving a single mix at the end instead of a
// chain of them.
inline constexpr uint64_t kDimMix[16] = {
    0x9E3779B97F4A7C15ULL * 1,  0x9E3779B97F4A7C15ULL * 2,  0x9E3779B97F4A7C15ULL * 3,  0x9E3779B97F4A7C15ULL * 4,
    0x9E3779B97F4A7C15ULL * 5,  0x9E3779B97F4A7C15ULL * 6,  0x9E3779B97F4A7C15ULL * 7,  0x9E3779B97F4A7C15ULL * 8,
    0x9E3779B97F4A7C15ULL * 9,  0x9E3779B97F4A7C15ULL * 10, 0x9E3779B97F4A7C15ULL * 11, 0x9E3779B97F4A7C15ULL * 12,
    0x9E3779B97F4A7C15ULL * 13, 0x9E3779B97F4A7C15ULL * 14, 0x9E3779B97F4A7C15ULL * 15, 0x9E3779B97F4A7C15ULL * 16,
};

}  // namespace detail

}  // namespace crucible
