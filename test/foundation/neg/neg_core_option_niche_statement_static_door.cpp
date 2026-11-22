// A class outside the base states that no door builds its empty value.  Its
// constructor is private, and a public static door over it builds a value
// from each raw word, also the empty value.  A reflection over the
// constructors does not see such a door, so the statement is open to a type
// of the base only (THE CHECK OF THE STATEMENT of foundation/core/Choice.h),
// and the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class StaticDoorSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit StaticDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    [[nodiscard]] static constexpr StaticDoorSlot of(std::uint32_t raw) noexcept { return StaticDoorSlot{raw}; }

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr StaticDoorSlot
    niche_empty(::foundation::core::niche_key<StaticDoorSlot> const&) noexcept {
        return StaticDoorSlot{0u};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<StaticDoorSlot> const&) const noexcept {
        return raw_ == 0;
    }
};

int main() { return ::foundation::core::Option<StaticDoorSlot>::some(StaticDoorSlot::of(0)).is_some() ? 0 : 1; }
