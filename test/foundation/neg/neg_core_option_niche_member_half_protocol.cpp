// A class that gives niche_empty() and no is_niche_empty() declares half
// of the member niche protocol.  The Option would fall to the plain form
// with no message, so the payload gate refuses the payload.

#include <foundation/core/Choice.h>

#include <cstdint>

struct SlotNumber {
    std::uint32_t raw = 0;

    [[nodiscard]] static constexpr SlotNumber niche_empty() noexcept { return SlotNumber{0xFFFFFFFFu}; }
};

int main() { return sizeof(::foundation::core::Option<SlotNumber>) > 0 ? 0 : 1; }
