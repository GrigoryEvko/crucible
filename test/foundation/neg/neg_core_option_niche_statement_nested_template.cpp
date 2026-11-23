// A class outside the base states that no door builds its empty value.  A
// public member class template has a static function that builds the slot
// from the empty word through the private constructor.  The walk cannot read
// the members of a class template before an instance, so a member class
// template is refused (the check of the statement of
// foundation/core/Choice.h).  So the walk of the doors refuses the class, and
// the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class NestedTemplateSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit NestedTemplateSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    template <class Unused>
    struct Door {
        [[nodiscard]] static constexpr NestedTemplateSlot empty_value() noexcept {
            return NestedTemplateSlot{0xFFFFFFFFu};
        }
    };

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr NestedTemplateSlot
    niche_empty(::foundation::core::niche_key<NestedTemplateSlot> const&) noexcept {
        return NestedTemplateSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool
    is_niche_empty(::foundation::core::niche_key<NestedTemplateSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() {
    return ::foundation::core::Option<NestedTemplateSlot>::some(NestedTemplateSlot::Door<int>::empty_value()).is_some()
             ? 0
             : 1;
}
