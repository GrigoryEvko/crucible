// A class outside the base states that no door builds its empty value.  Its
// constructor from a raw word is protected, and the class is not final.  A
// class outside derives from it and builds its base part from the empty
// word, and a copy of the derived value gives that base part as a slot.  The
// walks judge each member that is not private (the check of the statement of
// foundation/core/Choice.h), so the walk of the doors refuses the class, and
// the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class ProtectedDoorSlot {
    std::uint32_t raw_ = 0;

protected:
    constexpr explicit ProtectedDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    constexpr ProtectedDoorSlot(ProtectedDoorSlot const&) noexcept = default;

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr ProtectedDoorSlot
    niche_empty(::foundation::core::niche_key<ProtectedDoorSlot> const&) noexcept {
        return ProtectedDoorSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool
    is_niche_empty(::foundation::core::niche_key<ProtectedDoorSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

struct DerivedSlot : ProtectedDoorSlot {
    constexpr DerivedSlot() noexcept : ProtectedDoorSlot{0xFFFFFFFFu} {}
};

int main() {
    ProtectedDoorSlot const sliced = DerivedSlot{};
    return ::foundation::core::Option<ProtectedDoorSlot>::some(sliced).is_some() ? 0 : 1;
}
