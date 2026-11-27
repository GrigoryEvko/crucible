// with_fitting of a value whose type has more bits than its field does not
// compile: the type does not prove that the value fits, so the write takes
// with and its refusal.

#include <foundation/core/PackedWord.h>

#include <cstdint>

struct Tags final {};
struct Data final {};

int main() {
    ::foundation::core::PackedWord<std::uint16_t, ::foundation::core::BitField<Tags, 10>,
                                   ::foundation::core::BitField<Data, 5>> const word{};
    return word.with_fitting<Data>(std::uint8_t{1}).bits();
}
