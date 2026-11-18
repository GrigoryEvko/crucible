// A layout whose widths sum past the bits of the word does not compile: the
// word cannot hold each field.

#include <foundation/core/PackedWord.h>

#include <cstdint>

struct Tags final {};
struct Data final {};

int main() {
    [[maybe_unused]] ::foundation::core::PackedWord<std::uint8_t, ::foundation::core::BitField<Tags, 5>,
                                                    ::foundation::core::BitField<Data, 4>> const word{};
    return 0;
}
