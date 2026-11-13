// A bit copy into a plain OneOf can write a tag that names no alternative.
// The tag lives in a class with the annotation foundation::core::closed_tag,
// so the quarantine plugin refuses std::bit_cast into a OneOf in each file and
// each mode.

#include <foundation/core/OneOf.h>

#include <bit>
#include <cstdint>

struct ForgedReading {
    std::uint32_t storage = 0;
    std::uint32_t tag_word = 200;
};

using Reading = foundation::core::OneOf<std::uint32_t, std::uint16_t>;

int main() {
    Reading const forged = std::bit_cast<Reading>(ForgedReading{});
    return static_cast<int>(forged.which().raw());
}
