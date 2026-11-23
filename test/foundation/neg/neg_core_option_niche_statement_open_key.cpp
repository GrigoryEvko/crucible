// A class template outside the base states that no door builds its empty
// value.  Its public constructor takes a raw word and a key of its template
// argument.  The key is a specialization of niche, the template with the
// open mark, in the file of its argument: a class of the namespace
// foundation::core with no public constructor, which a friend of this file
// builds.  The key also copies the mark of a key of the base, and that mark
// reads this file.  A key vouches for a raw value only when its mark stands
// in a header of the base (the check of the statement of
// foundation/core/Choice.h).  So the walk of the constructors refuses the
// class, and the Option of the empty value does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

struct OpenTag;

template <>
struct foundation::core::niche<OpenTag> {
private:
    constexpr niche() noexcept {}
    constexpr niche(niche const&) noexcept {}
    friend struct ::OpenTag;

public:
    niche& operator=(niche const&) = delete("a key lives for one construction");
    static constexpr ::foundation::core::niche_statement is_niche_voucher{};
};

struct OpenTag {
    [[nodiscard]] static constexpr ::foundation::core::niche<OpenTag> mint_key() noexcept { return {}; }
};

template <class Key>
class KeyedSlot {
    std::uint32_t raw_ = 0;

public:
    constexpr KeyedSlot(std::uint32_t raw, Key const& /*key*/) noexcept : raw_{raw} {}

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr KeyedSlot niche_empty(::foundation::core::niche_key<KeyedSlot> const&) noexcept {
        return KeyedSlot{0xFFFFFFFFu, OpenTag::mint_key()};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<KeyedSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

using OpenSlot = KeyedSlot<::foundation::core::niche<OpenTag>>;

int main() {
    return ::foundation::core::Option<OpenSlot>::some(OpenSlot{0xFFFFFFFFu, OpenTag::mint_key()}).is_some() ? 0 : 1;
}
