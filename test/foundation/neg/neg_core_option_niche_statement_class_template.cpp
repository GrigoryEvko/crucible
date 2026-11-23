// A class template outside the base states that no door builds its empty
// value, and its members pass the two walks.  The gate first forms the
// template while foundation/core/Choice.h evaluates the check, and the mark
// of the statement still reads the file of the class template, which is
// outside the base (the check of the statement of foundation/core/Choice.h).
// So the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

// The one door of the slot builds the first slot with a key, so it passes
// the walk of the doors.
template <std::uint32_t Spare>
class TemplateSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit TemplateSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    [[nodiscard]] static constexpr TemplateSlot first(::foundation::core::niche_key<TemplateSlot> const&) noexcept {
        return TemplateSlot{Spare + 1u};
    }

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr TemplateSlot
    niche_empty(::foundation::core::niche_key<TemplateSlot> const&) noexcept {
        return TemplateSlot{Spare};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<TemplateSlot> const&) const noexcept {
        return raw_ == Spare;
    }
};

int main() { return ::foundation::core::Option<TemplateSlot<0>>{}.is_some() ? 1 : 0; }
