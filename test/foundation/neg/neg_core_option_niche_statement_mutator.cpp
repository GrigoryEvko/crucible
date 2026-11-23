// A class states that no door builds its empty value.  Its one door never
// builds the empty value, but a public member function writes the raw word.
// The frame door with_mut gives the payload in place, and a local can change
// before some(), so a full Option would read as empty.  The walk of the
// members refuses a public member function that is not const (the check of
// the statement of foundation/core/Choice.h), so the Option of the class does
// not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

// The tag of the one door of the slot, which builds the first slot.
struct FirstSlot {};

class MutatorSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit MutatorSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    [[nodiscard]] static constexpr MutatorSlot first(FirstSlot /*tag*/) noexcept { return MutatorSlot{1u}; }
    constexpr void set_word(std::uint32_t raw) noexcept { raw_ = raw; }

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr MutatorSlot niche_empty(::foundation::core::niche_key<MutatorSlot> const&) noexcept {
        return MutatorSlot{0u};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<MutatorSlot> const&) const noexcept {
        return raw_ == 0;
    }
};

int main() {
    MutatorSlot local = MutatorSlot::first(FirstSlot{});
    local.set_word(0);
    return ::foundation::core::Option<MutatorSlot>::some(local).is_some() ? 0 : 1;
}
