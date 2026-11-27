// with_low_bits of a signed value does not compile: the low bits of a
// negative value are the bits of its two's complement, which no caller means.
// A signed source goes through one explicit conversion at the call.

#include <foundation/core/PackedWord.h>

#include <cstdint>

struct Tags final {};
struct Data final {};

int main() {
    ::foundation::core::PackedWord<std::uint16_t, ::foundation::core::BitField<Tags, 10>,
                                   ::foundation::core::BitField<Data, 5>> const word{};
    return word.with_low_bits<Data>(std::int16_t{-1}).bits();
}
