// The sweep of exp, log and erf of fixy/fp against the pinned oracles.
//
//   bench_fp_sweep float FUNCTION    each of the 2^32 floats against CORE-MATH.
//                                    Each result must be the correctly rounded
//                                    result.  For a NaN of the oracle, the
//                                    result must be the canonical NaN
//   bench_fp_sweep double FUNCTION   a sample of doubles against MPFR: the
//                                    largest error in units of the last place
//                                    must be at most the bound of the header,
//                                    and the sweep also counts the results
//                                    that are not the correctly rounded result
//                                    of CORE-MATH.  For a correctly rounded
//                                    function that count must be zero
//   bench_fp_sweep hard exp|log      each input of the list on standard input,
//                                    a list .wc of CORE-MATH with one input in
//                                    hexadecimal on each line, against
//                                    CORE-MATH and the round of MPFR, through
//                                    the function and through its slow path on
//                                    its own.  For log, the relative error of
//                                    the three doubles of the slow path before
//                                    the round must be at most its eps_acc
//   bench_fp_sweep fast-bound exp    the relative error of the pair hi + lo of
//                                    the fast path of exp(double) against MPFR
//                                    on a sample: it must be at most eps_fast
//                                    of the budget of fixy/fp/Exp.h.  The
//                                    sweep also gives the rate of the slow path
//   bench_fp_sweep fast-bound log    on a sample, the absolute error of the
//                                    pair of the fast path of log(double), the
//                                    relative error of the pair of its path
//                                    near 1, and the relative error of the
//                                    three doubles of its slow path, against
//                                    MPFR: each must be at most its bound of
//                                    the budget of fixy/fp/Log.h.  The sweep
//                                    also gives the rates of the slow path
//   bench_fp_sweep subnormal log     2^28 subnormal inputs through log and
//                                    through its slow path against the round
//                                    of MPFR, and the distance of log(x) to the
//                                    nearest midpoint, from the three doubles
//                                    of the slow path: the sweep gives the
//                                    closest input
//
//   bench_fp_sweep fma TYPE          the fma of a target with no fused
//                                    multiply-add against the exact form and
//                                    the instruction, for float or double
//                                    (bench/fp_sweep_fma.h)
//
// FUNCTION is exp, log or erf.  The sample of a double function has regions:
// a walk of the bit patterns of the domain, and a walk of the values of each
// hard region (near 0, near the edges of overflow and underflow, the
// subnormal inputs, near 1 for log, and for erf the edge of the odd
// polynomial at 1/2, the first two intervals and the edge to ±1).  Each region
// has kRegionCount inputs from a Weyl step, so the sample is the same on each
// run.
//
// Only the preset bench builds the sweep (CRUCIBLE_FP_SWEEP of
// bench/CMakeLists.txt), and the nightly entry runs its tests in the build
// of that preset.  Exit 0 when each result holds its bound, 1 when
// one does not, 2 on a usage error.  Complexity: 2^32 calls of each side for a
// float function, and 2^21 inputs of each region for a double function.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/fp/Erf.h>
#include <fixy/fp/Exp.h>
#include <fixy/fp/Log.h>
#include <fixy/os/Args.h>
#include <fixy/os/Stdio.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include "fp_sweep_fma.h"
#include "fp_sweep_oracles.h"

#include <cstdint>

