// The bench of exp, log and erf of fixy/fp against the references of the
// state of the art, on one thread: glibc libm of the host, the correctly
// rounded CORE-MATH, and SLEEF in its scalar class of 1.0 ulp.
//
// Each row reads kInputs inputs of the domain of its function and gives the
// cycles of the core for one input at the p50 and the p99, the coefficient of
// variation, the floor of the core model, and the ratios to the floor and to
// the fastest reference of the same form.
//
//   throughput  the calls do not depend on each other (fp_elementary_rows.h)
//   latency     each call takes the result of the call before it, through
//               x + 0 * y, so the row gives the latency of one call and of
//               that multiply and add
//
// The functions of fixy/fp inline into the loop of the row.  A reference is
// one call of a library for each input, so its rows also hold a call and a
// return.  The floor rows run the same loops with the identity in place of the
// function: the cost of the loop, the load and the xor, or of the chain.
//
// A clock row before the two rows of each implementation gives the cycles of
// the core in one ns, from a chain of dependent adds of one cycle each.  The
// TSC counts at 2.2466 GHz on luks-a, and the core can boost above that.
//
// Before the rows, the bench checks that the float results and the exp(double)
// results of fixy/fp have the bits of CORE-MATH on each input of the rows: the
// two are correctly rounded, so they agree.

#include <fixy/Core.h>
#include <fixy/fp/Erf.h>
#include <fixy/fp/Exp.h>
#include <fixy/fp/Log.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

#include "bench_harness.h"
#include "fp_elementary_rows.h"

