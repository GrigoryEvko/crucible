// Sentinel TU for foundation/ThreadLocalRef.h.  Handles that name one Tag
// and T reach one cell, a different Tag reaches a different cell, and each
// thread has a cell of its own.

#include <foundation/ThreadLocalRef.h>

#include <bit>
#include <cstdint>
#include <thread>

namespace {

namespace fnd = ::foundation;

struct CounterTag {};
struct AccumulatorTag {};
struct OtherTag {};

using IntCounter = fnd::ThreadLocalRef<CounterTag, int>;
using IntAccumulator = fnd::ThreadLocalRef<AccumulatorTag, int>;
using DoubleOther = fnd::ThreadLocalRef<OtherTag, double>;

[[nodiscard]] int cell_round_trips() {
    const IntCounter counter{};
    if (counter.peek() != 0) return 1;
    counter.store(34);
    if (counter.peek() != 34) return 2;
    counter.peek_mut() = 51;
    if (counter.peek() != 51) return 3;
    counter.reset();
    if (counter.peek() != 0) return 4;
    return 0;
}

// Same T, different Tag: a write to one does not reach the other.  Same
// Tag: two handles and a minted one reach one cell.
[[nodiscard]] int tags_select_cells() {
    const IntCounter counter{};
    const IntAccumulator accumulator{};
    counter.store(100);
    accumulator.store(200);
    if (counter.peek() != 100 || accumulator.peek() != 200) return 1;
    const IntCounter second{};
    second.store(17);
    if (counter.peek() != 17) return 2;
    const auto minted = fnd::mint_thread_local_ref<CounterTag, int>();
    if (minted.peek() != 17) return 3;
    counter.reset();
    accumulator.reset();
    return 0;
}

// The compare goes through bit_cast because equality on a floating-point
// value is an error under the project warning set.
[[nodiscard]] int floating_cell_round_trips() {
    const DoubleOther other{};
    other.store(2.5);
    const bool same_bits = std::bit_cast<std::uint64_t>(other.peek()) == std::bit_cast<std::uint64_t>(2.5);
    other.reset();
    return same_bits ? 0 : 1;
}

// A second thread starts from its own default cell, and its write does not
// reach the cell of this thread.
[[nodiscard]] int each_thread_has_its_own_cell() {
    const IntCounter counter{};
    counter.store(5);
    int seen_by_worker = -1;
    {
        std::jthread worker{[&seen_by_worker] {
            const IntCounter worker_counter{};
            seen_by_worker = worker_counter.peek();
            worker_counter.store(99);
        }};
    }
    const bool isolated = seen_by_worker == 0 && counter.peek() == 5;
    counter.reset();
    return isolated ? 0 : 1;
}

}  // namespace

int main() {
    if (const int failed = cell_round_trips(); failed != 0) return 10 + failed;
    if (const int failed = tags_select_cells(); failed != 0) return 20 + failed;
    if (const int failed = floating_cell_round_trips(); failed != 0) return 30 + failed;
    if (const int failed = each_thread_has_its_own_cell(); failed != 0) return 40 + failed;
    return 0;
}
