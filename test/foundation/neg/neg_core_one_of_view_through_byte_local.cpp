// A copy of bytes through a local std::byte* onto a plain OneOf can write a
// tag that names no alternative.  The rule forged-tag of the quarantine
// plugin refuses the conversion of the address of the OneOf into a pointer
// to bytes that can write, the view of bytes, also through a pointer to void
// in the same expression.

#include <foundation/core/OneOf.h>

#include <cstddef>
#include <cstdint>

struct ForgedReading {
    std::uint32_t storage = 0;
    std::uint32_t tag_word = 200;
};

using Reading = foundation::core::OneOf<std::uint32_t, std::uint16_t>;

int main() {
    Reading forged{std::uint32_t{1}};
    ForgedReading const words{};
    auto* const bytes = static_cast<std::byte*>(static_cast<void*>(&forged));
    __builtin_memcpy(bytes, &words, sizeof(words));
    return forged.is<std::uint16_t>() ? 1 : 0;
}
