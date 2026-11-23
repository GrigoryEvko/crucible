// A class outside the base states that no door builds its empty value.  Its
// constructor is private, and a public static data member holds a value that
// the private constructor built from the empty word.  A copy of the member
// is a value that no door of the walk gives.  Each public static data member
// of a stated type that holds a value of the type is a door with no
// parameter (the check of the statement of foundation/core/Choice.h).  So
// the walk of the doors refuses the class, and the full Option of the empty
// value does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class StaticValueSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit StaticValueSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    static StaticValueSlot const empty_value;

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr StaticValueSlot
    niche_empty(::foundation::core::niche_key<StaticValueSlot> const&) noexcept {
        return StaticValueSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<StaticValueSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};
inline constexpr StaticValueSlot StaticValueSlot::empty_value{0xFFFFFFFFu};

int main() { return ::foundation::core::Option<StaticValueSlot>::some(StaticValueSlot::empty_value).is_some() ? 0 : 1; }
