// A class outside the base states that no door builds its empty value.  Its
// public constructor takes a raw word and a key with no public constructor,
// and the key is a class of this file.  The author of the key builds one in a
// door of the key, and that door can give the raw word of the empty value.
// A key vouches for a raw value only when a header of the base defines it
// (the check of the statement of foundation/core/Choice.h).  So the walk of
// the constructors refuses the class, and the Option of the class does not
// compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class OutsideKey final {
    constexpr OutsideKey() noexcept {}

public:
    [[nodiscard]] static constexpr OutsideKey mint() noexcept { return OutsideKey{}; }
};

class KeyedSlot {
    std::uint32_t raw_ = 0;
    struct EmptyTag {};
    constexpr explicit KeyedSlot(EmptyTag /*tag*/) noexcept {}

public:
    constexpr KeyedSlot(std::uint32_t raw, OutsideKey const& /*key*/) noexcept : raw_{raw} {}

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr KeyedSlot niche_empty(::foundation::core::niche_key<KeyedSlot> const&) noexcept {
        return KeyedSlot{EmptyTag{}};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<KeyedSlot> const&) const noexcept {
        return raw_ == 0;
    }
};

int main() { return ::foundation::core::Option<KeyedSlot>::some(KeyedSlot{0u, OutsideKey::mint()}).is_some() ? 0 : 1; }
