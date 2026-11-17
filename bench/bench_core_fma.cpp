// Microbench of fp::fma of foundation/core/Float.h at three levels on one
// thread: the exact form in integers at x86-64 and x86-64-v2, an out-of-line
// call for each fma, and the instruction of the host.
//
// The units fma_bench_v1.cpp and fma_bench_v2.cpp compile for x86-64 and
// x86-64-v2, and this unit for the host (fma_bench_units.h).  Each row reads
// 4096 triples of doubles: a left and a right factor in [1, 2), and an addend
// in (-2, 2).  The chain feeds each result into the addend of the next fma,
// so the latency of one fma bounds it.  The sum adds the results of fmas
// that do not depend on each other, so the throughput bounds it.  Before the
// rows, the bench checks that the three units give the same bits for each
// row, because the bits of a recipe do not depend on the level.
//
// Each line gives the core of its row (the core that the harness reads after
// its pin, or cpu-1 for a row with no pin), the percentiles of one call in
// nanoseconds, the coefficient of variation in thousandths, and the rate in
// fmas for each microsecond at the median.  The Report family writes integers
// only.

#include <fixy/Core.h>
#include <foundation/core/Float.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

#include "bench_harness.h"
#include "fma_bench_body.h"
#include "fma_bench_units.h"

namespace {

using ::foundation::core::HeapArray;
using ::foundation::core::View;

namespace units = ::crucible::bench_fma;

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The count of triples of each row, and of values.
constexpr std::size_t kTriples = 4096;
constexpr std::size_t kValues = 3 * kTriples;

// The wall time of one row, in milliseconds.
constexpr std::size_t kRowWallMs = 2000;

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// Writes one line: the name of the row, the core of the row, the percentiles
// of one call, and the rate of fmas at the median.
void print_row(fixy::TextView name, bench::Report const& measured) noexcept {
    double const median = measured.pct.p50 > 0.0 ? measured.pct.p50 : 1.0;
    fixy::report(fixy::Sink::Out, "{}  core=cpu{}  p50={} ns  p99={} ns  max={} ns  cv={} permille  rate={} fma/us\n",
                 name, measured.pinned_cpu.raw(), nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99),
                 nanoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5),
                 static_cast<std::uint64_t>(static_cast<double>(kTriples) * 1000.0 / median + 0.5));
}

// Measures one row with the pin of CRUCIBLE_BENCH_CORE and the wall cap of
// kRowWallMs.
template <typename Body>
[[nodiscard]] bench::Report measure_row(bench::Run&& run, Body&& body) {
    if (int const core = bench::env_core(); core >= 0) static_cast<void>(run.core(core));
    static_cast<void>(run.max_wall_ms(kRowWallMs));
    return run.measure(static_cast<Body&&>(body));
}

// The triples, from a linear congruential stream: a factor gets the exponent
// of 1 and a random fraction, and an addend gets a random sign too.
[[nodiscard]] HeapArray<double> draw_triples() noexcept {
    HeapArray<double> values = ::foundation::core::mint_heap_array(kAlloc, kValues, 0.0);
    std::uint64_t state = 0x9E3779B97F4A7C15u;
    std::size_t index = 0;
    for (double& value : values) {
        state = state * 6364136223846793005u + 1442695040888963407u;
        std::uint64_t const fraction = state >> 12;
        std::uint64_t const sign = index % 3 == 2 ? (state & 1u) << 63 : 0u;
        value = ::fixy::bit_cast<double>(sign | (std::uint64_t{1023} << 52) | fraction);
        ++index;
    }
    return values;
}

// The bits of a double, for the check of the units.
[[nodiscard]] std::uint64_t bits_of(double value) noexcept { return ::fixy::bit_cast<std::uint64_t>(value); }

// The three units give the same bits for each row.
void check_the_units_agree(View<double const> triples) noexcept {
    std::uint64_t const chain = bits_of(units::chain_of_fma_at_host(triples));
    std::uint64_t const sum = bits_of(units::sum_of_fma_at_host(triples));
    bool const is_same =
        bits_of(units::chain_of_fma_at_v1(triples)) == chain && bits_of(units::chain_of_fma_at_v2(triples)) == chain
        && bits_of(units::sum_of_fma_at_v1(triples)) == sum && bits_of(units::sum_of_fma_at_v2(triples)) == sum;
    if (!is_same) fixy::fatal("bench_core_fma: the units of x86-64, x86-64-v2 and the host give different bits");
    fixy::report(fixy::Sink::Out, "the three units give the same bits: chain {} and sum {}\n", chain, sum);
}

}  // namespace

int main() {
    HeapArray<double> const values = draw_triples();
    View<double const> const triples = values.view();
    check_the_units_agree(triples);
    print_row("fp::fma double chain, x86-64 exact form, 4096",
              measure_row(bench::Run{"chain v1"}, [&] { bench::do_not_optimize(units::chain_of_fma_at_v1(triples)); }));
    print_row("fp::fma double chain, x86-64-v2 exact form, 4096",
              measure_row(bench::Run{"chain v2"}, [&] { bench::do_not_optimize(units::chain_of_fma_at_v2(triples)); }));
    print_row("fp::fma double chain, host instruction, 4096", measure_row(bench::Run{"chain host"}, [&] {
                  bench::do_not_optimize(units::chain_of_fma_at_host(triples));
              }));
    print_row("fp::fma double sum, x86-64 exact form, 4096",
              measure_row(bench::Run{"sum v1"}, [&] { bench::do_not_optimize(units::sum_of_fma_at_v1(triples)); }));
    print_row("fp::fma double sum, x86-64-v2 exact form, 4096",
              measure_row(bench::Run{"sum v2"}, [&] { bench::do_not_optimize(units::sum_of_fma_at_v2(triples)); }));
    print_row("fp::fma double sum, host instruction, 4096",
              measure_row(bench::Run{"sum host"}, [&] { bench::do_not_optimize(units::sum_of_fma_at_host(triples)); }));
    return 0;
}

namespace crucible::bench_fma {

double chain_of_fma_at_host(View<double const> triples) noexcept { return chain_of_fma_here(triples); }

double sum_of_fma_at_host(View<double const> triples) noexcept { return sum_of_fma_here(triples); }

}  // namespace crucible::bench_fma
