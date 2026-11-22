// Microbench of the work-stealing deque of fixy/concurrent/ChaseLevDeque.h on
// one thread.
//
// Each row gives the latency of one step of the owner or of a thief on a
// deque that one thread holds in its own cache, with the sink of
// bench::do_not_optimize in each sample.  The stress tests of
// test/test_chase_lev_deque.cpp run the same steps on several threads.
//
// Each step is one call to a function of its own, aligned to 64 bytes and
// never inlined, so the code of a row does not move one step against the
// other, and the instructions of each step stand apart in the binary.  Each
// line gives the percentiles of one row in picoseconds, and the coefficient
// of variation in thousandths.

#include <fixy/Core.h>
#include <fixy/concurrent/ChaseLevDeque.h>
#include <foundation/Linear.h>
#include <foundation/core/Ref.h>

#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

using Deque = ::fixy::concurrent::ChaseLevDeque<std::uint64_t, 1024>;
using Owner = Deque::OwnerEnd;
using Stealer = Deque::StealerEnd;

// A time in nanoseconds, as the nearest count of picoseconds.
[[nodiscard]] std::uint64_t picoseconds(double nanoseconds) noexcept {
    return static_cast<std::uint64_t>(nanoseconds * 1000.0 + 0.5);
}

// Writes one line: the name of the row and the percentiles of its samples.
void print_row(::fixy::TextView name, bench::Report const& measured) noexcept {
    bench::print("{}  p50={} ps  p99={} ps  p99.9={} ps  max={} ps  cv={} permille\n", name,
                 picoseconds(measured.pct.p50), picoseconds(measured.pct.p99), picoseconds(measured.pct.p99_9),
                 picoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

// The item of a take, or zero for no item.
[[nodiscard]] std::uint64_t item_or_zero(::fixy::Option<std::uint64_t> taken) noexcept {
    return std::move(taken).match([](std::uint64_t item) noexcept { return item; },
                                  []() noexcept { return std::uint64_t{0}; });
}

[[gnu::noinline, gnu::aligned(64)]] bool push_once(Owner& owner, std::uint64_t item) noexcept {
    return owner.push_bottom(item);
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t pop_once(Owner& owner) noexcept {
    return item_or_zero(owner.pop_bottom());
}

[[gnu::noinline, gnu::aligned(64)]] std::uint64_t steal_once(Stealer const& stealer) noexcept {
    return item_or_zero(stealer.steal_top());
}

// The codegen rows: push then steal, and push then pop, inlined in a loop of
// the caller.  The two ends are locals of this function, so the address of
// the deque stays in a register, and the loop has the instructions of the
// deque of one object.  The function takes the OwnerEnd by move and ends it.
[[gnu::noinline, gnu::aligned(64)]] std::uint64_t run_rounds(Owner&& moved, Stealer const& thief,
                                                             std::uint64_t rounds) noexcept {
    Owner owner{std::move(moved)};
    Stealer const stealer = thief;
    std::uint64_t sum = 0;
    for (std::uint64_t round = 0; round < rounds; ++round) {
        sum += owner.push_bottom(round) ? std::uint64_t{1} : std::uint64_t{0};
        sum += item_or_zero(stealer.steal_top());
    }
    for (std::uint64_t round = 0; round < rounds; ++round) {
        sum += owner.push_bottom(round) ? std::uint64_t{1} : std::uint64_t{0};
        sum += item_or_zero(owner.pop_bottom());
    }
    ::foundation::linear::drop(std::move(owner));
    return sum;
}

// The rows of the owner and of a thief.  Each sample of a pair pushes one
// item and takes it back, so the deque stays near empty, and each take runs
// its path with one item: the pop wins the last item with its compare-and-
// swap, and the steal advances top with its compare-and-swap.  Each row
// borrows the OwnerEnd through a Ref, and the codegen rows end the OwnerEnd
// after the rows.
void report_steps() noexcept {
    Deque deque{};
    auto ends = ::fixy::concurrent::mint_chase_lev_ends(deque);
    auto const owner = ::foundation::core::to(ends.owner);
    Stealer const stealer = ends.stealer;

    bench::Report const push_pop = bench::run("ChaseLevDeque push_bottom then pop_bottom", [owner] {
        bool const is_pushed = push_once(*owner, 7);
        bench::do_not_optimize(is_pushed);
        std::uint64_t const item = pop_once(*owner);
        bench::do_not_optimize(item);
    });
    print_row("ChaseLevDeque push_bottom then pop_bottom", push_pop);

    bench::Report const push_steal = bench::run("ChaseLevDeque push_bottom then steal_top", [owner, &stealer] {
        bool const is_pushed = push_once(*owner, 7);
        bench::do_not_optimize(is_pushed);
        std::uint64_t const item = steal_once(stealer);
        bench::do_not_optimize(item);
    });
    print_row("ChaseLevDeque push_bottom then steal_top", push_steal);

    bench::Report const empty_pop = bench::run("ChaseLevDeque pop_bottom of an empty deque", [owner] {
        std::uint64_t const item = pop_once(*owner);
        bench::do_not_optimize(item);
    });
    print_row("ChaseLevDeque pop_bottom of an empty deque", empty_pop);

    bench::Report const empty_steal = bench::run("ChaseLevDeque steal_top of an empty deque", [&stealer] {
        std::uint64_t const item = steal_once(stealer);
        bench::do_not_optimize(item);
    });
    print_row("ChaseLevDeque steal_top of an empty deque", empty_steal);

    bench::do_not_optimize(run_rounds(std::move(ends.owner), stealer, 64));
}

}  // namespace

int main() {
    report_steps();
    return 0;
}
