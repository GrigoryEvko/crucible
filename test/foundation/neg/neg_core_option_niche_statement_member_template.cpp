// A class states that no door builds its empty value, and a public member
// function template writes the raw word.  GCC 16 reflection gives no const
// qualifier of a member function template, so the walk of the members
// refuses each public member function template other than a constructor
// template (THE CHECK OF THE STATEMENT of foundation/core/Choice.h), and the
// Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class MemberTemplateSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit MemberTemplateSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    [[nodiscard]] static constexpr MemberTemplateSlot of_byte(std::uint8_t byte) noexcept {
        return MemberTemplateSlot{std::uint32_t{byte} + 1u};
    }
    template <std::uint32_t Raw>
    constexpr void set_word() noexcept {
        raw_ = Raw;
    }

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr MemberTemplateSlot
    niche_empty(::foundation::core::niche_key<MemberTemplateSlot> const&) noexcept {
        return MemberTemplateSlot{0u};
    }
    [[nodiscard]] constexpr bool
    is_niche_empty(::foundation::core::niche_key<MemberTemplateSlot> const&) const noexcept {
        return raw_ == 0;
    }
};

int main() {
    MemberTemplateSlot local = MemberTemplateSlot::of_byte(3);
    local.set_word<0>();
    return ::foundation::core::Option<MemberTemplateSlot>::some(local).is_some() ? 0 : 1;
}
