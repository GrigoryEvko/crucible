// A class outside the base states that no door builds its empty value.  A
// public nested class has no definition where the payload gate reads the
// class, and a later definition can give it a door that builds the empty
// word.  The walk cannot read the members of that class, so it fails closed:
// the class IncompleteCheckedClass of foundation/core/Choice.h stops the
// compile, and the diagnostic names the nested class.

#include <foundation/core/Choice.h>

#include <cstdint>

class NestedLaterSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit NestedLaterSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Later;

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr NestedLaterSlot
    niche_empty(::foundation::core::niche_key<NestedLaterSlot> const&) noexcept {
        return NestedLaterSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<NestedLaterSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() { return ::foundation::core::Option<NestedLaterSlot>{}.is_some() ? 1 : 0; }
