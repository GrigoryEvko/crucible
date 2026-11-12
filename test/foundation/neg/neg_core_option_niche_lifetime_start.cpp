// The empty value of this niche has bytes that are not zero.  Zero bytes
// are then a full Option that no constructor made, so the checked lifetime
// start refuses the Option.

#include <foundation/Lifetime.h>
#include <foundation/core/Choice.h>

#include <cstdint>

struct SlotNumber {
    std::uint32_t raw = 0;
};

template <>
struct foundation::core::niche<SlotNumber> {
    [[nodiscard]] static constexpr SlotNumber empty() noexcept { return SlotNumber{0xFFFFFFFFu}; }
    [[nodiscard]] static constexpr bool is_empty(SlotNumber const& slot) noexcept { return slot.raw == 0xFFFFFFFFu; }
};

int main() {
    std::uint64_t word = 0;
    static_cast<void>(::foundation::lifetime::start_as_array<::foundation::core::Option<SlotNumber>>(&word, 1));
    return 0;
}
