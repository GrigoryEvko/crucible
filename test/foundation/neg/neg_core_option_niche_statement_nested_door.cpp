// A class outside the base states that no door builds its empty value.  Its
// constructor is private, and a public nested class has a static function
// that builds the slot from the empty word through that constructor.  A
// nested class has the access of the class, so each nested class passes the
// arms of the own doors (the check of the statement of
// foundation/core/Choice.h).  So the walk of the doors refuses the class, and
// the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class NestedDoorSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit NestedDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Door {
        [[nodiscard]] static constexpr NestedDoorSlot empty_value() noexcept { return NestedDoorSlot{0xFFFFFFFFu}; }
    };

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr NestedDoorSlot
    niche_empty(::foundation::core::niche_key<NestedDoorSlot> const&) noexcept {
        return NestedDoorSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<NestedDoorSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() {
    return ::foundation::core::Option<NestedDoorSlot>::some(NestedDoorSlot::Door::empty_value()).is_some() ? 0 : 1;
}
