#pragma once

// The sample and the digests of test_fp_elementary, for each unit of the test.
//
// The unit of the test takes the level of its build.  On x86_64 the build
// also compiles a unit for each other level of x86-64 (test/fixy/CMakeLists.txt),
// and each unit computes the digests with the bodies of its own level: the
// functions of fixy/fp are in the inline namespace CRUCIBLE_SIMD_TARGET, so
// the link keeps one body for each level.  fp_digests_of_this_unit is a
// template of the unit, so each unit has its own instantiation too.
//
// The probe functions probe_* are out of line, so the disassembly check
// (test/fixy/check_fp_fma.py) can count the fma instructions of the body of
// each function at each level.  A probe holds the inlined body of one
// function, and the cold paths keep their own symbols.
//
// The sample of each function has two parts.  The first part walks the bit
// patterns of the type with a Weyl step, so it holds each class of input:
// the signs, the subnormal values, the infinities and the NaNs.  The second
// part walks the values of the domain where the function is not constant,
// with an even step.  exp(double) has two more parts: the hard inputs of
// fp_exp_hard_cases.h through exp, and the same inputs in the domain of the
// slow path through the slow path on its own.  So each level runs the fast
// path, the edge path and the slow path on the hard inputs.  log(double) has
// the same two parts with the hard inputs of fp_log_hard_cases.h.  The digest is
// FNV-1a of 64 bits over the bits of each result, in the order of the
// sample.

#include <fixy/Core.h>
#include <fixy/fp/Erf.h>
#include <fixy/fp/Exp.h>
#include <fixy/fp/Log.h>

#include "fp_exp_hard_cases.h"
#include "fp_log_hard_cases.h"

#include <cstdint>

