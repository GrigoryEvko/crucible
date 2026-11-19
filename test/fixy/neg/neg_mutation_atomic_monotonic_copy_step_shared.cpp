// The checked publish that steps a copy of the counter is sound only for the
// one thread that writes the counter and keeps the copy.  A counter of many
// writers has no such thread, so its type has no such publish, as it has no
// advance_sole_writer_from of two values.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    auto copy = fixy::mint_monotonic<std::uint64_t>(0);
    counter.advance_sole_writer_from(0, 1, copy);
    return 0;
}
