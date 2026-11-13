// A foreign ABI can write each bit pattern, and an enumeration with
// enumerators has values that no byte pattern proves, so mint_foreign_view
// refuses a run of such an enumeration.

#include <foundation/core/Region.h>

#include <cstdint>

enum class Mode : std::uint8_t {
    off,
    on
};

int main() {
    auto const modes = ::foundation::core::mint_foreign_view(static_cast<Mode const*>(nullptr), 0);
    return static_cast<int>(modes.size());
}
