// The member form of the niche protocol takes the key too.  A call with no
// key would give the empty value of the payload to any caller.

#include <foundation/core/Choice.h>

#include <cstdint>

struct SlotNumber {
    std::uint32_t raw = 0;

    [[nodiscard]] static constexpr SlotNumber niche_empty(::foundation::core::niche_key<SlotNumber> const&) noexcept {
        return SlotNumber{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<SlotNumber> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

int main() { return static_cast<int>(SlotNumber::niche_empty().raw); }