namespace fp_sweep {

namespace {

// The count of the inputs of one region of a double sweep.
constexpr std::uint64_t kRegionCount = std::uint64_t{1} << 21;
// The count of the mismatches that the float sweep writes.
constexpr std::uint64_t kShownMismatches = 16;
// The rounds of an fma sweep: about 13 cases of a float and 26 of a double
// in each round (bench/fp_sweep_fma.cpp).
constexpr std::uint64_t kFmaRounds = std::uint64_t{1} << 24;

// The bound of each double function in units of the last place, from the
// comments of fixy/fp/Exp.h, Log.h and Erf.h.
[[nodiscard]] constexpr double bound_of(Function function) noexcept {
    switch (function) {
        case Function::Exp:
            return 0.5;
        case Function::Log:
            return 0.5;
        case Function::Erf:
            return 0.8;
        default:
            ::fixy::unreachable();
    }
}

// A correctly rounded double function: each result of its sweep must be the
// result of CORE-MATH.
[[nodiscard]] constexpr bool is_correctly_rounded(Function function) noexcept {
    return function == Function::Exp || function == Function::Log;
}

[[nodiscard]] ::fixy::TextView name_of(Function function) noexcept {
    switch (function) {
        case Function::Exp:
            return "exp";
        case Function::Log:
            return "log";
        case Function::Erf:
            return "erf";
        default:
            ::fixy::unreachable();
    }
}

constexpr ::fixy::FixedArray<Function, 3> kFunctions{Function::Exp, Function::Log, Function::Erf};

[[nodiscard]] ::fixy::Option<Function> function_of(::fixy::TextView text) noexcept {
    for (Function const function : kFunctions) {
        if (text == name_of(function)) return ::fixy::some(function);
    }
    return ::fixy::none;
}

[[nodiscard]] float ours(Function function, float input) noexcept {
    switch (function) {
        case Function::Exp:
            return ::fixy::fp::exp(input);
        case Function::Log:
            return ::fixy::fp::log(input);
        case Function::Erf:
            return ::fixy::fp::erf(input);
        default:
            ::fixy::unreachable();
    }
}

[[nodiscard]] double ours(Function function, double input) noexcept {
    switch (function) {
        case Function::Exp:
            return ::fixy::fp::exp(input);
        case Function::Log:
            return ::fixy::fp::log(input);
        case Function::Erf:
            return ::fixy::fp::erf(input);
        default:
            ::fixy::unreachable();
    }
}

// The result of fixy/fp agrees with the oracle when their bits are the same.
// For a NaN of the oracle, the result must have the bits of the canonical
// NaN quiet_nan_of<T>, because the NaN of the oracle has the bits of its own
// library.
[[nodiscard]] bool agree(float got, float oracle) noexcept {
    float const expected = ::fixy::fp::is_nan(oracle) ? ::fixy::quiet_nan_of<float> : oracle;
    return ::fixy::bit_cast<std::uint32_t>(got) == ::fixy::bit_cast<std::uint32_t>(expected);
}

[[nodiscard]] bool agree(double got, double oracle) noexcept {
    double const expected = ::fixy::fp::is_nan(oracle) ? ::fixy::quiet_nan_of<double> : oracle;
    return ::fixy::bit_cast<std::uint64_t>(got) == ::fixy::bit_cast<std::uint64_t>(expected);
}

// Each of the 2^32 floats.  O(2^32).
[[nodiscard]] int sweep_float(Function function) noexcept {
    std::uint64_t mismatches = 0;
    for (std::uint64_t word = 0; word < (std::uint64_t{1} << 32); ++word) {
        float const input = ::fixy::bit_cast<float>(static_cast<std::uint32_t>(word));
        float const got = ours(function, input);
        float const expected = correctly_rounded(function, input);
        if (agree(got, expected)) continue;
        if (mismatches < kShownMismatches) {
            ::fixy::report(::fixy::Sink::Out, "  {} of 0x{} gives 0x{}, and the correct round is 0x{}\n",
                           name_of(function), ::fixy::Hex<8>{static_cast<std::uint32_t>(word)},
                           ::fixy::Hex<8>{::fixy::bit_cast<std::uint32_t>(got)},
                           ::fixy::Hex<8>{::fixy::bit_cast<std::uint32_t>(expected)});
        }
        ++mismatches;
    }
    ::fixy::report(::fixy::Sink::Out, "bench_fp_sweep: {} of float: {} of the 2^32 inputs are not correctly rounded\n",
                   name_of(function), mismatches);
    return mismatches == 0 ? 0 : 1;
}

// A region of a double sweep: inputs from start to start + width, as values
// or as bit patterns.
enum class RegionKind : std::uint8_t {
    Values,
    Bits,
};

struct Region {
    RegionKind kind = RegionKind::Values;
    double start = 0.0;
    double width = 0.0;
    std::uint64_t first_bits = 0;
    std::uint64_t bit_count = 0;
};

[[nodiscard]] constexpr Region values(double start, double end) noexcept {
    return Region{RegionKind::Values, start, end - start, 0, 0};
}

[[nodiscard]] constexpr Region bits(std::uint64_t first, std::uint64_t last) noexcept {
    return Region{RegionKind::Bits, 0.0, 0.0, first, last - first + 1};
}

// The input of a region at an index of its walk.
[[nodiscard]] double input_of(Region const& region, std::uint64_t index) noexcept {
    std::uint64_t const step = (index + 1) * 0x9E3779B97F4A7C15ULL;
    if (region.kind == RegionKind::Bits) return ::fixy::bit_cast<double>(region.first_bits + step % region.bit_count);
    double const unit = static_cast<double>(step >> 11) * 0x1p-53;
    return region.start + region.width * unit;
}

// The regions of each function.
constexpr ::fixy::FixedArray<Region, 7> kExpRegions{
    bits(0x0000000000000000ULL, 0x40862E42FEFA39EFULL),  // each positive input with a finite result
    bits(0x8000000000000000ULL, 0xC0874910D52D3051ULL),  // each negative input with a result above zero
    values(-746.0, 710.0),
    values(709.0, 709.7827128933840),
    values(-745.1332191019411, -708.39),
    values(-1.0, 1.0),
    values(-0x1p-20, 0x1p-20),
};

constexpr ::fixy::FixedArray<Region, 7> kLogRegions{
    bits(0x0000000000000001ULL, 0x7FEFFFFFFFFFFFFFULL),  // each positive finite input
    bits(0x0000000000000001ULL, 0x000FFFFFFFFFFFFFULL),  // each subnormal input
    values(1.0 - 0x1p-4, 1.0 + 0x1.09p-4),
    values(1.0 - 0x1p-20, 1.0 + 0x1p-20),
    values(0.5, 2.0),
    values(0x1.6p-1, 0x1.6p0),
    values(1.0e300, 1.7976931348623157e308),
};

constexpr ::fixy::FixedArray<Region, 8> kErfRegions{
    bits(0x0000000000000000ULL, 0x4017AFB48DC96627ULL),  // each positive input below the edge to 1
    bits(0x0000000000000001ULL, 0x000FFFFFFFFFFFFFULL),  // each subnormal input
    values(-6.0, 6.0),
    values(5.5, 5.9215871957945075),
    values(0.25, 0.5),
    values(0.4999, 0.5001),
    values(-0x1p-20, 0x1p-20),
    values(0.5, 0.75),
};

// The largest error of a region and the count of its results that are not
// correctly rounded.  O(kRegionCount).
struct RegionResult {
    double largest = 0.0;
    double largest_input = 0.0;
    std::uint64_t not_correctly_rounded = 0;
};

[[nodiscard]] RegionResult sweep_region(Function function, Region const& region) noexcept {
    RegionResult result{};
    for (std::uint64_t index = 0; index < kRegionCount; ++index) {
        double const input = input_of(region, index);
        double const got = ours(function, input);
        if (!agree(got, correctly_rounded(function, input))) ++result.not_correctly_rounded;
        if (::fixy::fp::is_nan(got) || ::fixy::fp::is_inf(got)) continue;
        double const error =
            error_in_ulp(function, ::fixy::side_as<side::Input>(input), ::fixy::side_as<side::Result>(got));
        if (error > result.largest) {
            result.largest = error;
            result.largest_input = input;
        }
    }
    return result;
}

template <std::size_t Count>
[[nodiscard]] int sweep_regions(Function function, ::fixy::FixedArray<Region, Count> const& regions) noexcept {
    double largest = 0.0;
    std::uint64_t not_correctly_rounded = 0;
    std::size_t position = 0;
    for (Region const& region : regions) {
        RegionResult const result = sweep_region(function, region);
        ::fixy::report(::fixy::Sink::Out, "  region {}: largest error {} ulp at {}, {} not correctly rounded\n",
                       position, ::fixy::Fixed<4>{result.largest}, result.largest_input, result.not_correctly_rounded);
        if (result.largest > largest) largest = result.largest;
        not_correctly_rounded += result.not_correctly_rounded;
        ++position;
    }
    bool const holds = largest <= bound_of(function) && (!is_correctly_rounded(function) || not_correctly_rounded == 0);
    ::fixy::report(::fixy::Sink::Out,
                   "bench_fp_sweep: {} of double: largest error {} ulp, bound {} ulp, {} of {} inputs not correctly "
                   "rounded\n",
                   name_of(function), ::fixy::Fixed<4>{largest}, ::fixy::Fixed<2>{bound_of(function)},
                   not_correctly_rounded, kRegionCount * Count);
    return holds ? 0 : 1;
}

[[nodiscard]] int sweep_double(Function function) noexcept {
    switch (function) {
        case Function::Exp:
            return sweep_regions(function, kExpRegions);
        case Function::Log:
            return sweep_regions(function, kLogRegions);
        case Function::Erf:
            return sweep_regions(function, kErfRegions);
        default:
            ::fixy::unreachable();
    }
}

// The bound of the sweep fast-bound: eps_fast = 2^-65.20 of the budget of
// fixy/fp/Exp.h, rounded up to 2^-65.19.
constexpr double kExpFastBound = 0x1.c0d2771113e65p-66;

// The domain of the slow path of exp(double): 2^-54 <= |x|, and a result from
// 2^-1074 up to the largest finite value.
[[nodiscard]] bool is_in_slow_domain(double value) noexcept {
    double const magnitude = value < 0.0 ? -value : value;
    return !(magnitude < 0x1p-54) && value > ::fixy::fp::detail::kExpZeroAtOrBelow
        && !(value > ::fixy::fp::detail::kExpLargestFinite);
}

// 2^exponent for -1022 <= exponent <= 1023.
[[nodiscard]] double power_of_two(std::int32_t exponent) noexcept {
    return ::fixy::bit_cast<double>(static_cast<std::uint64_t>(exponent + 1023) << 52);
}

[[nodiscard]] std::int32_t hex_digit_of(char character) noexcept {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

// The double of the first token of a line of a list .wc of CORE-MATH: an
// optional sign, 0x, the digit 0 or 1 or no digit, an optional point and at
// most 13 hexadecimal digits, p and a decimal exponent.  A line that does not start
// with such a token, as a comment or a NaN, gives no value.  The value is
// exact: the mantissa has at most 53 bits, and two multiplies by powers of
// two give the exponent.
[[nodiscard]] ::fixy::Option<double> parse_hex_double(::fixy::TextView line) noexcept {
    enum class Step : std::uint8_t {
        Sign,
        Zero,
        Ex,
        Lead,
        AfterLead,
        Fraction,
        ExponentSign,
        Exponent,
        Done
    };
    Step step = Step::Sign;
    bool is_negative = false;
    bool is_exponent_negative = false;
    std::uint64_t mantissa = 0;
    std::int32_t fraction_digits = 0;
    std::int32_t exponent = 0;
    std::int32_t exponent_digits = 0;
    for (char const character : line) {
        if (step == Step::Done) break;
        std::int32_t const digit = hex_digit_of(character);
        switch (step) {
            case Step::Sign:
                if (character == '-' || character == '+') {
                    is_negative = character == '-';
                    step = Step::Zero;
                } else if (character == '0') {
                    step = Step::Ex;
                } else {
                    return ::fixy::none;
                }
                break;
            case Step::Zero:
                if (character != '0') return ::fixy::none;
                step = Step::Ex;
                break;
            case Step::Ex:
                if (character != 'x' && character != 'X') return ::fixy::none;
                step = Step::Lead;
                break;
            case Step::Lead:
                if (character == '.') {
                    step = Step::Fraction;
                    break;
                }
                if (digit < 0 || digit > 1) return ::fixy::none;
                mantissa = static_cast<std::uint64_t>(digit);
                step = Step::AfterLead;
                break;
            case Step::AfterLead:
            case Step::Fraction:
                if (character == 'p' || character == 'P') {
                    step = Step::ExponentSign;
                } else if (step == Step::AfterLead && character == '.') {
                    step = Step::Fraction;
                } else if (step == Step::Fraction && digit >= 0 && fraction_digits < 13) {
                    mantissa = mantissa * 16 + static_cast<std::uint64_t>(digit);
                    ++fraction_digits;
                } else {
                    return ::fixy::none;
                }
                break;
            case Step::ExponentSign:
                if (character == '-' || character == '+') {
                    is_exponent_negative = character == '-';
                    step = Step::Exponent;
                    break;
                }
                [[fallthrough]];
            case Step::Exponent:
                if (character >= '0' && character <= '9' && exponent_digits < 5) {
                    exponent = exponent * 10 + (character - '0');
                    ++exponent_digits;
                    step = Step::Exponent;
                } else if (step == Step::Exponent && exponent_digits > 0) {
                    step = Step::Done;
                } else {
                    return ::fixy::none;
                }
                break;
            case Step::Done:
            default:
                break;
        }
    }
    if (step != Step::Exponent && step != Step::Done) return ::fixy::none;
    if (exponent_digits == 0) return ::fixy::none;
    std::int32_t const scale = (is_exponent_negative ? -exponent : exponent) - 4 * fraction_digits;
    if (scale < -1126 || scale > 1023) return ::fixy::none;
    double value = static_cast<double>(mantissa);
    if (scale < -1000) {
        value = value * power_of_two(-1000) * power_of_two(scale + 1000);
    } else {
        value *= power_of_two(scale);
    }
    return ::fixy::some(is_negative ? -value : value);
}

// The reader of the list: a line of a list .wc holds far fewer than 256
// characters.  It has static storage, as the one reader of standard input.
::fixy::os::LineReader<256> g_list_reader;

// The bound of the three doubles of the slow path of log: eps_acc = 2^-137.16
// of the budget of fixy/fp/Log.h, rounded up to 2^-137.15.
constexpr double kLogSlowBound = 0x1.cd70b35cd634cp-138;

// The domain of the slow path of log(double): each positive finite input.
[[nodiscard]] bool is_in_log_slow_domain(double value) noexcept {
    return value > 0.0 && ::fixy::bit_cast<std::uint64_t>(value) < 0x7FF0000000000000ULL;
}

// The three doubles of the slow path of log, for a positive finite input.
[[nodiscard]] Triple log_slow_triple(double value) noexcept {
    ::fixy::fp::detail::LogSum const sum = ::fixy::fp::detail::log_accurate_sum_(value);
    return Triple{sum.high, sum.middle, sum.low};
}

// The slow path on its own, or none for an input out of its domain.
[[nodiscard]] ::fixy::Option<double> slow_path_of(Function function, double input) noexcept {
    if (function == Function::Exp) {
        if (!is_in_slow_domain(input)) return ::fixy::none;
        return ::fixy::some(::fixy::fp::detail::exp_accurate_(input));
    }
    if (!is_in_log_slow_domain(input)) return ::fixy::none;
    return ::fixy::some(::fixy::fp::detail::log_accurate_(input));
}

// The counts of the sweep hard.
struct HardCounts {
    std::uint64_t inputs = 0;
    std::uint64_t function_wrong = 0;
    std::uint64_t slow_wrong = 0;
    std::uint64_t oracles_differ = 0;
    double largest_slow_error = 0.0;
    double largest_slow_input = 0.0;
};

void check_hard_input(Function function, double input, HardCounts& counts) noexcept {
    ++counts.inputs;
    double const expected = correctly_rounded(function, input);
    double const by_mpfr = rounded_by_mpfr(function, ::fixy::side_as<side::Input>(input));
    // A NaN of each oracle has the bits of its own library, as for a negative
    // input of log.
    bool const are_both_nan = ::fixy::fp::is_nan(by_mpfr) && ::fixy::fp::is_nan(expected);
    if (!are_both_nan && !agree(by_mpfr, expected)) {
        if (counts.oracles_differ < kShownMismatches) {
            ::fixy::report(::fixy::Sink::Out, "  the oracles differ at {}: CORE-MATH {}, MPFR {}\n", input, expected,
                           by_mpfr);
        }
        ++counts.oracles_differ;
    }
    double const got = ours(function, input);
    if (!agree(got, expected)) {
        if (counts.function_wrong < kShownMismatches) {
            ::fixy::report(::fixy::Sink::Out, "  {} of {} gives {}, and the correct round is {}\n", name_of(function),
                           input, got, expected);
        }
        ++counts.function_wrong;
    }
    slow_path_of(function, input)
        .match(
            [&](double slow) {
                if (agree(slow, expected)) return;
                if (counts.slow_wrong < kShownMismatches) {
                    ::fixy::report(::fixy::Sink::Out, "  the slow path of {} gives {}, and the correct round is {}\n",
                                   input, slow, expected);
                }
                ++counts.slow_wrong;
            },
            [] {});
    // log(1) = 0 has no relative error, and its slow path gives three zeros.
    if (function != Function::Log || !is_in_log_slow_domain(input)
        || ::fixy::bit_cast<std::uint64_t>(input) == ::fixy::bit_cast<std::uint64_t>(1.0)) {
        return;
    }
    double const error = error_of_triple(function, ::fixy::side_as<side::Input>(input), log_slow_triple(input));
    if (error > counts.largest_slow_error) {
        counts.largest_slow_error = error;
        counts.largest_slow_input = input;
    }
}

// Each input of the list on standard input.  O(the count of the lines).
[[nodiscard]] int sweep_hard(Function function) noexcept {
    ::fixy::InitLoadCtx const ctx{::foundation::effects::host::InitOwner::mint_init_context()};
    HardCounts counts{};
    bool has_ended = false;
    bool has_failed = false;
    while (!has_ended && !has_failed) {
        g_list_reader.next_line(ctx).match(
            [&](::fixy::Option<::fixy::TextView> line) {
                line.match(
                    [&](::fixy::TextView text) {
                        parse_hex_double(text).match([&](double input) { check_hard_input(function, input, counts); },
                                                     [] {});
                    },
                    [&]() { has_ended = true; });
            },
            [&](::fixy::os::LineError) { has_failed = true; });
    }
    if (has_failed) {
        ::fixy::report(::fixy::Sink::Err, "bench_fp_sweep: the read of the list on standard input failed\n");
        return 2;
    }
    ::fixy::report(::fixy::Sink::Out,
                   "bench_fp_sweep: {} of the hard list: {} inputs, {} not correctly rounded through {}, {} through "
                   "the slow path, {} where CORE-MATH and MPFR differ\n",
                   name_of(function), counts.inputs, counts.function_wrong, name_of(function), counts.slow_wrong,
                   counts.oracles_differ);
    bool holds =
        counts.inputs > 0 && counts.function_wrong == 0 && counts.slow_wrong == 0 && counts.oracles_differ == 0;
    if (function == Function::Log) {
        double const in_bits = ::fixy::fp::log(counts.largest_slow_error) / 0x1.62e42fefa39efp-1;
        ::fixy::report(::fixy::Sink::Out,
                       "bench_fp_sweep: the three doubles of the slow path of log: largest relative error 2^{} at "
                       "{}, bound 2^-137.15\n",
                       ::fixy::Fixed<3>{in_bits}, counts.largest_slow_input);
        holds = holds && counts.largest_slow_error <= kLogSlowBound;
    }
    return holds ? 0 : 1;
}

// The regions of the sweep fast-bound: each input of the fast path.
constexpr ::fixy::FixedArray<Region, 4> kExpFastRegions{
    bits(0x3C90000000000000ULL, 0x407FFFFFFFFFFFFFULL),  // 2^-54 <= x < 512
    bits(0xBC90000000000000ULL, 0xC07FFFFFFFFFFFFFULL),  // -512 < x <= -2^-54
    values(-512.0, 512.0),
    values(-1.0, 1.0),
};

// The pair of the fast path on each input of the regions.  O(4 * kRegionCount).
[[nodiscard]] int sweep_fast_bound_exp() noexcept {
    double largest = 0.0;
    double largest_input = 0.0;
    std::uint64_t inputs = 0;
    std::uint64_t slow_calls = 0;
    for (Region const& region : kExpFastRegions) {
        for (std::uint64_t index = 0; index < kRegionCount; ++index) {
            double const input = input_of(region, index);
            double const magnitude = input < 0.0 ? -input : input;
            if (magnitude < 0x1p-54 || !(magnitude < 512.0)) continue;
            ++inputs;
            ::fixy::fp::detail::DoubleWord const pair = ::fixy::fp::detail::exp_pair_(input, 0);
            double const margin = pair.high * ::fixy::fp::detail::kExpMargin;
            double const upper = pair.high + (pair.low + margin);
            double const lower = pair.high + (pair.low - margin);
            if (::fixy::bit_cast<std::uint64_t>(upper) != ::fixy::bit_cast<std::uint64_t>(lower)) ++slow_calls;
            double const error =
                error_of_pair(Function::Exp, ::fixy::side_as<side::Input>(input), Pair{pair.high, pair.low});
            if (error > largest) {
                largest = error;
                largest_input = input;
            }
        }
    }
    double const in_bits = ::fixy::fp::log(largest) / 0x1.62e42fefa39efp-1;
    ::fixy::report(::fixy::Sink::Out,
                   "bench_fp_sweep: the fast pair of exp: {} inputs, largest relative error 2^{} at {}, bound "
                   "2^-65.19, {} inputs take the slow path\n",
                   inputs, ::fixy::Fixed<3>{in_bits}, largest_input, slow_calls);
    return largest <= kExpFastBound ? 0 : 1;
}

// The bounds of the sweep fast-bound of log, from the budget of
// fixy/fp/Log.h: eps_fast = 2^-69.20 absolute, rounded up to 2^-69.19, and
// eps_near = 2^-69.41 relative, rounded up to 2^-69.40.
constexpr double kLogFastBound = 0x1.c0d2771113e65p-70;
constexpr double kLogNearBound = 0x1.8406003b2ae42p-70;

// The regions of the sweep fast-bound of log: each positive finite input,
// e = 0 and e = 1 with each interval of the first reduction, the path near 1,
// the inputs near 1 with |k2| <= 2, the subnormal inputs, and the largest
// exponents.
constexpr ::fixy::FixedArray<Region, 7> kLogFastRegions{
    bits(0x0000000000000001ULL, 0x7FEFFFFFFFFFFFFFULL),
    values(0x1.6a09e667f3bccp-1, 0x1.6a09e667f3bcdp0),
    values(0x1.6a09e667f3bccp0, 0x1.6a09e667f3bcdp1),
    values(1.0 - 0x1p-9, 1.0 + 0x1p-9),
    values(1.0 - 0x1p-15, 1.0 + 0x1p-15),
    bits(0x0000000000000001ULL, 0x000FFFFFFFFFFFFFULL),
    values(0x1p1022, 1.7976931348623157e308),
};

// The largest error of a part of the sweep fast-bound of log.
struct LargestError {
    double value = 0.0;
    double input = 0.0;
};

void take_error(LargestError& largest, double error, double input) noexcept {
    if (error > largest.value) {
        largest.value = error;
        largest.input = input;
    }
}

// The pair of the fast path or of the path near 1, and the three doubles of
// the slow path, on each input of the regions.  O(7 * kRegionCount).
[[nodiscard]] int sweep_fast_bound_log() noexcept {
    LargestError fast{};
    LargestError near{};
    LargestError slow{};
    std::uint64_t inputs = 0;
    std::uint64_t fast_slow_calls = 0;
    std::uint64_t near_slow_calls = 0;
    std::uint64_t const one_bits = ::fixy::bit_cast<std::uint64_t>(1.0);
    for (Region const& region : kLogFastRegions) {
        for (std::uint64_t index = 0; index < kRegionCount; ++index) {
            double const input = input_of(region, index);
            std::uint64_t bits_of_input = ::fixy::bit_cast<std::uint64_t>(input);
            if (!is_in_log_slow_domain(input) || bits_of_input == one_bits) continue;
            ++inputs;
            ::fixy::Side<side::Input, double> const side_input = ::fixy::side_as<side::Input>(input);
            take_error(slow, error_of_triple(Function::Log, side_input, log_slow_triple(input)), input);
            bool const is_near = bits_of_input - ::fixy::fp::detail::kLogNearLow
                               < ::fixy::fp::detail::kLogNearHigh - ::fixy::fp::detail::kLogNearLow;
            if (is_near) {
                ::fixy::fp::detail::DoubleWord const pair = ::fixy::fp::detail::log_near_pair_(input);
                double const margin = (input - 1.0) * ::fixy::fp::detail::kLogNearMargin;
                double const upper = pair.high + (pair.low + margin);
                double const lower = pair.high + (pair.low - margin);
                if (::fixy::bit_cast<std::uint64_t>(upper) != ::fixy::bit_cast<std::uint64_t>(lower)) ++near_slow_calls;
                take_error(near, error_of_pair(Function::Log, side_input, Pair{pair.high, pair.low}), input);
                continue;
            }
            if ((bits_of_input >> 52) == 0) bits_of_input = ::fixy::fp::detail::log_normal_bits_(bits_of_input);
            ::fixy::fp::detail::DoubleWord const pair =
                ::fixy::fp::detail::log_pair_(::fixy::fp::detail::log_reduce_(bits_of_input));
            double const upper = pair.high + (pair.low + ::fixy::fp::detail::kLogMargin);
            double const lower = pair.high + (pair.low - ::fixy::fp::detail::kLogMargin);
            if (::fixy::bit_cast<std::uint64_t>(upper) != ::fixy::bit_cast<std::uint64_t>(lower)) ++fast_slow_calls;
            // The absolute error: the relative error times |log(x)|, which is at
            // most |hi| (1 + 2^-50).
            double const magnitude = pair.high < 0.0 ? -pair.high : pair.high;
            double const relative = error_of_pair(Function::Log, side_input, Pair{pair.high, pair.low});
            take_error(fast, relative * magnitude * (1.0 + 0x1p-50), input);
        }
    }
    double const ln2 = 0x1.62e42fefa39efp-1;
    ::fixy::report(::fixy::Sink::Out,
                   "bench_fp_sweep: log of {} inputs: the fast pair, largest absolute error 2^{} at {}, bound "
                   "2^-69.19, {} slow calls; the pair near 1, largest relative error 2^{} at {}, bound 2^-69.40, {} "
                   "slow calls; the slow path, largest relative error 2^{} at {}, bound 2^-137.15\n",
                   inputs, ::fixy::Fixed<3>{::fixy::fp::log(fast.value) / ln2}, fast.input, fast_slow_calls,
                   ::fixy::Fixed<3>{::fixy::fp::log(near.value) / ln2}, near.input, near_slow_calls,
                   ::fixy::Fixed<3>{::fixy::fp::log(slow.value) / ln2}, slow.input);
    bool const holds = fast.value <= kLogFastBound && near.value <= kLogNearBound && slow.value <= kLogSlowBound;
    return holds ? 0 : 1;
}

// The count of the inputs of the sweep subnormal.
constexpr std::uint64_t kSubnormalCount = std::uint64_t{1} << 28;

// The distance of log(x) to the nearest midpoint in units of the last place
// of the result, from the three doubles of the slow path and their round.
// The three doubles are within 2^-136 of log(x) in the mantissa, far below
// each distance that the sweep reports.  delta = log(x) - result is a pair:
// high - result is exact, as the two are at most one unit apart.
[[nodiscard]] double midpoint_distance(Triple triple, double result) noexcept {
    double const sign = result < 0.0 ? -1.0 : 1.0;
    double const magnitude = sign * result;
    ::fixy::fp::detail::DoubleWord const head =
        ::fixy::fp::detail::two_sum_(sign * triple.high - magnitude, sign * triple.middle);
    double const delta_low = head.low + sign * triple.low;
    double const unit = ::fixy::bit_cast<double>(::fixy::bit_cast<std::uint64_t>(magnitude) + 1) - magnitude;
    bool const is_high_zero = (::fixy::bit_cast<std::uint64_t>(head.high) << 1) == 0;
    bool const is_above = head.high > 0.0 || (is_high_zero && delta_low > 0.0);
    double const half =
        is_above ? unit * 0.5
                 : (magnitude - ::fixy::bit_cast<double>(::fixy::bit_cast<std::uint64_t>(magnitude) - 1)) * 0.5;
    double const delta_high = is_above ? head.high : -head.high;
    double const delta_rest = is_above ? delta_low : -delta_low;
    return ((half - delta_high) - delta_rest) / unit;
}

// 2^28 subnormal inputs from a Weyl walk of their bits, through log and its
// slow path against the round of MPFR.  O(kSubnormalCount).
[[nodiscard]] int sweep_subnormal_log() noexcept {
    std::uint64_t log_wrong = 0;
    std::uint64_t slow_wrong = 0;
    double closest = 1.0;
    double closest_input = 0.0;
    Region const region = bits(0x0000000000000001ULL, 0x000FFFFFFFFFFFFFULL);
    for (std::uint64_t index = 0; index < kSubnormalCount; ++index) {
        double const input = input_of(region, index);
        double const expected = rounded_by_mpfr(Function::Log, ::fixy::side_as<side::Input>(input));
        double const got = ::fixy::fp::log(input);
        double const slow = ::fixy::fp::detail::log_accurate_(input);
        if (!agree(got, expected)) {
            if (log_wrong < kShownMismatches) {
                ::fixy::report(::fixy::Sink::Out, "  log of {} gives {}, and the round of MPFR is {}\n", input, got,
                               expected);
            }
            ++log_wrong;
        }
        if (!agree(slow, expected)) {
            if (slow_wrong < kShownMismatches) {
                ::fixy::report(::fixy::Sink::Out, "  the slow path of {} gives {}, and the round of MPFR is {}\n",
                               input, slow, expected);
            }
            ++slow_wrong;
        }
        double const distance = midpoint_distance(log_slow_triple(input), slow);
        if (distance < closest) {
            closest = distance;
            closest_input = input;
        }
    }
    ::fixy::report(::fixy::Sink::Out,
                   "bench_fp_sweep: log of {} subnormal inputs: {} not correctly rounded through log, {} through the "
                   "slow path, the closest midpoint 2^{} units in the last place at {}\n",
                   kSubnormalCount, log_wrong, slow_wrong,
                   ::fixy::Fixed<2>{::fixy::fp::log(closest) / 0x1.62e42fefa39efp-1}, closest_input);
    return log_wrong == 0 && slow_wrong == 0 ? 0 : 1;
}

[[nodiscard]] int run_sweep(::fixy::os::Args const& arguments) noexcept {
    ::fixy::Option<::fixy::CText> const kind = arguments.get(1);
    ::fixy::Option<::fixy::CText> const name = arguments.get(2);
    if (arguments.size() != 3 || kind.is_none() || name.is_none()) {
        ::fixy::report(::fixy::Sink::Err, "usage: bench_fp_sweep float|double exp|log|erf, bench_fp_sweep fma "
                                          "float|double, bench_fp_sweep hard|fast-bound exp|log, or bench_fp_sweep "
                                          "subnormal log\n");
        return 2;
    }
    ::fixy::TextView const kind_text = ::fixy::Option<::fixy::CText>{kind}.expect("the count is three").view();
    ::fixy::TextView const name_text = ::fixy::Option<::fixy::CText>{name}.expect("the count is three").view();
    if (kind_text == "hard" || kind_text == "fast-bound") {
        bool const is_exp = name_text == "exp";
        if (!is_exp && name_text != "log") {
            ::fixy::report(::fixy::Sink::Err, "usage: bench_fp_sweep hard exp|log, or bench_fp_sweep fast-bound "
                                              "exp|log\n");
            return 2;
        }
        if (kind_text == "hard") return sweep_hard(is_exp ? Function::Exp : Function::Log);
        return is_exp ? sweep_fast_bound_exp() : sweep_fast_bound_log();
    }
    if (kind_text == "subnormal") {
        if (name_text != "log") {
            ::fixy::report(::fixy::Sink::Err, "usage: bench_fp_sweep subnormal log\n");
            return 2;
        }
        return sweep_subnormal_log();
    }
    if (kind_text == "fma") {
        ::fixy::TextView const type_text = ::fixy::Option<::fixy::CText>{name}.expect("the count is three").view();
        if (type_text == "float") return sweep_fma_float(kFmaRounds);
        if (type_text == "double") return sweep_fma_double(kFmaRounds);
        ::fixy::report(::fixy::Sink::Err, "usage: bench_fp_sweep fma float|double\n");
        return 2;
    }
    ::fixy::Option<Function> const function =
        function_of(::fixy::Option<::fixy::CText>{name}.expect("the count is three").view());
    if (function.is_none() || (kind_text != "float" && kind_text != "double")) {
        ::fixy::report(::fixy::Sink::Err, "usage: bench_fp_sweep float|double exp|log|erf\n");
        return 2;
    }
    Function const chosen = ::fixy::Option<Function>{function}.expect("the name is a function");
    return kind_text == "float" ? sweep_float(chosen) : sweep_double(chosen);
}

}  // namespace

}  // namespace fp_sweep

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) { return fp_sweep::run_sweep(::fixy::os::args(argc, argv)); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
