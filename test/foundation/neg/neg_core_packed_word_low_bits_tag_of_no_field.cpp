// with_low_bits of a tag that names no field of the word does not compile:
// the word holds no place for the tag.

#include <foundation/core/PackedWord.h>

#include <cstdint>

struct Tags final {};
struct Data final {};
struct Other final {};

int main() {
    ::foundation::core::PackedWord<std::uint16_t, ::foundation::core::BitField<Tags, 10>,
                                   ::foundation::core::BitField<Data, 5>> const word{};
    return word.with_low_bits<Other>(std::uint8_t{1}).bits();
}
