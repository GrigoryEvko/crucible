// A class forges the statement that no door builds the empty value: its
// public constructor takes the null pointer and no key, and it builds the
// empty value from it.  The payload gate checks the statement against the
// constructors of the class (THE CHECK OF THE STATEMENT of
// foundation/core/Choice.h), so the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstddef>
#include <cstdint>

class SlotNumber {
    std::uint32_t raw_ = 0;

public:
    constexpr explicit SlotNumber(std::nullptr_t /*address*/) noexcept {}

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr SlotNumber niche_empty(::foundation::core::niche_key<SlotNumber> const&) noexcept {
        return SlotNumber{nullptr};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<SlotNumber> const&) const noexcept {
        return raw_ == 0;
    }
};

int main() { return sizeof(::foundation::core::Option<SlotNumber>) > 0 ? 0 : 1; }
