// A copy of bytes through a local void* onto a plain Option can write a tag
// that names no alternative.  The rule forged-tag of the quarantine plugin
// refuses the conversion of the address of the Option into a pointer to void
// that can write, the view of bytes, so no such local exists.

#include <foundation/core/Choice.h>

#include <cstdint>

struct ForgedWords {
    std::uint64_t payload = 7;
    std::uint64_t tag = 2;
};

int main() {
    ::foundation::core::Option<std::uint64_t> forged{};
    ForgedWords const words{};
    void* const raw = &forged;
    __builtin_memcpy(raw, &words, sizeof(words));
    return forged.is_some() ? 1 : 0;
}
