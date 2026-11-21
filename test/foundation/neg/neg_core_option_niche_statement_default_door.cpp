// A class states that no door builds the empty value, and its public default
// constructor builds the empty value.  The payload gate checks the statement
// against the constructors of the class (THE CHECK OF THE STATEMENT of
// foundation/core/Choice.h), so the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class SlotNumber {
    std::uint32_t raw_ = 0xFFFFFFFFu;

public:
    constexpr SlotNumber() noexcept = default;

    static constexpr bool is_niche_empty_never_built = true;
    [[nodiscard]] static constexpr SlotNumber niche_empty(::foundation::core::niche_key<SlotNumber> const&) noexcept {
        return SlotNumber{};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<SlotNumber> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() { return sizeof(::foundation::core::Option<SlotNumber>) > 0 ? 0 : 1; }