namespace {

namespace rows = ::bench_fp_elementary;
using rows::Function;
using ::foundation::core::HeapArray;
using ::foundation::core::View;

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The inputs of one row, and the wall time of one row in milliseconds.
constexpr std::size_t kInputs = 4096;
constexpr std::size_t kRowWallMs = 1000;
constexpr std::size_t kClockSamples = 2048;

// The dependent adds of one call of the clock body, and the calls of one sample.
constexpr std::uint32_t kClockAdds = 64;
constexpr std::size_t kClockBatch = 256;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a chain of dependent adds gives the clock of the core")
// kClockAdds dependent adds to the value in one block of code, which the
// compiler cannot fold.
[[gnu::always_inline]] inline void add_chain(std::uint64_t& value) noexcept {
#if defined(__x86_64__)
    asm volatile(".rept %c1\n\tadd $1, %0\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#else
    asm volatile(".rept %c1\n\tadd %0, %0, #1\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#endif
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// kClockAdds dependent adds: one cycle each on the core.
struct ClockChain {
    std::uint64_t value = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        std::uint64_t local = value;
        add_chain(local);
        value = local;
    }
};

// The floor of each row in cycles of the core: the floor of the faster of two
// codes of the function, fixy/fp and CORE-MATH.  Each code floor counts the
// hot path in the assembly of the function out of line, on the path of the
// inputs of the rows: the table path of log, and the interval path of erf.
// fixy/fp compiles with the flags of this bench, -O3 -march=native, and
// CORE-MATH with the flags of core_math_reference, -O2 -march=native.  The
// core is the Zen 4c core of luks-a (EPYC 9754), with the model of the AMD
// guide 57647:
//   - Two FP pipes take a multiply or an fma, and two take an add, a
//     conversion or a round.
//   - The core dispatches six macro operations each cycle.  A compare and its
//     branch are one macro operation.
// The throughput floor of a code is the largest of three bounds: the fma and
// the multiplies on two pipes, the adds and the conversions on two pipes,
// and the macro operations at six for each cycle.
//   code              fma  mul  add  cvt  ops   floor
//   fixy exp double    12    2    8    0   46   7.67, the dispatch
//   fixy exp float      5    1    1    2   29   4.83, the dispatch
//   fixy log double     6    3   10    1   48   8.0, the dispatch
//   fixy log float      6    1    1    3   35   5.83, the dispatch
//   fixy erf double    11    2    4    2   33   6.5, the fma pipes
//   fixy erf float      8    4    1    4   32   6.0, the fma pipes
//   cr_expf             6    2    1    3   30   5.0, the dispatch
//   cr_logf             5    1    1    3   34   5.67, the dispatch
//   cr_erff             8    4    1    5   32   6.0, the fma pipes
// The loop of a row adds a load, an xor and the loop control to each input,
// so the row can be up to 0.7 cycles above the function alone.
//
// The latency floor of a code is the longest chain of dependent operations
// from the input to the result, with these latencies: 4 for an fma, 3 for a
// multiply, an add, a conversion or a round, 1 for an integer operation or a
// move between a general register and an xmm register, 0 for a move that the
// rename ends, and 4 for a load from L1.  Each latency is a lower bound of
// the guide, so the floor is a lower bound too.  The chain x + 0 * y of the
// row adds a multiply and an add, 6 cycles:
//   fixy exp double 43 + 6 = 49   fixy log double 38 + 6 = 44   fixy erf double 72 + 6 = 78
//   fixy exp float  29 + 6 = 35   fixy log float  27 + 6 = 33   fixy erf float  30 + 6 = 36
//   cr_expf         28 + 6 = 34   cr_logf         23 + 6 = 29   cr_erff         30 + 6 = 36
// The Horner chain of fixy erf double holds 11 dependent fma.  fixy erf float
// and cr_erff take an Estrin form of degree 7.  After the table, fixy log
// float takes a polynomial of degree 5 in r, and cr_logf one of degree 3.  The
// rounding test of cr_logf admits an absolute error of about 2^-32, so more
// inputs take its second path.  The floors do not count glibc, SLEEF and the double
// code of CORE-MATH.  So a reference row below a floor shows a code that the
// floor does not count.
struct Floor {
    double throughput = 0.0;
    double latency = 0.0;
};

[[nodiscard]] constexpr Floor floor_of(Function function, bool is_double) noexcept {
    switch (function) {
        case Function::Exp:
            return is_double ? Floor{46.0 / 6.0, 49.0} : Floor{29.0 / 6.0, 34.0};
        case Function::Log:
            return is_double ? Floor{48.0 / 6.0, 44.0} : Floor{34.0 / 6.0, 29.0};
        case Function::Erf:
            return is_double ? Floor{13.0 / 2.0, 78.0} : Floor{6.0, 36.0};
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

// The inputs of a function: exp in [-50, 50], log in [2^-20, 2^20] with an even
// spread of the exponent, erf in [-4, 4].  A linear congruential stream gives
// the fraction of each input, so each run reads the same inputs.
[[nodiscard]] double input_of(Function function, std::uint64_t word) noexcept {
    double const unit = static_cast<double>(word >> 11) * 0x1p-53;
    switch (function) {
        case Function::Exp:
            return -50.0 + 100.0 * unit;
        case Function::Log: {
            std::uint64_t const exponent = 1023u - 20u + (word >> 58) % 41u;
            return ::fixy::bit_cast<double>((exponent << 52) | (word & 0x000FFFFFFFFFFFFFULL));
        }
        case Function::Erf:
            return -4.0 + 8.0 * unit;
        default:
            ::fixy::unreachable();
    }
}

[[nodiscard]] HeapArray<double> draw_doubles(Function function) noexcept {
    std::uint64_t state = 0x9E3779B97F4A7C15u;
    return ::foundation::core::mint_heap_array_with<double>(kAlloc, kInputs, [&state, function](std::size_t) noexcept {
        state = state * 6364136223846793005u + 1442695040888963407u;
        return input_of(function, state);
    });
}

[[nodiscard]] HeapArray<float> draw_floats(Function function) noexcept {
    std::uint64_t state = 0xD1B54A32D192ED03u;
    return ::foundation::core::mint_heap_array_with<float>(kAlloc, kInputs, [&state, function](std::size_t) noexcept {
        state = state * 6364136223846793005u + 1442695040888963407u;
        return static_cast<float>(input_of(function, state));
    });
}

// The kernels of fixy/fp and the identity of the floor rows.
template <Function Of>
struct FixyKernel {
    template <class Value>
    [[nodiscard, gnu::always_inline]] Value operator()(Value value) const noexcept {
        if constexpr (Of == Function::Exp) {
            return ::fixy::fp::exp(value);
        } else if constexpr (Of == Function::Log) {
            return ::fixy::fp::log(value);
        } else {
            return ::fixy::fp::erf(value);
        }
    }
};

struct Identity {
    template <class Value>
    [[nodiscard, gnu::always_inline]] Value operator()(Value value) const noexcept {
        return value;
    }
};

// The value 0 at run time, so the compiler cannot drop the multiply of the chain.
template <class Value>
[[nodiscard]] Value opaque_zero() noexcept {
    volatile Value zero = 0;
    return zero;
}

// One measured row, with the pin of CRUCIBLE_BENCH_CORE and the wall cap.
template <class Body>
[[nodiscard]] bench::Report measure_row(bench::Run&& run, Body&& body) {
    if (int const core = bench::env_core(); core >= 0) static_cast<void>(run.core(core));
    static_cast<void>(run.max_wall_ms(kRowWallMs));
    return run.measure(static_cast<Body&&>(body));
}

// The cycles of the core in one ns, on the core of the rows.
[[nodiscard]] double measure_cycles_per_ns() {
    ClockChain chain{};
    bench::Run run{"clock"};
    static_cast<void>(run.batch(kClockBatch).samples(kClockSamples));
    bench::Report const report = measure_row(static_cast<bench::Run&&>(run), chain);
    bench::do_not_optimize(chain.value);
    return report.pct.p50 > 0.0 ? kClockAdds / report.pct.p50 : 0.0;
}

// The cycles of one input at the p50 and the p99 of a row, and the clock of
// the core in GHz that gives them.
struct Cycles {
    double p50 = 0.0;
    double p99 = 0.0;
    double cv = 0.0;
    double clock = 0.0;
};

[[nodiscard]] Cycles cycles_of(bench::Report const& report, double cycles_per_ns) noexcept {
    double const scale = cycles_per_ns / static_cast<double>(kInputs);
    return Cycles{report.pct.p50 * scale, report.pct.p99 * scale, report.pct.cv, cycles_per_ns};
}

// Writes one row: the name, the cycles at the p50 and the p99 of one input,
// the coefficient of variation, the clock, the floor and the ratio to it, and
// the ratio to the fastest reference of the form (0 when no reference ran).
void print_row(::fixy::TextView name, Cycles cycles, double floor, double fastest_reference) noexcept {
    double const to_floor = floor > 0.0 ? cycles.p50 / floor : 0.0;
    double const to_reference = fastest_reference > 0.0 ? cycles.p50 / fastest_reference : 0.0;
    bench::print("  {}  p50={} cycles  p99={} cycles  cv={} permille  clock={} GHz  floor={}  to floor={}  to "
                 "fastest reference={}\n",
                 name, ::fixy::Fixed<2>{cycles.p50}, ::fixy::Fixed<2>{cycles.p99},
                 static_cast<std::uint64_t>(cycles.cv * 1000.0 + 0.5), ::fixy::Fixed<3>{cycles.clock},
                 ::fixy::Fixed<2>{floor}, ::fixy::Fixed<2>{to_floor}, ::fixy::Fixed<2>{to_reference});
}

// The smaller of two cycle counts, where 0 is no count.  A build with no
// reference other than glibc does not call it.
[[maybe_unused, nodiscard]] double faster_of(double known, double candidate) noexcept {
    if (!(known > 0.0)) return candidate;
    return candidate < known ? candidate : known;
}

// The two forms of one implementation of one function.
struct Pair {
    Cycles throughput{};
    Cycles latency{};
};

// The two rows of one implementation, in cycles of the clock that a clock row
// gives immediately before them.  The clock of the core can change during the
// bench, so each pair measures its own.
template <class Value, class XorBody, class ChainBody>
[[nodiscard]] Pair measure_pair(XorBody const& xor_body, ChainBody const& chain_body) {
    double const cycles_per_ns = measure_cycles_per_ns();
    bench::Report const throughput =
        measure_row(bench::Run{"throughput"}, [&xor_body] { bench::do_not_optimize(xor_body()); });
    bench::Report const latency =
        measure_row(bench::Run{"latency"}, [&chain_body] { bench::do_not_optimize(chain_body()); });
    return Pair{cycles_of(throughput, cycles_per_ns), cycles_of(latency, cycles_per_ns)};
}

// The rows of one function for one type: fixy/fp, then each reference that
// the build holds.  The ratio of a row to the fastest reference compares its
// own form.
template <class Value, Function Of>
void run_function(View<Value const> inputs, Value zero) {
    constexpr bool is_double = sizeof(Value) == sizeof(double);
    ::fixy::TextView const type = is_double ? "double" : "float ";
    Floor const floor = floor_of(Of, is_double);
    Pair const fixy =
        measure_pair<Value>([inputs] { return rows::xor_of_results(FixyKernel<Of>{}, inputs); },
                            [inputs, zero] { return rows::chain_of_results(FixyKernel<Of>{}, inputs, zero); });
    Pair const glibc = measure_pair<Value>([inputs] { return rows::glibc::xor_of(Of, inputs); },
                                           [inputs, zero] { return rows::glibc::chain_of(Of, inputs, zero); });
    double fastest_throughput = glibc.throughput.p50;
    double fastest_latency = glibc.latency.p50;
#if defined(CRUCIBLE_BENCH_CORE_MATH)
    Pair const core_math = measure_pair<Value>([inputs] { return rows::core_math::xor_of(Of, inputs); },
                                               [inputs, zero] { return rows::core_math::chain_of(Of, inputs, zero); });
    fastest_throughput = faster_of(fastest_throughput, core_math.throughput.p50);
    fastest_latency = faster_of(fastest_latency, core_math.latency.p50);
#endif
#if defined(CRUCIBLE_BENCH_SLEEF)
    Pair const sleef = measure_pair<Value>([inputs] { return rows::sleef::xor_of(Of, inputs); },
                                           [inputs, zero] { return rows::sleef::chain_of(Of, inputs, zero); });
    fastest_throughput = faster_of(fastest_throughput, sleef.throughput.p50);
    fastest_latency = faster_of(fastest_latency, sleef.latency.p50);
#endif
    bench::print("{} {}, {} inputs\n", name_of(Of), type, kInputs);
    print_row("fixy/fp    throughput", fixy.throughput, floor.throughput, fastest_throughput);
    print_row("glibc      throughput", glibc.throughput, floor.throughput, fastest_throughput);
#if defined(CRUCIBLE_BENCH_CORE_MATH)
    print_row("CORE-MATH  throughput", core_math.throughput, floor.throughput, fastest_throughput);
#endif
#if defined(CRUCIBLE_BENCH_SLEEF)
    print_row("SLEEF u10  throughput", sleef.throughput, floor.throughput, fastest_throughput);
#endif
    print_row("fixy/fp    latency   ", fixy.latency, floor.latency, fastest_latency);
    print_row("glibc      latency   ", glibc.latency, floor.latency, fastest_latency);
#if defined(CRUCIBLE_BENCH_CORE_MATH)
    print_row("CORE-MATH  latency   ", core_math.latency, floor.latency, fastest_latency);
#endif
#if defined(CRUCIBLE_BENCH_SLEEF)
    print_row("SLEEF u10  latency   ", sleef.latency, floor.latency, fastest_latency);
#endif
}

// The floor rows of one type: the loops of the two forms with the identity.
template <class Value>
void run_floor(View<Value const> inputs, Value zero) {
    Pair const floor = measure_pair<Value>([inputs] { return rows::xor_of_results(Identity{}, inputs); },
                                           [inputs, zero] { return rows::chain_of_results(Identity{}, inputs, zero); });
    ::fixy::TextView const type = sizeof(Value) == sizeof(double) ? "double" : "float ";
    bench::print("the loop with the identity, {}\n", type);
    print_row("identity   throughput", floor.throughput, 0.0, 0.0);
    print_row("identity   latency   ", floor.latency, 0.0, 0.0);
}

#if defined(CRUCIBLE_BENCH_CORE_MATH)
// The correctly rounded results of fixy/fp, each float function and exp and
// log of double, have the bits of CORE-MATH on each input.
void check_the_correct_rounds(View<double const> exp_doubles, View<double const> log_doubles,
                              View<float const> exp_inputs, View<float const> log_inputs,
                              View<float const> erf_inputs) noexcept {
    bool const is_same = rows::xor_of_results(FixyKernel<Function::Exp>{}, exp_doubles)
                          == rows::core_math::xor_of(Function::Exp, exp_doubles)
                      && rows::xor_of_results(FixyKernel<Function::Log>{}, log_doubles)
                             == rows::core_math::xor_of(Function::Log, log_doubles)
                      && rows::xor_of_results(FixyKernel<Function::Exp>{}, exp_inputs)
                             == rows::core_math::xor_of(Function::Exp, exp_inputs)
                      && rows::xor_of_results(FixyKernel<Function::Log>{}, log_inputs)
                             == rows::core_math::xor_of(Function::Log, log_inputs)
                      && rows::xor_of_results(FixyKernel<Function::Erf>{}, erf_inputs)
                             == rows::core_math::xor_of(Function::Erf, erf_inputs);
    if (!is_same) ::fixy::fatal("bench_fp_elementary: a correctly rounded result of fixy/fp differs from CORE-MATH");
    bench::print("the correctly rounded results of fixy/fp have the bits of CORE-MATH on the inputs of the rows\n");
}
#endif

}  // namespace

int main() {
    HeapArray<double> const exp_doubles = draw_doubles(Function::Exp);
    HeapArray<double> const log_doubles = draw_doubles(Function::Log);
    HeapArray<double> const erf_doubles = draw_doubles(Function::Erf);
    HeapArray<float> const exp_floats = draw_floats(Function::Exp);
    HeapArray<float> const log_floats = draw_floats(Function::Log);
    HeapArray<float> const erf_floats = draw_floats(Function::Erf);
#if defined(CRUCIBLE_BENCH_CORE_MATH)
    check_the_correct_rounds(exp_doubles.view(), log_doubles.view(), exp_floats.view(), log_floats.view(),
                             erf_floats.view());
#endif
    bench::print("=== {} cycles of the core in one ns at the start (the TSC counts 2.2466 GHz)\n",
                 ::fixy::Fixed<3>{measure_cycles_per_ns()});
    double const zero = opaque_zero<double>();
    float const zero_float = opaque_zero<float>();
    run_floor<double>(exp_doubles.view(), zero);
    run_floor<float>(exp_floats.view(), zero_float);
    run_function<double, Function::Exp>(exp_doubles.view(), zero);
    run_function<double, Function::Log>(log_doubles.view(), zero);
    run_function<double, Function::Erf>(erf_doubles.view(), zero);
    run_function<float, Function::Exp>(exp_floats.view(), zero_float);
    run_function<float, Function::Log>(log_floats.view(), zero_float);
    run_function<float, Function::Erf>(erf_floats.view(), zero_float);
    return 0;
}
