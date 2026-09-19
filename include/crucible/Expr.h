#pragma once

#include <crucible/Ops.h>
#include <crucible/Platform.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/_Pre.h>
#include <crucible/safety/_Tagged.h>
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
    // The arity ceiling of the whole IR. `nargs` is a uint8_t, so a node
    // cannot name a 256th child: a wider argument list would have to be
    // truncated, and a node whose `nargs` disagrees with the length of its
    // `args` array reads past that array on every later traversal.
    //
    // Every scratch buffer and every arity precondition in ExprPool is sized
    // from this one constant, so the bound has a single place to change.
    static constexpr uint8_t kMaxArgs = 255;

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

// Ties the ceiling to the field it comes from. Widening `nargs` without
// raising kMaxArgs would leave every ExprPool scratch buffer short.
static_assert(Expr::kMaxArgs == static_cast<uint8_t>(~static_cast<uint8_t>(0)),
              "Expr::kMaxArgs must be the largest value Expr::nargs can hold");

static_assert(std::is_same_v<decltype(std::declval<Expr>().hash),
                             const ::crucible::safety::Tagged<std::uint64_t, hash_family::FamilyB>>,
              "Expr::hash must stay a const Tagged<uint64_t, hash_family::FamilyB>");
static_assert(sizeof(::crucible::safety::Tagged<std::uint64_t, hash_family::FamilyB>) == sizeof(std::uint64_t),
              "Tagged<uint64_t, hash_family::FamilyB> must stay the width of its payload, or the Expr "
              "layout changes");

namespace detail {

// The one mixing primitive in the tree.  Accumulator folds do not call it
// directly — they call `combine_ids(state, input)` just below, which is this
// function behind one displacement step.  The displacement is load-bearing,
// and the last paragraph says why.
//
// This is the MurmurHash3 finalizer.  Each step is invertible modulo 2^64 —
// an xor-shift is its own inverse family, and a multiply by an odd constant
// is invertible — so fmix64 is a PERMUTATION of the 64-bit words.  That one
// property is why it is the only mixer here:
//
//   * A permutation has no absorbing value.  There is no `x` for which
//     `fmix64(state ^ x)` forgets `state`, so no single input can erase the
//     prefix of a fold.  Measured: zero collisions over 400,000 consecutive
//     inputs, as a bijection must give.
//   * It is total.  Every 64-bit input maps to a distinct 64-bit output, so
//     a content hash of 0 — the KernelCache empty-slot sentinel, and a value
//     make_region's own postcondition refuses — can only arise from the one
//     accumulator state that maps to it, never from a degenerate input.
//
// It replaced `wymix(a, b) = lo(a*b) ^ hi(a*b)`, deleted 2026-09-15, which
// was none of those things.  The 128-bit product folded down to 64 bits is
// lossy by construction and had two absorbing values for its second operand:
// `wymix(a, 0)` is 0 and `wymix(a, ~0)` is ~0, for every `a`.  Both were
// reachable from real tensor metadata — a uint8 tensor on CPU:0 packs to
// zero, and ScalarType::Undefined is int8_t(-1), which sign-extends to all
// ones — so two structurally different regions hashed identically, on the
// value that keys the compiler's cache.  Three call sites had already
// patched around the zero case locally with three different spellings, each
// commenting that wymix collapses on it; the per-op tensor fold, the one
// that matters most, never got the treatment.
//
// wymix bought nothing for that.  Its own comment claimed it avalanched
// better than a shift-and-xor chain; measured against this tree's other
// fmix64-based combiner it avalanches identically, 31.93 against 32.04
// flipped output bits per input bit flipped, both at the ideal 32 of 64.
//
// Two reasons a fold calls combine_ids rather than fmix64 on a bare xor.
//
// A single `fmix64(a ^ b)` is xor-symmetric, so it cannot on its own tell
// operand order.  Order sensitivity comes from the CHAIN: the accumulator
// has already been through fmix64 and the input has not, so the two are not
// interchangeable across steps.  Do not flatten a fold into one xor.
//
// And `fmix64(0)` is 0.  A permutation still has exactly one preimage of
// zero, and for the bare form that preimage is `input == accumulator` — a
// coincidence that ordinary data reaches, because a seed and a schema hash
// can be built from the same constant.  It happened on the first run of
// this change: test_cipher_commit_lifetime mints a region whose schema hash
// is `1 * 0x9E3779B97F4A7C15`, which is the fold's own seed, so the first
// step produced `fmix64(0)` and the content hash came out 0 — the
// KernelCache empty-slot sentinel, caught by make_region's postcondition.
// combine_ids displaces the input by the golden ratio and mixes the
// accumulator's own bits before the xor, so the zero preimage stops
// coinciding with `input == accumulator`.  Measured: 200,000 of 200,000
// self-mixes give 0 through the bare form, 0 of 200,000 through combine_ids.
constexpr uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// The accumulator step every fold in the tree spells.  Boost-style combine
// — a golden-ratio salt and two shifts of the accumulator — finalized by
// fmix64 above.  It is order-sensitive: combining a with b differs from
// combining b with a, and callers that fold a sequence rely on that.
//
// Read the fmix64 block above for why a fold calls this rather than
// fmix64 on a bare xor.
//
// constexpr and not consteval because one body has to serve both the
// compile-time fold and a runtime check that re-derives the same value.
// A second copy of this body under any other name is a drift surface:
// changing the salt, the mix or the finalizer would leave that copy stale
// and change the shared key while every assertion against it still passed.
// scripts/check-no-combine-ids-duplicate.sh is the gate on that.
//
// It lives here rather than in safety/diag/StableName.h, where it was
// written, because MerkleDag.h, Graph.h and ExprPool.h fold with it and
// are fixy-certified — they cannot name `safety::` to reach it.  Expr.h is
// upstream of all of them and of StableName.h, which now uses it from here.
[[nodiscard]] constexpr uint64_t combine_ids(uint64_t a, uint64_t b) noexcept {
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return fmix64(a);
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
