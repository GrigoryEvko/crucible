// A class outside the base states that no door builds its empty value.  A
// protected member function writes the raw word, and the class is not final.
// A class outside that derives from it can copy a slot into its base part and
// write the empty word there, and a copy of the derived value then gives that
// base part as a slot.  The walks judge each member that is not private (the
// check of the statement of foundation/core/Choice.h), so the walk of the
// members refuses the class, and the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class ProtectedWriterSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit ProtectedWriterSlot(std::uint32_t raw) noexcept : raw_{raw} {}

protected:
    constexpr void set_word(std::uint32_t raw) noexcept { raw_ = raw; }

public:
    [[nodiscard]] static constexpr ProtectedWriterSlot
    first(::foundation::core::niche_key<ProtectedWriterSlot> const&) noexcept {
        return ProtectedWriterSlot{1u};
    }

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr ProtectedWriterSlot
    niche_empty(::foundation::core::niche_key<ProtectedWriterSlot> const&) noexcept {
        return ProtectedWriterSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool
    is_niche_empty(::foundation::core::niche_key<ProtectedWriterSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() { return ::foundation::core::Option<ProtectedWriterSlot>{}.is_some() ? 1 : 0; }
