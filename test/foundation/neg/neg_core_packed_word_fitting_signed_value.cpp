// with_fitting of a signed value does not compile, also when its type has no
// more bits than the field: a negative value would set each bit above the
// field.  A signed source goes through one explicit conversion at the call.

#include <foundation/core/PackedWord.h>

#include <cstdint>

struct Tags final {};
struct Data final {};

int main() {
    ::foundation::core::PackedWord<std::uint16_t, ::foundation::core::BitField<Tags, 10>,
                                   ::foundation::core::BitField<Data, 5>> const word{};
    return word.with_fitting<Tags>(std::int8_t{-1}).bits();
}
