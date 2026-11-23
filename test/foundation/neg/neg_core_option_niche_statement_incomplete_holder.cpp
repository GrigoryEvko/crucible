// A class outside the base states that no door builds its empty value.  A
// public static door gives a class that has no definition where the payload
// gate reads the door.  The walk cannot read the fields of that class, so it
// fails closed: the class IncompleteCheckedClass of foundation/core/Choice.h
// stops the compile, and the diagnostic names the incomplete class.

#include <foundation/core/Choice.h>

#include <cstdint>

struct LaterBox;

class IncompleteHolderSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit IncompleteHolderSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    [[nodiscard]] static LaterBox boxed(std::uint32_t raw) noexcept;

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr IncompleteHolderSlot
    niche_empty(::foundation::core::niche_key<IncompleteHolderSlot> const&) noexcept {
        return IncompleteHolderSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool
    is_niche_empty(::foundation::core::niche_key<IncompleteHolderSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() { return ::foundation::core::Option<IncompleteHolderSlot>{}.is_some() ? 1 : 0; }
