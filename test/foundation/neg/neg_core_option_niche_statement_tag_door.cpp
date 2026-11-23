// A class outside the base states that no door builds its empty value.  Its
// public constructor takes a public empty tag and no voucher.  The tag
// carries nothing, so the body of the constructor picks a constant, and here
// the constant is the empty word, as for a door from nothing.  Each
// constructor other than copy and move takes a voucher (the check of the
// statement of foundation/core/Choice.h), so the walk of the doors refuses
// the class, and the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class TagDoorSlot {
    std::uint32_t raw_ = 0;

public:
    struct Tag {};
    constexpr explicit TagDoorSlot(Tag /*tag*/) noexcept : raw_{0xFFFFFFFFu} {}

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr TagDoorSlot niche_empty(::foundation::core::niche_key<TagDoorSlot> const&) noexcept {
        return TagDoorSlot{Tag{}};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<TagDoorSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() { return ::foundation::core::Option<TagDoorSlot>::some(TagDoorSlot{TagDoorSlot::Tag{}}).is_some() ? 0 : 1; }
