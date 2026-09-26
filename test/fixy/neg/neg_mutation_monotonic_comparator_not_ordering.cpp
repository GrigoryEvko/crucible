// An int is not a comparison, so "not backward" has no meaning for the
// counter.  The door refuses a comparator that is not a strict weak order
// over the carrier.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_monotonic<std::uint32_t, int>(0u);
    return static_cast<int>(counter.get());
}
