// Microbench of the operations of fixy::Atomic and fixy::Tally on one
// thread.
//
// Each operation of the Atomic family is one __atomic builtin, so its cost
// is the cost of the instruction that the builtin gives.  The bench gives
// the latency of each operation on a cell that one thread holds in its own
// cache, with the call of bench::do_not_optimize in each sample.  A test of
// the same operations under contention is in test/fixy/test_core_atomic.cpp.
//
// Each line gives the percentiles of one operation in picoseconds, and the
// coefficient of variation in thousandths.  The Report family writes
// integers only, so the bench writes no fraction of a nanosecond.

#include <fixy/Core.h>
#include <foundation/Linear.h>

#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

// The role of the writer of the cell of one writer.
struct BenchHeadRole {};

// A time in nanoseconds, as the nearest count of picoseconds.
[[nodiscard]] std::uint64_t picoseconds(double nanoseconds) noexcept {
    return static_cast<std::uint64_t>(nanoseconds * 1000.0 + 0.5);
}

// Writes one line: the name of the operation and the percentiles of its
// samples.
void print_row(fixy::TextView name, bench::Report const& measured) noexcept {
    fixy::report(fixy::Sink::Out, "{}  p50={} ps  p99={} ps  p99.9={} ps  max={} ps  cv={} permille\n", name,
                 picoseconds(measured.pct.p50), picoseconds(measured.pct.p99), picoseconds(measured.pct.p99_9),
                 picoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

}  // namespace

int main() {
    bench::elevate_priority();

    fixy::Atomic<std::uint64_t> cell{1};
    fixy::Atomic<std::uint64_t> counting_cell{1};
    fixy::Atomic<std::uint64_t> refusing_cell{1};
    fixy::Atomic<std::uint64_t> rising_cell{0};
    fixy::Tally tally{};

    // Only this thread touches the cells, so each compare-and-swap of
    // counting_cell stores, and each one of refusing_cell refuses.
    std::uint64_t operand = 3;
    std::uint64_t expected = 1;
    std::uint64_t rising = 1;

    print_row("Atomic::load_acquire", bench::run("Atomic::load_acquire", [&] {
                  std::uint64_t const value = cell.load_acquire();
                  bench::do_not_optimize(value);
              }));

    print_row("Atomic::store_release", bench::run("Atomic::store_release", [&] {
                  cell.store_release(operand);
                  bench::do_not_optimize(operand);
              }));

    print_row("Atomic::exchange_acq_rel", bench::run("Atomic::exchange_acq_rel", [&] {
                  std::uint64_t const before = cell.exchange_acq_rel(operand);
                  bench::do_not_optimize(before);
              }));

    print_row("Atomic::cas_acq_rel stores", bench::run("Atomic::cas_acq_rel stores", [&] {
                  bool const stored = counting_cell.cas_acq_rel(expected, expected + 1).is_ok();
                  expected += 1;
                  bench::do_not_optimize(stored);
              }));

    print_row("Atomic::cas_acq_rel refuses", bench::run("Atomic::cas_acq_rel refuses", [&] {
                  bool const stored = refusing_cell.cas_acq_rel(0, operand).is_ok();
                  bench::do_not_optimize(stored);
              }));

    print_row("Atomic::fetch_add_acq_rel", bench::run("Atomic::fetch_add_acq_rel", [&] {
                  std::uint64_t const before = cell.fetch_add_acq_rel(operand);
                  bench::do_not_optimize(before);
              }));

    print_row("Atomic::fetch_sub_acq_rel", bench::run("Atomic::fetch_sub_acq_rel", [&] {
                  std::uint64_t const before = cell.fetch_sub_acq_rel(operand);
                  bench::do_not_optimize(before);
              }));

    print_row("Atomic::fetch_or_acq_rel", bench::run("Atomic::fetch_or_acq_rel", [&] {
                  std::uint64_t const before = cell.fetch_or_acq_rel(operand);
                  bench::do_not_optimize(before);
              }));

    print_row("Atomic::fetch_and_acq_rel", bench::run("Atomic::fetch_and_acq_rel", [&] {
                  std::uint64_t const before = cell.fetch_and_acq_rel(operand);
                  bench::do_not_optimize(before);
              }));

    print_row("Atomic::fetch_max_acq_rel stores", bench::run("Atomic::fetch_max_acq_rel stores", [&] {
                  rising += 1;
                  std::uint64_t const before = rising_cell.fetch_max_acq_rel(rising);
                  bench::do_not_optimize(before);
              }));

    print_row("Atomic::fetch_max_acq_rel keeps", bench::run("Atomic::fetch_max_acq_rel keeps", [&] {
                  std::uint64_t const before = rising_cell.fetch_max_acq_rel(0);
                  bench::do_not_optimize(before);
              }));

    print_row("Atomic::fetch_min_acq_rel keeps", bench::run("Atomic::fetch_min_acq_rel keeps", [&] {
                  std::uint64_t const before = rising_cell.fetch_min_acq_rel(rising + 1);
                  bench::do_not_optimize(before);
              }));

    print_row("Tally::add", bench::run("Tally::add", [&] {
                  tally.add(operand);
                  bench::do_not_optimize(operand);
              }));

    print_row("Tally::read", bench::run("Tally::read", [&] {
                  std::uint64_t const total = tally.read();
                  bench::do_not_optimize(total);
              }));

    // The cell of one writer.  On x86-64 a relaxed load and an acquire load
    // are the same mov, so load_owned and load_acquire cost the same here.
    // The measured loops borrow the writer through a Ref to a const writer,
    // so no loop can end it.
    auto writer = fixy::mint_sole_writer<BenchHeadRole>();
    decltype(writer) const& shared_writer = writer;
    auto const lent_writer = fixy::to(shared_writer);
    fixy::Atomic<std::uint64_t, decltype(writer)> owned_cell{1};

    print_row("Atomic<SoleWriter>::load_owned", bench::run("Atomic<SoleWriter>::load_owned", [&, lent_writer] {
                  std::uint64_t const value = owned_cell.load_owned(*lent_writer);
                  bench::do_not_optimize(value);
              }));

    print_row("Atomic<SoleWriter>::load_acquire", bench::run("Atomic<SoleWriter>::load_acquire", [&] {
                  std::uint64_t const value = owned_cell.load_acquire();
                  bench::do_not_optimize(value);
              }));

    print_row("Atomic<SoleWriter>::store_release", bench::run("Atomic<SoleWriter>::store_release", [&, lent_writer] {
                  owned_cell.store_release(*lent_writer, operand);
                  bench::do_not_optimize(operand);
              }));

    print_row("Atomic<SoleWriter>::load_owned and store_release",
              bench::run("Atomic<SoleWriter>::load_owned and store_release", [&, lent_writer] {
                  owned_cell.store_release(*lent_writer, owned_cell.load_owned(*lent_writer) + 1);
                  bench::do_not_optimize(operand);
              }));

    ::foundation::linear::drop(std::move(writer));
    return 0;
}
