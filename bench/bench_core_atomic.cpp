// Microbench of the operations of fixy::Atomic and fixy::Tally on one
// thread.
//
// Each operation of the Atomic family is one __atomic builtin, so its cost
// is the cost of the instruction that the builtin gives.  The bench gives
// the latency of each operation on a cell that one thread holds in its own
// cache, with the sink of bench::do_not_optimize in each sample.  A test of
// the same operations under contention is in test/fixy/test_core_atomic.cpp.
//
// Each line gives the percentiles of one operation in picoseconds, and the
// coefficient of variation in thousandths.  The Report family writes
// integers only, so the bench writes no fraction of a nanosecond.  The rows
// of the weak compare-and-swap, the xor and the count stand beside the same
// operation of std::atomic, with the ratio of the two medians.

#include <fixy/Core.h>
#include <foundation/Linear.h>
#include <foundation/Quarantine.h>

#include <atomic>
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
    bench::print("{}  p50={} ps  p99={} ps  p99.9={} ps  max={} ps  cv={} permille\n", name,
                 picoseconds(measured.pct.p50), picoseconds(measured.pct.p99), picoseconds(measured.pct.p99_9),
                 picoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

// Writes the rows of an operation and of its reference, and the ratio of
// their medians in thousandths.  A ratio above 1000 is slower than the
// reference.
void print_pair(fixy::TextView name, bench::Report const& measured, fixy::TextView reference_name,
                bench::Report const& reference) noexcept {
    print_row(name, measured);
    print_row(reference_name, reference);
    std::uint64_t const ratio =
        reference.pct.p50 > 0.0 ? static_cast<std::uint64_t>(measured.pct.p50 * 1000.0 / reference.pct.p50 + 0.5) : 0;
    bench::print("{}  ratio to the reference: p50 {} permille\n", name, ratio);
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: std::atomic is the reference of the weak swap, the xor and the count")
namespace {

using Cell = fixy::Atomic<std::uint64_t>;
using Count = fixy::Atomic<std::uint64_t, fixy::Counter>;
using ReferenceCell = std::atomic<std::uint64_t>;

// Layout
//   Each operation of a pair is one call to a function of its own, aligned
//   to 64 bytes and never inlined, so the code around the operation does not
//   move one side of a pair against the other.  With the operation inlined in
//   its loop, the count load and the relaxed load of std::atomic differed by
//   12%, with the same instructions.  The value that a row carries from one
//   sample to the next is a member of its lambda, so it can stay in a
//   register.

[[gnu::noinline, gnu::aligned(64)]] bool weak_swap_once(Cell& cell, std::uint64_t expected) noexcept {
    return cell.cas_weak_acq_rel(expected, expected + 1).is_ok();
}

[[gnu::noinline, gnu::aligned(64)]] bool reference_weak_swap_once(ReferenceCell& cell,
                                                                  std::uint64_t expected) noexcept {
    return cell.compare_exchange_weak(expected, expected + 1, std::memory_order_acq_rel, std::memory_order_acquire);
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t xor_once(Cell& cell, std::uint64_t bits) noexcept {
    return cell.fetch_xor_acq_rel(bits);
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t reference_xor_once(ReferenceCell& cell, std::uint64_t bits) noexcept {
    return cell.fetch_xor(bits, std::memory_order_acq_rel);
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t add_once(Count& count, std::uint64_t delta) noexcept {
    return count.fetch_add_relaxed(delta);
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t reference_add_once(ReferenceCell& cell,
                                                                     std::uint64_t delta) noexcept {
    return cell.fetch_add(delta, std::memory_order_relaxed);
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t read_once(Count const& count) noexcept {
    return count.load_acquire();
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t reference_read_once(ReferenceCell const& cell) noexcept {
    return cell.load(std::memory_order_relaxed);
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t max_once(Count& count, std::uint64_t value) noexcept {
    return count.fetch_max_relaxed(value);
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t reference_max_once(ReferenceCell& cell,
                                                                     std::uint64_t value) noexcept {
    return cell.fetch_max(value, std::memory_order_relaxed);
}

// The rows of the weak compare-and-swap, the xor and the count, each beside
// the same operation of std::atomic.  Only this thread touches the cells,
// and each cell is alone on its cache line.
void report_reference_pairs() noexcept {
    fixy::CacheLine<Cell> cell{std::uint64_t{1}};
    fixy::CacheLine<Count> count{};
    fixy::CacheLine<ReferenceCell> reference_cell{std::uint64_t{1}};
    constexpr std::uint64_t kOperand = 3;
    // A bound above each count of the rows, so each bound of a rising value
    // stores.
    constexpr std::uint64_t kBoundBase = std::uint64_t{1} << 40;

    bench::Report const weak =
        bench::run("Atomic::cas_weak_acq_rel stores", [&cell, expected = std::uint64_t{1}]() mutable {
            bool const stored = weak_swap_once(*cell, expected);
            expected += stored ? 1 : 0;
            bench::do_not_optimize(stored);
        });
    bench::Report const reference_weak = bench::run(
        "std::atomic::compare_exchange_weak stores", [&reference_cell, expected = std::uint64_t{1}]() mutable {
            bool const stored = reference_weak_swap_once(*reference_cell, expected);
            expected += stored ? 1 : 0;
            bench::do_not_optimize(stored);
        });
    print_pair("Atomic::cas_weak_acq_rel stores", weak, "std::atomic::compare_exchange_weak stores", reference_weak);

    bench::Report const flips = bench::run("Atomic::fetch_xor_acq_rel", [&cell] {
        std::uint64_t const before = xor_once(*cell, kOperand);
        bench::do_not_optimize(before);
    });
    bench::Report const reference_flips = bench::run("std::atomic::fetch_xor", [&reference_cell] {
        std::uint64_t const before = reference_xor_once(*reference_cell, kOperand);
        bench::do_not_optimize(before);
    });
    print_pair("Atomic::fetch_xor_acq_rel", flips, "std::atomic::fetch_xor", reference_flips);

    bench::Report const adds = bench::run("Atomic<Counter>::fetch_add_relaxed", [&count] {
        std::uint64_t const before = add_once(*count, kOperand);
        bench::do_not_optimize(before);
    });
    bench::Report const reference_adds = bench::run("std::atomic::fetch_add relaxed", [&reference_cell] {
        std::uint64_t const before = reference_add_once(*reference_cell, kOperand);
        bench::do_not_optimize(before);
    });
    print_pair("Atomic<Counter>::fetch_add_relaxed", adds, "std::atomic::fetch_add relaxed", reference_adds);

    bench::Report const reads = bench::run("Atomic<Counter>::load_acquire", [&count] {
        std::uint64_t const value = read_once(*count);
        bench::do_not_optimize(value);
    });
    bench::Report const reference_reads = bench::run("std::atomic::load relaxed", [&reference_cell] {
        std::uint64_t const value = reference_read_once(*reference_cell);
        bench::do_not_optimize(value);
    });
    print_pair("Atomic<Counter>::load_acquire", reads, "std::atomic::load relaxed", reference_reads);

    bench::Report const bounds =
        bench::run("Atomic<Counter>::fetch_max_relaxed stores", [&count, rising = kBoundBase]() mutable {
            rising += 1;
            std::uint64_t const before = max_once(*count, rising);
            bench::do_not_optimize(before);
        });
    bench::Report const reference_bounds =
        bench::run("std::atomic::fetch_max relaxed stores", [&reference_cell, rising = kBoundBase]() mutable {
            rising += 1;
            std::uint64_t const before = reference_max_once(*reference_cell, rising);
            bench::do_not_optimize(before);
        });
    print_pair("Atomic<Counter>::fetch_max_relaxed stores", bounds, "std::atomic::fetch_max relaxed stores",
               reference_bounds);

    // A bound below the count keeps the count.  The count then writes
    // nothing, and the std::atomic form still runs its lock cmpxchg.
    bench::Report const kept = bench::run("Atomic<Counter>::fetch_max_relaxed keeps", [&count] {
        std::uint64_t const before = max_once(*count, 0);
        bench::do_not_optimize(before);
    });
    bench::Report const reference_kept = bench::run("std::atomic::fetch_max relaxed keeps", [&reference_cell] {
        std::uint64_t const before = reference_max_once(*reference_cell, 0);
        bench::do_not_optimize(before);
    });
    print_pair("Atomic<Counter>::fetch_max_relaxed keeps", kept, "std::atomic::fetch_max relaxed keeps",
               reference_kept);
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

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

    // The operand is an object in memory, so a row that makes it opaque for
    // the next call gives it to mark_changed(), which adds no load and no
    // store to the loop (the sink of bench_harness.h).
    print_row("Atomic::store_release", bench::run("Atomic::store_release", [&] {
                  cell.store_release(operand);
                  bench::mark_changed(operand);
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
                  bench::mark_changed(operand);
              }));

    print_row("Tally::read", bench::run("Tally::read", [&] {
                  std::uint64_t const total = tally.read();
                  bench::do_not_optimize(total);
              }));

    // The cell of one writer.  On x86-64 a relaxed load and an acquire load
    // are the same mov.  load_owned and store_release also compare the
    // run-time id of the proof with the id that the cell keeps.  The measured
    // loops borrow the writer through a Ref to a const writer, so no loop can
    // end it.
    auto writer = fixy::mint_sole_writer<BenchHeadRole>();
    decltype(writer) const& shared_writer = writer;
    auto const lent_writer = fixy::to(shared_writer);
    fixy::Atomic<std::uint64_t, decltype(writer)> owned_cell{writer, 1};

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
                  bench::mark_changed(operand);
              }));

    print_row("Atomic<SoleWriter>::load_owned and store_release",
              bench::run("Atomic<SoleWriter>::load_owned and store_release", [&, lent_writer] {
                  owned_cell.store_release(*lent_writer, owned_cell.load_owned(*lent_writer) + 1);
                  bench::mark_changed(operand);
              }));

    ::foundation::linear::drop(std::move(writer));

    report_reference_pairs();
    return 0;
}
