// A class states that no door builds its empty value, and its public
// constructor takes a plain class that holds the raw word.  The parameter is
// a class and not a raw value, but it holds a raw leaf, so RawWord{0} builds
// the empty value.  The walk of the constructors counts such a class as a raw
// value (THE CHECK OF THE STATEMENT of foundation/core/Choice.h), so the
// Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

struct RawWord {
    std::uint32_t value = 0;
};

class WrappedRawSlot {
    std::uint32_t raw_ = 0;
    struct EmptyTag {};
    constexpr explicit WrappedRawSlot(EmptyTag /*tag*/) noexcept {}

public:
    constexpr explicit WrappedRawSlot(RawWord word) noexcept : raw_{word.value} {}

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr WrappedRawSlot
    niche_empty(::foundation::core::niche_key<WrappedRawSlot> const&) noexcept {
        return WrappedRawSlot{EmptyTag{}};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<WrappedRawSlot> const&) const noexcept {
        return raw_ == 0;
    }
};

int main() { return ::foundation::core::Option<WrappedRawSlot>::some(WrappedRawSlot{RawWord{0}}).is_some() ? 0 : 1; }