namespace test_fp_elementary {

// The units of the test: the unit of the build and the units of the levels.
enum class FpUnit : std::uint8_t {
    Build,
    V1,
    V2,
    V3,
    V4,
};

// The digest of the results of each function on its sample.
struct FpDigests {
    std::uint64_t exp_double = 0;
    std::uint64_t exp_float = 0;
    std::uint64_t log_double = 0;
    std::uint64_t log_float = 0;
    std::uint64_t erf_double = 0;
    std::uint64_t erf_float = 0;
};

// The count of each part of the sample of one function.
inline constexpr std::uint32_t kSampleCount = 4096;

// The domain of the second part of each sample: its start and its width.
struct Domain {
    double start = 0.0;
    double width = 0.0;
};

inline constexpr Domain kExpDomain{-746.0, 1456.0};
inline constexpr Domain kExpFloatDomain{-105.0, 194.0};
inline constexpr Domain kLogDomain{0.25, 3.75};
inline constexpr Domain kErfDomain{-6.5, 13.0};
inline constexpr Domain kErfFloatDomain{-4.5, 9.0};

inline constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
inline constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

[[nodiscard]] constexpr std::uint64_t fold_word(std::uint64_t digest, std::uint64_t word) noexcept {
    for (int byte = 0; byte < 8; ++byte) {
        digest = (digest ^ ((word >> (8 * byte)) & 0xFFu)) * kFnvPrime;
    }
    return digest;
}

// The digest of a function of double on its sample.  The unit is a template
// parameter, so each unit has its own body of the walk too.
// O(2 * kSampleCount).
template <FpUnit Unit, class Function>
[[nodiscard]] std::uint64_t digest_of_double(Function const& function, Domain domain) noexcept {
    std::uint64_t digest = kFnvOffset;
    for (std::uint32_t index = 0; index < kSampleCount; ++index) {
        std::uint64_t const pattern = (std::uint64_t{index} + 1) * 0x9E3779B97F4A7C15ULL;
        digest = fold_word(digest, ::fixy::bit_cast<std::uint64_t>(function(::fixy::bit_cast<double>(pattern))));
    }
    for (std::uint32_t index = 0; index < kSampleCount; ++index) {
        double const value = domain.start + domain.width * (static_cast<double>(index) + 0.5) / kSampleCount;
        digest = fold_word(digest, ::fixy::bit_cast<std::uint64_t>(function(value)));
    }
    return digest;
}

// The digest of a function of float on its sample.  O(2 * kSampleCount).
template <FpUnit Unit, class Function>
[[nodiscard]] std::uint64_t digest_of_float(Function const& function, Domain domain) noexcept {
    std::uint64_t digest = kFnvOffset;
    for (std::uint32_t index = 0; index < kSampleCount; ++index) {
        std::uint32_t const pattern = (index + 1u) * 0x9E3779B9u;
        digest = fold_word(digest, ::fixy::bit_cast<std::uint32_t>(function(::fixy::bit_cast<float>(pattern))));
    }
    for (std::uint32_t index = 0; index < kSampleCount; ++index) {
        double const value = domain.start + domain.width * (static_cast<double>(index) + 0.5) / kSampleCount;
        digest = fold_word(digest, ::fixy::bit_cast<std::uint32_t>(function(static_cast<float>(value))));
    }
    return digest;
}

template <FpUnit Unit>
[[gnu::noinline, nodiscard]] double probe_exp_double(double value) noexcept {
    return ::fixy::fp::exp(value);
}

template <FpUnit Unit>
[[gnu::noinline, nodiscard]] double probe_exp_slow(double value) noexcept {
    return ::fixy::fp::detail::exp_accurate_(value);
}

// The digest of exp(double) on its sample, then on the hard inputs through
// exp, then on the hard inputs in the domain of the slow path through the
// slow path.  O(2 * kSampleCount).
template <FpUnit Unit>
[[nodiscard]] std::uint64_t digest_of_exp_double() noexcept {
    std::uint64_t digest = digest_of_double<Unit>(probe_exp_double<Unit>, kExpDomain);
    for (::test_fp_exp::HardCase const& item : ::test_fp_exp::kExpHardCases) {
        digest = fold_word(
            digest, ::fixy::bit_cast<std::uint64_t>(probe_exp_double<Unit>(::fixy::bit_cast<double>(item.input))));
    }
    for (::test_fp_exp::HardCase const& item : ::test_fp_exp::kExpHardCases) {
        double const input = ::fixy::bit_cast<double>(item.input);
        double const magnitude = input < 0.0 ? -input : input;
        bool const is_in_domain = !(magnitude < 0x1p-54) && input > ::fixy::fp::detail::kExpZeroAtOrBelow
                               && !(input > ::fixy::fp::detail::kExpLargestFinite);
        if (is_in_domain) digest = fold_word(digest, ::fixy::bit_cast<std::uint64_t>(probe_exp_slow<Unit>(input)));
    }
    return digest;
}

template <FpUnit Unit>
[[gnu::noinline, nodiscard]] float probe_exp_float(float value) noexcept {
    return ::fixy::fp::exp(value);
}

template <FpUnit Unit>
[[gnu::noinline, nodiscard]] double probe_log_double(double value) noexcept {
    return ::fixy::fp::log(value);
}

template <FpUnit Unit>
[[gnu::noinline, nodiscard]] double probe_log_slow(double value) noexcept {
    return ::fixy::fp::detail::log_accurate_(value);
}

// The digest of log(double) on its sample, then on the hard inputs through
// log, then on the hard inputs through the slow path.  The slow path takes
// each positive finite input, and each hard input is one.  O(2 * kSampleCount).
template <FpUnit Unit>
[[nodiscard]] std::uint64_t digest_of_log_double() noexcept {
    std::uint64_t digest = digest_of_double<Unit>(probe_log_double<Unit>, kLogDomain);
    for (::test_fp_log::HardCase const& item : ::test_fp_log::kLogHardCases) {
        digest = fold_word(
            digest, ::fixy::bit_cast<std::uint64_t>(probe_log_double<Unit>(::fixy::bit_cast<double>(item.input))));
    }
    for (::test_fp_log::HardCase const& item : ::test_fp_log::kLogHardCases) {
        digest = fold_word(digest,
                           ::fixy::bit_cast<std::uint64_t>(probe_log_slow<Unit>(::fixy::bit_cast<double>(item.input))));
    }
    return digest;
}

template <FpUnit Unit>
[[gnu::noinline, nodiscard]] float probe_log_float(float value) noexcept {
    return ::fixy::fp::log(value);
}

template <FpUnit Unit>
[[gnu::noinline, nodiscard]] double probe_erf_double(double value) noexcept {
    return ::fixy::fp::erf(value);
}

template <FpUnit Unit>
[[gnu::noinline, nodiscard]] float probe_erf_float(float value) noexcept {
    return ::fixy::fp::erf(value);
}

template <FpUnit Unit>
[[nodiscard]] FpDigests fp_digests_of_this_unit() noexcept {
    FpDigests digests{};
    digests.exp_double = digest_of_exp_double<Unit>();
    digests.exp_float = digest_of_float<Unit>(probe_exp_float<Unit>, kExpFloatDomain);
    digests.log_double = digest_of_log_double<Unit>();
    digests.log_float = digest_of_float<Unit>(probe_log_float<Unit>, kLogDomain);
    digests.erf_double = digest_of_double<Unit>(probe_erf_double<Unit>, kErfDomain);
    digests.erf_float = digest_of_float<Unit>(probe_erf_float<Unit>, kErfFloatDomain);
    return digests;
}

// The digests of the units of the levels.  A unit runs only on a host of its
// level.
[[nodiscard]] FpDigests fp_digests_of_v1_unit() noexcept;
[[nodiscard]] FpDigests fp_digests_of_v2_unit() noexcept;
[[nodiscard]] FpDigests fp_digests_of_v3_unit() noexcept;
[[nodiscard]] FpDigests fp_digests_of_v4_unit() noexcept;

}  // namespace test_fp_elementary
