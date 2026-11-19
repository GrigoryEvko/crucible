// A Monotonic steps from a value that its caller names only inside the
// checked publish of AtomicMonotonic, whose check proves that the copy holds
// that value.  A caller outside it could name a stale value and move the copy
// backward, so the step is private to the pair.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto copy = fixy::mint_monotonic<std::uint64_t>(10);
    copy.advance_from_checked_(0, 1);
    return 0;
}
