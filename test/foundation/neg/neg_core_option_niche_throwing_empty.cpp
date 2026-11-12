// A niche whose empty() can throw is not well formed.  The Option would
// fall to the plain form with no message, so the payload gate refuses the
// payload.

#include <foundation/core/Choice.h>

#include <cstdint>

struct SlotNumber {
    std::uint32_t raw = 0;
};

template <>
struct foundation::core::niche<SlotNumber> {
    [[nodiscard]] static constexpr SlotNumber empty() { return SlotNumber{0xFFFFFFFFu}; }
    [[nodiscard]] static constexpr bool is_empty(SlotNumber const& slot) noexcept { return slot.raw == 0xFFFFFFFFu; }
};

int main() { return sizeof(::foundation::core::Option<SlotNumber>) > 0 ? 0 : 1; }
