// A uint8_t cannot hold the bound 256.  A conversion would make the bound
// 0, so the door refuses a bound that the carrier cannot hold exactly.

#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    auto counter = fixy::mint_bounded_monotonic<std::uint8_t, 256>(std::uint8_t{0});
    return static_cast<int>(counter.get());
}
