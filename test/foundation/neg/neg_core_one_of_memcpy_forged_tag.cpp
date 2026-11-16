// A memcpy from bytes of a different type over a plain OneOf can write a tag
// that names no alternative, such as a tag of 200.  A match would then run
// the arm of the last alternative on bytes of no alternative.  The cells of
// the tag carry the mark closed_tag, so the quarantine plugin refuses the
// copy.

#include <foundation/core/OneOf.h>

#include <cstdint>

struct ForgedReading {
    std::uint32_t storage = 7;
    std::uint32_t tag_word = 200;
};

using Reading = ::foundation::core::OneOf<std::uint32_t, std::uint16_t>;

int main() {
    Reading forged{std::uint32_t{1}};
    ForgedReading const bytes{};
    __builtin_memcpy(static_cast<void*>(&forged), &bytes, sizeof(bytes));
    return forged.is<std::uint16_t>() ? 1 : 0;
}
