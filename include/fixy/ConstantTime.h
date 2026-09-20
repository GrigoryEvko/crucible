#pragma once

// Primitives whose execution trace does not depend on their operands,
// for code paths that handle credentials, keys and authentication tags.
// Every operation is bitwise or integer arithmetic, with no branch and
// no lookup indexed by the data.
//
// These are building blocks, not a guarantee.  Nothing here detects a
// branch on a secret elsewhere in the caller.
//
// Scope: a value that is not classified does not belong here.  An
// identity, a content hash or a deterministic generator stream is a
// public input, and routing it through these primitives buys nothing
// and costs cycles.
//
// Old spelling: include/crucible/safety/ConstantTime.h.

#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <type_traits>

namespace fixy::ct {

// Turns a 0 or 1 into an all-zeros or all-ones mask.  The caller owes
// the invariant that the input really is 0 or 1.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T mask_from_bit(T bit01) noexcept {
    return static_cast<T>(T{0} - (bit01 & T{1}));
}

// Returns a when bit01 is 1 and b when it is 0.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T select(T bit01, T a, T b) noexcept {
    const T m = mask_from_bit(bit01);
    return (a & m) | (b & ~m);
}

// Equality of two byte buffers whose timing depends only on the
// length, which is public, and not on where the first difference is.
//
// The parameters are spans rather than pointer-and-length triples
// because a span is structurally non-null at any non-zero length.  An
// empty span pair compares equal, which is the vacuous truth over no
// bytes.
//
// A length mismatch is a caller bug and traps.  Every real use of this
// primitive compares tags of a statically known length, so a mismatch
// means corruption or a mis-edit, and returning false would hide it.
[[nodiscard]] constexpr bool eq(std::span<const std::byte> a, std::span<const std::byte> b) noexcept {
    CRUCIBLE_PRE(a.size() == b.size());
    std::byte acc{0};
    for (std::size_t i = 0; i < a.size(); ++i) {
        acc |= a[i] ^ b[i];
    }
    return acc == std::byte{0};
}

// Returns 1 when a < b and 0 otherwise.
//
// The obvious idiom, the high bit of (a - b), is wrong at every width.
// A same-width subtraction discards the borrow, which belongs at bit
// `bits` and not at bit `bits - 1`, so the high bit of the difference
// says nothing about the ordering.  The formula below reconstructs the
// borrow from the bitwise relationship between a, b and the modular
// difference instead:
//
//   lt(a, b) := ((~a & b) | ((~a | b) & (a - b))) >> (bits - 1)
//
// `~a & b` lights up each position where b has a 1 and a has a 0, and
// `(~a | b) & (a - b)` propagates the borrow leftward.  The result has
// its high bit set exactly when a < b.
//
// Nothing widens, so the formula also works at the widest unsigned
// type.  The explicit casts truncate back to T after each promotion to
// int, which keeps the modular relationship the formula relies on and
// keeps a signed value out of the final shift.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T less(T a, T b) noexcept {
    constexpr int bits = static_cast<int>(sizeof(T) * 8);
    T const diff = static_cast<T>(a - b);
    T const lt = static_cast<T>((static_cast<T>(~a) & b) | (static_cast<T>(static_cast<T>(~a) | b) & diff));
    return static_cast<T>(lt >> (bits - 1)) & T{1};
}

// Returns 1 when x is zero and 0 otherwise.  For any non-zero x the
// high bit of (x | -x) is set, and it is clear only for zero.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T is_zero(T x) noexcept {
    constexpr int bits = static_cast<int>(sizeof(T) * 8);
    const T neg_x = static_cast<T>(T{0} - x);
    return static_cast<T>((neg_x | x) >> (bits - 1) ^ T{1});
}

// Swaps a and b when cond01 is 1 and leaves them alone when it is 0.
template <std::unsigned_integral T>
constexpr void cswap(T cond01, T& a, T& b) noexcept {
    const T m = mask_from_bit(cond01);
    const T d = (a ^ b) & m;
    a ^= d;
    b ^= d;
}

}  // namespace fixy::ct
