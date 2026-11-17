// The word of a BitSet of five positions is one byte.  A word of two bytes
// is the word of a different set, so the mint refuses it.

#include <foundation/core/BitSet.h>

#include <cstdint>

int main() {
    auto const slots = ::foundation::core::mint_bit_set_from_bits<5>(std::uint16_t{1});
    return slots.is_some() ? 0 : 1;
}
