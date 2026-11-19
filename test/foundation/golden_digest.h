#pragma once

// The digest of the golden bits tests.  A test folds the bits of each output
// of a form into one digest, the FNV-1a hash of 64 bits, and holds the digest
// of each form.  A change of the bits of one output changes the digest.  Take
// each new digest from two builds of different vector widths, and only when
// the two builds agree.
//
//   kFnvOffset, fold_output, fold_outputs   the digest
//   Mix                                     a combine that is neither
//                                           associative nor commutative
//   is_golden                               the compare with a golden digest,
//                                           which prints a digest that differs

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "nan_outputs.h"

#include <cstdint>

namespace crucible::test::golden {

// The FNV-1a hash of 64 bits.  A word adds its bytes from the low byte.
inline constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325u;
inline constexpr std::uint64_t kFnvPrime = 0x100000001B3u;

// Adds the count low bytes of word to digest.
[[nodiscard]] constexpr std::uint64_t fold_bytes(std::uint64_t digest, std::uint64_t word, int count) noexcept {
    for (int position = 0; position < count; ++position) {
        digest = (digest ^ ((word >> (8 * position)) & 0xFFu)) * kFnvPrime;
    }
    return digest;
}

[[nodiscard]] inline std::uint64_t fold_output(std::uint64_t digest, float value) noexcept {
    return fold_bytes(digest, float_bits(value), 4);
}
[[nodiscard]] inline std::uint64_t fold_output(std::uint64_t digest, double value) noexcept {
    return fold_bytes(digest, float_bits(value), 8);
}
[[nodiscard]] inline std::uint64_t fold_output(std::uint64_t digest, std::uint64_t value) noexcept {
    return fold_bytes(digest, value, 8);
}

template <class T>
[[nodiscard]] std::uint64_t fold_outputs(std::uint64_t digest, ::fixy::View<T const> outputs) noexcept {
    for (T const value : outputs) {
        digest = fold_output(digest, value);
    }
    return digest;
}

// A combine that is neither associative nor commutative: a change of the
// shape of the tree or of the order of two operands changes its result.
struct Mix {
    [[nodiscard]] constexpr std::uint64_t operator()(std::uint64_t const& left,
                                                     std::uint64_t const& right) const noexcept {
        return ::fixy::rotl(left, 7) * 0x9E3779B97F4A7C15u + (right ^ 0xD1B54A32D192ED03u);
    }
};

// Compares the digest of one form with its golden digest, and prints the two
// when they differ.
[[nodiscard]] inline bool is_golden(::fixy::TestRunnerCtx const& test, ::fixy::TextView form, std::uint64_t digest,
                                    std::uint64_t golden) noexcept {
    if (digest == golden) return true;
    ::fixy::check::pass(test, "the digest of {} is {}, and the golden digest is {}\n", form, digest, golden);
    return false;
}

}  // namespace crucible::test::golden
