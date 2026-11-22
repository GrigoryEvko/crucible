// Microbench of one access of foundation::ThreadLocalRef on one thread, with
// a plain thread_local of an inline function as the reference.
//
// A ThreadLocalRef keeps its cell in the table of the thread, at the index
// that the process-wide registry gave its (Tag, T) at the first use.  So one
// access is the test of the static of the index, the load of the index and
// the TLS address of the table.  The reference is the thread_local of an
// inline function, the form that gives each shared library its own cell.
//
// Each line gives the percentiles of one operation in picoseconds, and the
// coefficient of variation in thousandths.  The last two lines give the
// ratio of the median of each operation of ThreadLocalRef to the median of
// the reference, in thousandths.  The Report family writes integers only.

#include <fixy/Core.h>
#include <foundation/Quarantine.h>
#include <foundation/ThreadLocalRef.h>

#include <cstdint>

#include "bench_harness.h"

// A tag needs a stable identity, so it stands in a named namespace.
namespace bench_thread_local_ref {

struct ShardTag {};

}  // namespace bench_thread_local_ref

namespace {

using ShardCell = ::foundation::ThreadLocalRef<bench_thread_local_ref::ShardTag, std::uint64_t>;

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

// Writes the ratio of the median of an operation to the median of its
// reference, in thousandths.
void print_ratio(fixy::TextView name, bench::Report const& measured, bench::Report const& reference) noexcept {
    bench::print("{}  ratio to the plain thread_local: {} permille\n", name,
                 static_cast<std::uint64_t>(measured.pct.p50 * 1000.0 / reference.pct.p50 + 0.5));
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a plain thread_local is the reference of one access of ThreadLocalRef")
namespace {

[[nodiscard]] inline std::uint64_t& plain_cell() noexcept {
    thread_local std::uint64_t storage = 0;
    return storage;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    bench::elevate_priority();

    const ShardCell cell{};
    std::uint64_t operand = 3;
    cell.store(operand);
    plain_cell() = operand;

    // The input sink makes the cell opaque, so each call reads it.  The cache
    // of the index of ThreadLocalRef is an Atomic, and the optimizer loads an
    // atomic in each call, so peek reads the index and its cell in each call
    // with no sink.
    bench::Report const plain_read = bench::run("plain thread_local read", [&] {
        bench::mark_changed(plain_cell());
        std::uint64_t const value = plain_cell();
        bench::do_not_optimize(value);
    });
    print_row("plain thread_local read", plain_read);

    // The two rows of a store measure a store, so the barrier keeps the store
    // of each call (the sink of bench_harness.h).
    bench::Report const plain_write = bench::run("plain thread_local write", [&] {
        plain_cell() = operand;
        bench::clobber_memory();
    });
    print_row("plain thread_local write", plain_write);

    bench::Report const peek = bench::run("ThreadLocalRef::peek", [&] {
        std::uint64_t const value = cell.peek();
        bench::do_not_optimize(value);
    });
    print_row("ThreadLocalRef::peek", peek);

    bench::Report const store = bench::run("ThreadLocalRef::store", [&] {
        cell.store(operand);
        bench::clobber_memory();
    });
    print_row("ThreadLocalRef::store", store);

    print_ratio("ThreadLocalRef::peek", peek, plain_read);
    print_ratio("ThreadLocalRef::store", store, plain_write);
    return 0;
}
