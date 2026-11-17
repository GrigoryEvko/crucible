// A BitSet of more than 64 positions holds more than one word, so no single
// word gives its bits, and the mint refuses the set.

#include <foundation/core/BitSet.h>

#include <cstdint>

int main() {
    auto const wide = ::foundation::core::mint_bit_set_from_bits<65>(std::uint64_t{1});
    return wide.is_some() ? 0 : 1;
}
