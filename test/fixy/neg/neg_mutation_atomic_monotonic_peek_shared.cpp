// peek_relaxed reads a counter with relaxed order.  That read is sound only
// for the one thread that writes the counter, because that thread reads its
// own last store.  A counter of many writers, such as the top of a
// work-stealing deque, has no such thread, so its type has no peek_relaxed.
// A writer of such a counter seeds a compare-exchange with load_relaxed.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    return static_cast<int>(counter.peek_relaxed());
}
