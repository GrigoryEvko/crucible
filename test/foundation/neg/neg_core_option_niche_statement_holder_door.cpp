// A class outside the base states that no door builds its empty value.  Its
// constructor is private, and a public static door gives a plain class with
// a field of the slot, built from a raw word with no voucher.  The door gives
// a value that holds the slot, so the walk judges it as a constructor of the
// slot (holds_class, the check of the statement of foundation/core/Choice.h).
// So the walk of the doors refuses the class, and the Option of the class
// does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

struct HeldBox;

class HolderDoorSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit HolderDoorSlot(std::uint32_t raw) noexcept : raw_{raw} {}
    friend struct HeldBox;

public:
    constexpr HolderDoorSlot(HolderDoorSlot const&) noexcept = default;
    [[nodiscard]] static constexpr HeldBox boxed(std::uint32_t raw) noexcept;

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr HolderDoorSlot
    niche_empty(::foundation::core::niche_key<HolderDoorSlot> const&) noexcept {
        return HolderDoorSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<HolderDoorSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

struct HeldBox {
    HolderDoorSlot slot;
};

constexpr HeldBox HolderDoorSlot::boxed(std::uint32_t raw) noexcept { return HeldBox{HolderDoorSlot{raw}}; }

int main() {
    return ::foundation::core::Option<HolderDoorSlot>::some(HolderDoorSlot::boxed(0xFFFFFFFFu).slot).is_some() ? 0 : 1;
}
