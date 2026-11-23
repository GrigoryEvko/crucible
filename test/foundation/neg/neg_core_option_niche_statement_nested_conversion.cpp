// A class outside the base states that no door builds its empty value.  A
// public nested class has a conversion function to the slot that builds the
// empty word through the private constructor.  A conversion function of a
// nested class gives the slot, so it takes a voucher (the check of the
// statement of foundation/core/Choice.h).  So the walk of the doors refuses
// the class, and the Option of the class does not compile.

#include <foundation/core/Choice.h>

#include <cstdint>

class NestedConversionSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit NestedConversionSlot(std::uint32_t raw) noexcept : raw_{raw} {}

public:
    struct Converter {
        constexpr operator NestedConversionSlot() const noexcept { return NestedConversionSlot{0xFFFFFFFFu}; }
    };

    static constexpr ::foundation::core::niche_statement is_niche_empty_never_built{};
    [[nodiscard]] static constexpr NestedConversionSlot
    niche_empty(::foundation::core::niche_key<NestedConversionSlot> const&) noexcept {
        return NestedConversionSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool
    is_niche_empty(::foundation::core::niche_key<NestedConversionSlot> const&) const noexcept {
        return raw_ == 0xFFFFFFFFu;
    }
};

int main() {
    NestedConversionSlot const converted = NestedConversionSlot::Converter{};
    return ::foundation::core::Option<NestedConversionSlot>::some(converted).is_some() ? 0 : 1;
}
