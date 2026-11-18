// A layout that names one tag two times does not compile: get and with of the
// tag would name two places.

#include <foundation/core/PackedWord.h>

#include <cstdint>

struct Tags final {};

int main() {
    [[maybe_unused]] ::foundation::core::PackedWord<std::uint16_t, ::foundation::core::BitField<Tags, 4>,
                                                    ::foundation::core::BitField<Tags, 4>> const word{};
    return 0;
}
