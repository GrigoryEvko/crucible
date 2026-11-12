// The empty value of the member niche protocol has the type of the class.
// A niche_empty() that gives the raw word declares a niche that is not well
// formed, so the payload gate refuses the payload.

#include <foundation/core/Choice.h>

#include <cstdint>

struct SlotNumber {
    std::uint32_t raw = 0;

    [[nodiscard]] static constexpr std::uint32_t
    niche_empty(::foundation::core::niche_key<SlotNumber> const&) noexcept {
        return 0xFFFFFFFFu;
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<SlotNumber> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

int main() { return sizeof(::foundation::core::Option<SlotNumber>) > 0 ? 0 : 1; }
