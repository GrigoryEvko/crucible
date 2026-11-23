// A bit copy into a plain Result can write a tag that names no alternative.
// The tag lives in a class with the annotation foundation::core::closed_tag,
// so the quarantine plugin refuses std::bit_cast into a Result in each file
// and each mode.

#include <foundation/core/Choice.h>

#include <bit>
#include <cstdint>

enum class Refusal : std::uint8_t {
    full,
    closed
};

struct ForgedWords {
    std::uint64_t value = 7;
    std::uint64_t tag = 2;
};

int main() {
    auto const forged = std::bit_cast<::foundation::core::Result<std::uint64_t, Refusal>>(ForgedWords{});
    return forged.is_ok() ? 1 : 0;
}
