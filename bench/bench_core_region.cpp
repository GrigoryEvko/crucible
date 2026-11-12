// Microbench of the scans, the loops and the reads of the Region family on
// one thread.
//
// Each row is one pass over a run of 4096 elements of 32 bits, which the
// cache of the core holds after the first pass.  The rule codegen-shapes
// compares the machine code of the loops with the raw pointer loops that
// they replace (test/shape/core_shapes.cpp), so this bench gives the time
// and not a comparison.
//
// Each line gives the percentiles of one pass in nanoseconds, and the
// coefficient of variation in thousandths.  The Report family writes
// integers only, so the bench writes no fraction of a nanosecond.

#include <fixy/Core.h>

#include <cstdint>

#include "bench_harness.h"

namespace {

// The count of the elements of each run.
constexpr std::size_t kElements = 4096;

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// Writes one line: the name of the pass and the percentiles of its samples.
void print_row(fixy::TextView name, bench::Report const& measured) noexcept {
    fixy::report(fixy::Sink::Out, "{}  p50={} ns  p99={} ns  p99.9={} ns  max={} ns  cv={} permille\n", name,
                 nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99), nanoseconds(measured.pct.p99_9),
                 nanoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

}  // namespace

int main() {
    bench::elevate_priority();

    // A fixed pattern of values, and a second run with the same values.
    // The positions hold numbers below 16, so each one is an index of the
    // table of 16 elements.
    ::foundation::core::FixedArray<std::uint32_t, kElements> values{};
    ::foundation::core::FixedArray<std::uint32_t, kElements> same_values{};
    ::foundation::core::FixedArray<std::uint32_t, kElements> positions{};
    ::foundation::core::FixedArray<std::uint32_t, 16> table{};
    std::uint32_t pattern = 0x9E3779B9u;
    for (fixy::Index<kElements> index : values.indices()) {
        pattern = pattern * 1664525u + 1013904223u;
        values.at(index) = pattern;
        same_values.at(index) = pattern;
        positions.at(index) = pattern >> 28;
    }
    for (fixy::Index<16> index : table.indices()) {
        table.at(index) = static_cast<std::uint32_t>(index.raw()) * 7u;
    }

    fixy::View<std::uint32_t const> const run = values.view();
    fixy::View<std::uint32_t const> const same_run = same_values.view();

    print_row("View loop, 4096 elements", bench::run("View loop", [&] {
                  std::uint32_t sum = 0;
                  for (std::uint32_t element : run) {
                      sum += element;
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("View reverse(), 4096 elements", bench::run("View reverse()", [&] {
                  std::uint32_t sum = 0;
                  for (std::uint32_t element : run.reverse()) {
                      sum += element;
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("View windows<2>(), 4095 runs", bench::run("View windows<2>()", [&] {
                  std::uint32_t sum = 0;
                  for (fixy::View<std::uint32_t const, 2> pair : run.windows<2>()) {
                      sum += pair.at(fixy::index<0>()) ^ pair.at(fixy::index<1>());
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("View chunks<4>(), 1024 runs", bench::run("View chunks<4>()", [&] {
                  std::uint32_t sum = 0;
                  for (fixy::View<std::uint32_t const, 4> chunk : run.chunks<4>()) {
                      sum += chunk.at(fixy::index<0>()) ^ chunk.at(fixy::index<3>());
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("View enumerate(), 4096 elements", bench::run("View enumerate()", [&] {
                  std::uint32_t sum = 0;
                  for (auto const [position, element] : values.enumerate()) {
                      sum += *element ^ static_cast<std::uint32_t>(position.raw());
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("zip of two fixed Views, 4096 pairs", bench::run("zip of two fixed Views", [&] {
                  std::uint32_t sum = 0;
                  for (auto const [first, second] : fixy::zip(values.view(), same_values.view())) {
                      sum += *first * *second;
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("zip of two dynamic Views, 4096 pairs", bench::run("zip of two dynamic Views", [&] {
                  std::uint32_t const sum = fixy::zip(run, same_run)
                                                .match(
                                                    [](auto pairs) noexcept {
                                                        std::uint32_t total = 0;
                                                        for (auto const [first, second] : pairs) {
                                                            total += *first * *second;
                                                        }
                                                        return total;
                                                    },
                                                    [](fixy::LengthMismatch) noexcept { return std::uint32_t{0}; });
                  bench::do_not_optimize(sum);
              }));

    print_row("find with no match, 4096 elements", bench::run("find with no match", [&] {
                  bool const is_found = fixy::find(run, 3u).is_some();
                  bench::do_not_optimize(is_found);
              }));

    print_row("equal of two Views, 4096 elements", bench::run("equal of two Views", [&] {
                  bool const is_equal = fixy::equal(run, same_run);
                  bench::do_not_optimize(is_equal);
              }));

    print_row("Index check and at, 4096 reads", bench::run("Index check and at", [&] {
                  std::uint32_t sum = 0;
                  for (std::uint32_t position : positions) {
                      sum += table.at(fixy::Index<16>::check(position).value_or(fixy::index<0>()));
                  }
                  bench::do_not_optimize(sum);
              }));

    return 0;
}
