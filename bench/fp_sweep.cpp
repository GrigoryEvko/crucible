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
//                                    of CORE-MATH
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
#include <fixy/fp/Erf.h>
#include <fixy/fp/Exp.h>
#include <fixy/fp/Log.h>
#include <fixy/os/Args.h>
#include <foundation/Quarantine.h>

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
            return 0.52;
        case Function::Log:
            return 0.53;
        case Function::Erf:
            return 0.8;
        default:
            ::fixy::unreachable();
    }
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
    bool const holds = largest <= bound_of(function);
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

[[nodiscard]] int run_sweep(::fixy::os::Args const& arguments) noexcept {
    ::fixy::Option<::fixy::CText> const kind = arguments.get(1);
    ::fixy::Option<::fixy::CText> const name = arguments.get(2);
    if (arguments.size() != 3 || kind.is_none() || name.is_none()) {
        ::fixy::report(::fixy::Sink::Err, "usage: bench_fp_sweep float|double exp|log|erf, or bench_fp_sweep fma "
                                          "float|double\n");
        return 2;
    }
    ::fixy::TextView const kind_text = ::fixy::Option<::fixy::CText>{kind}.expect("the count is three").view();
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
