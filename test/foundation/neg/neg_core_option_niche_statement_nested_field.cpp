// A class outside the base states that no door builds its empty value.  A
// public nested class, defined after the class, has a field of the slot with
// an initializer that builds the empty word through the private constructor.
// A field of a nested class that holds the slot is refused, because its
// initializer or a constructor of the nested class builds the slot (the
// check of the statement of foundation/core/Choice.h).  So the walk of the
// doors refuses the class, and the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class NestedFieldSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit NestedFieldSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Holder;

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr NestedFieldSlot
    niche_empty(::foundation::core::niche_key<NestedFieldSlot> const&) noexcept {
        return NestedFieldSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<NestedFieldSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

struct NestedFieldSlot::Holder {
    NestedFieldSlot value{0xFFFFFFFFu};
};

int main() {
    return ::foundation::core::Option<NestedFieldSlot>::some(NestedFieldSlot::Holder{}.value).is_some() ? 0 : 1;
}
