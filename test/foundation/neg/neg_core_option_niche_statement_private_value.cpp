// A class outside the base states that no door builds its empty value.  A
// private static data member holds a value that the private constructor
// built from the empty word, and a friend gives a copy of it.  A friend or a
// nested class has the access of the class, so a static data member of each
// access that holds a value of a stated type is a door with no parameter
// (the check of the statement of foundation/core/Choice.h).  So the walk of
// the doors refuses the class, and the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class PrivateValueSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit PrivateValueSlot(std::uint32_t raw) noexcept : raw_{raw} {}
    static PrivateValueSlot const empty_value_;

    friend constexpr PrivateValueSlot copy_of_empty() noexcept;

public:
    constexpr PrivateValueSlot(PrivateValueSlot const&) noexcept = default;

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr PrivateValueSlot
    niche_empty(::foundation::core::niche_key<PrivateValueSlot> const&) noexcept {
        return PrivateValueSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<PrivateValueSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

inline constexpr PrivateValueSlot PrivateValueSlot::empty_value_{0xFFFFFFFFu};

[[nodiscard]] constexpr PrivateValueSlot copy_of_empty() noexcept { return PrivateValueSlot::empty_value_; }

int main() { return ::foundation::core::Option<PrivateValueSlot>::some(copy_of_empty()).is_some() ? 0 : 1; }
