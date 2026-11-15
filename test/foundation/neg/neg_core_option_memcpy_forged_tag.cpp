// A memcpy from bytes of a different type over a plain Option can write a tag
// that names no alternative, such as a tag of 2.  The cells of the tag carry
// the mark closed_tag, so the quarantine plugin refuses the copy.

#include <foundation/core/Choice.h>

#include <cstdint>

struct ForgedWords {
    std::uint64_t payload = 7;
    std::uint64_t tag = 2;
};

int main() {
    ::foundation::core::Option<std::uint64_t> forged{};
    ForgedWords const words{};
    __builtin_memcpy(static_cast<void*>(&forged), &words, sizeof(words));
    return forged.is_some() ? 1 : 0;
}
