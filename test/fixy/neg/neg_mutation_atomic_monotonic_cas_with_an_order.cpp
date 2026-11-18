// The order of each compare-and-swap of an AtomicMonotonic is in its name:
// compare_exchange_advance_acq_rel, _weak_acq_rel, _seq_cst and
// _weak_relaxed.  No form takes an order as an argument, so no caller can
// give a failure order that the standard forbids, such as release.  The two
// empty braces stand for a success order and a failure order.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    std::uint64_t expected = 0;
    return static_cast<int>(counter.compare_exchange_advance_acq_rel(expected, 1, {}, {}));
}
