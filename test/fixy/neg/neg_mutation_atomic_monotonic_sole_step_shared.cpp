// advance_sole_writer_from checks that its caller holds the stored value,
// and then it stores with a plain release store.  That is sound only for the
// one thread that writes the counter: a second writer can store between the
// check and the store.  A counter of many writers has no such thread, so its
// type has no advance_sole_writer_from, as it has no peek_relaxed.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    counter.advance_sole_writer_from(0, 1);
    return 0;
}
