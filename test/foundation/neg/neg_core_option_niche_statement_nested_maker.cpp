// A class outside the base states that no door builds its empty value.  A
// private nested class has a const member function that builds the slot
// from the empty word, and a public static function gives a value of the
// nested class, so a caller reaches the member through that value.  The
// object of a nested class is no value of the slot, so it vouches for
// nothing, and a private nested class counts too (the check of the statement
// of foundation/core/Choice.h).  So the walk of the doors refuses the class,
// and the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class NestedMakerSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit NestedMakerSlot(std::uint32_t raw) noexcept : raw_{raw} {}

    struct Maker {
        [[nodiscard]] constexpr NestedMakerSlot make() const noexcept { return NestedMakerSlot{0xFFFFFFFFu}; }
    };

public:
    [[nodiscard]] static constexpr Maker maker() noexcept { return Maker{}; }

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr NestedMakerSlot
    niche_empty(::foundation::core::niche_key<NestedMakerSlot> const&) noexcept {
        return NestedMakerSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<NestedMakerSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() {
    return ::foundation::core::Option<NestedMakerSlot>::some(NestedMakerSlot::maker().make()).is_some() ? 0 : 1;
}
