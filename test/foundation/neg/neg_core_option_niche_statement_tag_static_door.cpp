// A class outside the base states that no door builds its empty value.  Its
// constructor is private, and a public static door over it takes a public
// empty tag and no voucher.  The tag carries nothing, so the door picks a
// constant, and here the constant is the empty word, as for a door from
// nothing.  Each static member function that gives a value of a stated type
// takes a voucher (the check of the statement of foundation/core/Choice.h),
// so the walk of the doors refuses the class, and the Option of the class
// does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class TagStaticSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit TagStaticSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Tag {};
    [[nodiscard]] static constexpr TagStaticSlot of(Tag /*tag*/) noexcept { return TagStaticSlot{0xFFFFFFFFu}; }

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr TagStaticSlot
    niche_empty(::foundation::core::niche_key<TagStaticSlot> const&) noexcept {
        return TagStaticSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<TagStaticSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() {
    return ::foundation::core::Option<TagStaticSlot>::some(TagStaticSlot::of(TagStaticSlot::Tag{})).is_some() ? 0 : 1;
}
