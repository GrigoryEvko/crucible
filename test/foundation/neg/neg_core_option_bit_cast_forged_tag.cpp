// A bit copy into a plain Option can write a tag that names no alternative.
// The tag lives in a class with the annotation foundation::core::closed_tag,
// so the quarantine plugin refuses std::bit_cast into an Option in each file
// and each mode.

#include <foundation/core/Choice.h>

#include <bit>
#include <cstdint>

struct ForgedWords {
    std::uint64_t payload = 7;
    std::uint64_t tag = 2;
};

int main() {
    auto const forged = std::bit_cast<::foundation::core::Option<std::uint64_t>>(ForgedWords{});
    return forged.is_some() ? 1 : 0;
}
