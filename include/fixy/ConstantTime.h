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

#include <foundation/Platform.h>
#include <foundation/core/Region.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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

// Returns 1 when x is zero and 0 otherwise.  For any non-zero x the
// high bit of (x | -x) is set, and it is clear only for zero.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T is_zero(T x) noexcept {
    constexpr int bits = static_cast<int>(sizeof(T) * 8);
    const T neg_x = static_cast<T>(T{0} - x);
    return static_cast<T>((neg_x | x) >> (bits - 1) ^ T{1});
}

namespace detail {
// Returns true when the accumulated difference of two buffers is zero.
// The result comes from is_zero and not from a compare.  A Release build
// takes -fharden-compares, which puts a test and a branch to a trap after
// each compare that gives a value, and the operands of that branch are
// the secret difference.
template <std::unsigned_integral T>
[[nodiscard]] constexpr bool is_zero_without_compare(T difference) noexcept {
    return static_cast<bool>(is_zero(difference));
}
}  // namespace detail

// Equality of two byte views whose timing depends only on the length,
// which is public, and not on where the first difference is.  It ORs the
// difference of each pair of bytes and reads the accumulator once.
//
// The parameters are Views of the base, so no null pointer with a count
// reaches the loop.  An empty pair compares equal, which is the vacuous
// truth over no bytes.
//
// A length mismatch is a caller bug and traps.  Every real use of this
// primitive compares tags of one known length, so a mismatch means
// corruption or a mis-edit, and returning false would hide it.  The check
// holds under each contract semantic: an assumption of equal lengths lets
// the optimizer remove the end test of the cursor over b, and a shorter b
// is then read past its end.
[[nodiscard]] constexpr bool eq(::foundation::core::View<std::byte const> a,
                                ::foundation::core::View<std::byte const> b) noexcept {
    CRUCIBLE_FATAL_INVARIANT(a.size() == b.size());
    std::byte acc{0};
    auto cursor = b.begin();
    for (std::byte const byte : a) {
        acc |= byte ^ *cursor;
        ++cursor;
    }
    return detail::is_zero_without_compare(static_cast<std::uint8_t>(acc));
}

namespace detail {
// The element of a View that eq reads: a byte, const or not.
template <class E>
concept ByteElement = std::same_as<std::remove_const_t<E>, std::byte>;

// The XOR of the word of eight bytes at index Word of two runs of N bytes.
// A byte copy reads each word, so the runs need no alignment.
template <std::size_t Word, std::size_t N>
    requires((Word + 1) * sizeof(std::uint64_t) <= N)
[[nodiscard]] CRUCIBLE_INLINE std::uint64_t word_difference(std::byte const* left_first,
                                                            std::byte const* right_first) noexcept {
    std::uint64_t left = 0;
    std::uint64_t right = 0;
    __builtin_memcpy(&left, left_first + Word * sizeof(std::uint64_t), sizeof(std::uint64_t));
    __builtin_memcpy(&right, right_first + Word * sizeof(std::uint64_t), sizeof(std::uint64_t));
    return left ^ right;
}
}  // namespace detail

// Equality of two byte views of one static extent, such as two tags of a
// keyed hash.  Its timing depends only on the extent, which is public.
// The run-time fold below has no loop, so the compiled body carries no
// branch at all.  The rule codegen-shapes checks that claim on the
// compiled code.
//
// At run time, the fold reads the views in words of eight bytes and ORs
// the differences of the words.  A word holds each byte in a lane of its
// own, so the order of the bytes in a word does not change the zero test.
// The bytes after the last full word OR into the same accumulator.  A
// constant evaluation cannot copy bytes into a word, so it ORs the
// difference of each byte.  The two forms give the same result.  Two views
// of different static extents reach the deleted form below, and not the
// dynamic form above.
template <std::size_t N>
    requires(N != ::foundation::core::dynamic_extent)
[[nodiscard]] constexpr bool eq(::foundation::core::View<std::byte const, N> a,
                                ::foundation::core::View<std::byte const, N> b) noexcept {
    constexpr std::size_t word_bytes = sizeof(std::uint64_t);
    constexpr std::size_t words = N / word_bytes;
    std::uint64_t acc = 0;
    if consteval {
        auto cursor = b.begin();
        for (std::byte const byte : a) {
            acc |= static_cast<std::uint64_t>(byte ^ *cursor);
            ++cursor;
        }
    } else {
        std::byte const* const left = first_of_(::foundation::core::detail::ViewDoor::key(), a);
        std::byte const* const right = first_of_(::foundation::core::detail::ViewDoor::key(), b);
        [&]<std::size_t... W>(std::index_sequence<W...>) {
            ((acc |= detail::word_difference<W, N>(left, right)), ...);
        }(std::make_index_sequence<words>{});
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((acc |= static_cast<std::uint64_t>(left[words * word_bytes + I] ^ right[words * word_bytes + I])), ...);
        }(std::make_index_sequence<N % word_bytes>{});
    }
    return detail::is_zero_without_compare(acc);
}

// Two static extents that differ cannot name one tag, so the pair does
// not compile.  Without this overload, the two Views convert to the
// dynamic form above, whose precondition stops the mismatch only at run
// time.
template <detail::ByteElement A, detail::ByteElement B, std::size_t N, std::size_t M>
    requires(N != ::foundation::core::dynamic_extent && M != ::foundation::core::dynamic_extent && N != M)
bool eq(::foundation::core::View<A, N>, ::foundation::core::View<B, M>) =
    delete("fixy::ct::eq: the two views have static extents that differ, so they cannot hold one tag.  Compare "
           "views of one length.");

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

// Swaps a and b when cond01 is 1 and leaves them alone when it is 0.
template <std::unsigned_integral T>
constexpr void cswap(T cond01, T& a, T& b) noexcept {
    const T m = mask_from_bit(cond01);
    const T d = (a ^ b) & m;
    a ^= d;
    b ^= d;
}

}  // namespace fixy::ct
