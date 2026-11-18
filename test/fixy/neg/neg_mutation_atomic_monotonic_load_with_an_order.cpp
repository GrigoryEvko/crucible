// The order of each read of an AtomicMonotonic is in its name: get() reads
// with acquire order, peek_relaxed() is the read of the one writer, and
// load_relaxed() seeds a compare-and-swap.  A read that takes an order as an
// argument would let a caller give the counter any order, also a relaxed read
// of a value that a different thread stored.  So the read with an order is
// deleted, and its message names the three reads.  The empty braces give the
// read an order with no name of the standard library.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_atomic_monotonic<std::uint64_t>(0);
    return static_cast<int>(counter.load({}));
}
