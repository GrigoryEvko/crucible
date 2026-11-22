// A class outside the base states that no door builds its empty value.  Its
// public constructor template takes two values, and a reflection reads no
// parameter of a constructor template, so TemplateDoorSlot{0u, 0u} builds the empty
// value.  The statement is open to a type of the base only (THE CHECK OF THE
// STATEMENT of foundation/core/Choice.h), so the Option of the class does not
// compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class TemplateDoorSlot {
    std::uint32_t raw_ = 0;
    struct EmptyTag {};
    constexpr explicit TemplateDoorSlot(EmptyTag /*tag*/) noexcept {}

public:
    template <class Word, class Spare>
    constexpr TemplateDoorSlot(Word word, Spare /*spare*/) noexcept : raw_{word} {}

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr TemplateDoorSlot
    niche_empty(::foundation::core::niche_key<TemplateDoorSlot> const&) noexcept {
        return TemplateDoorSlot{EmptyTag{}};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<TemplateDoorSlot> const&) const noexcept {
        return raw_ == 0;
    }
};

int main() { return ::foundation::core::Option<TemplateDoorSlot>::some(TemplateDoorSlot{0u, 0u}).is_some() ? 0 : 1; }
