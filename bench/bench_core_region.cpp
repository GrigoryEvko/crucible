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
    bench::print("{}  p50={} ns  p99={} ns  p99.9={} ns  max={} ns  cv={} permille\n", name,
                 nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99), nanoseconds(measured.pct.p99_9),
                 nanoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

// The runs of the rows.  Each row holds its own copy in its home
// (bench::RowHome), so the distance from the runs to the frame of the timed
// loop depends only on the code of the row.  A fixed pattern of values, and
// a second run with the same values.  The positions hold numbers below 16,
// so each one is an index of the table of 16 elements.  The two views read
// the runs of this object, so the object does not copy.
struct Runs {
    ::foundation::core::FixedArray<std::uint32_t, kElements> values{};
    ::foundation::core::FixedArray<std::uint32_t, kElements> same_values{};
    ::foundation::core::FixedArray<std::uint32_t, kElements> positions{};
    ::foundation::core::FixedArray<std::uint32_t, 16> table{};
    fixy::View<std::uint32_t const> run;
    fixy::View<std::uint32_t const> same_run;

    Runs() noexcept : run{values.view()}, same_run{same_values.view()} {
        std::uint32_t pattern = 0x9E3779B9u;
        for (fixy::Index<kElements> index : values.indices()) {
            pattern = pattern * 1664525u + 1013904223u;
            values.set(index, pattern);
            same_values.set(index, pattern);
            positions.set(index, pattern >> 28);
        }
        for (fixy::Index<16> index : table.indices()) {
            table.set(index, static_cast<std::uint32_t>(index.raw()) * 7u);
        }
    }
    Runs(Runs const&) = delete("the two views read the runs of this object");
    Runs& operator=(Runs const&) = delete("the two views read the runs of this object");
};

// The report of a row whose step runs on the runs in a home of its own.
template <typename Step>
[[nodiscard]] bench::Report run_on_runs(fixy::TextView name, Step step) {
    return bench::run_in_home(name, []() noexcept { return Runs{}; }, step);
}

}  // namespace

int main() {
    bench::elevate_priority();

    print_row("View loop, 4096 elements", run_on_runs("View loop", [](Runs& row) noexcept {
                  std::uint32_t sum = 0;
                  for (std::uint32_t element : row.run) {
                      sum += element;
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("View reverse(), 4096 elements", run_on_runs("View reverse()", [](Runs& row) noexcept {
                  std::uint32_t sum = 0;
                  for (std::uint32_t element : row.run.reverse()) {
                      sum += element;
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("View windows<2>(), 4095 runs", run_on_runs("View windows<2>()", [](Runs& row) noexcept {
                  std::uint32_t sum = 0;
                  for (fixy::View<std::uint32_t const, 2> pair : row.run.windows<2>()) {
                      sum += pair.at(fixy::index<0>()) ^ pair.at(fixy::index<1>());
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("View chunks<4>(), 1024 runs", run_on_runs("View chunks<4>()", [](Runs& row) noexcept {
                  std::uint32_t sum = 0;
                  for (fixy::View<std::uint32_t const, 4> chunk : row.run.chunks<4>()) {
                      sum += chunk.at(fixy::index<0>()) ^ chunk.at(fixy::index<3>());
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("View enumerate(), 4096 elements", run_on_runs("View enumerate()", [](Runs& row) noexcept {
                  std::uint32_t sum = 0;
                  for (auto const [position, element] : row.values.enumerate()) {
                      sum += *element ^ static_cast<std::uint32_t>(position.raw());
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("zip of two walks of one fixed extent, 4096 pairs",
              run_on_runs("zip of two fixed walks", [](Runs& row) noexcept {
                  std::uint32_t sum = 0;
                  for (auto const [first, second] : row.values.view().walk().zip(row.same_values.view().walk())) {
                      sum += *first * *second;
                  }
                  bench::do_not_optimize(sum);
              }));

    print_row("try_zip of two walks of a dynamic extent, 4096 pairs",
              run_on_runs("try_zip of two walks", [](Runs& row) noexcept {
                  std::uint32_t const sum = row.run.walk()
                                                .try_zip(row.same_run.walk())
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

    print_row("find with no match, 4096 elements", run_on_runs("find with no match", [](Runs& row) noexcept {
                  bool const is_found = fixy::find(row.run, 3u).is_some();
                  bench::do_not_optimize(is_found);
              }));

    // The two runs go through the input sink, so each call compares them
    // again.  With no sink, the optimizer compares them one time before the
    // timed loop (the sink of bench_harness.h).
    print_row("equal of two Views, 4096 elements", run_on_runs("equal of two Views", [](Runs& row) noexcept {
                  bool const is_equal = fixy::equal(bench::make_opaque(row.run), bench::make_opaque(row.same_run));
                  bench::do_not_optimize(is_equal);
              }));

    print_row("Index check and at, 4096 reads", run_on_runs("Index check and at", [](Runs& row) noexcept {
                  std::uint32_t sum = 0;
                  for (std::uint32_t position : row.positions) {
                      sum += row.table.at(fixy::Index<16>::check(position).value_or(fixy::index<0>()));
                  }
                  bench::do_not_optimize(sum);
              }));

    return 0;
}
