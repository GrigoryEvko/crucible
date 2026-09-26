// A uint32_t cannot hold the bound -1.  A conversion would make the bound
// the maximum of the carrier, so the counter would take any value.  The
// door refuses a bound that the carrier cannot hold exactly.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_bounded_monotonic<std::uint32_t, -1>(0u);
    return static_cast<int>(counter.get());
}
