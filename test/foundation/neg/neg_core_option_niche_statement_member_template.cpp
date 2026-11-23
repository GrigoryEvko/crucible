// A class states that no door builds its empty value, and a public member
// function template writes the raw word.  GCC 16 reflection gives no const
// qualifier of a member function template, so the walk of the members
// refuses each public member function template other than a constructor
// template (the check of the statement of foundation/core/Choice.h), and the
// Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

// The one door of the slot builds the first slot with a key, so it passes
// the walk of the doors.
class MemberTemplateSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit MemberTemplateSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    [[nodiscard]] static constexpr MemberTemplateSlot
    first(::foundation::core::niche_key<MemberTemplateSlot> const&) noexcept {
        return MemberTemplateSlot{1u};
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

int main() { return ::foundation::core::Option<MemberTemplateSlot>{}.is_some() ? 1 : 0; }
