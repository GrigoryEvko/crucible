// A niche outside the base takes the statement of the niche of a Ref by
// inheritance, so its own class states nothing.  The check reads only an own
// member of the niche or of the class (THE CHECK OF THE STATEMENT of
// foundation/core/Choice.h), so the Option of the class does not compile.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

#include <cstdint>

struct InheritBrand {};

class InheritedSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit InheritedSlot(std::uint32_t raw) noexcept : raw_{raw} {}
    friend struct ::foundation::core::niche<InheritedSlot>;

public:
    [[nodiscard]] static constexpr InheritedSlot of(std::uint32_t raw) noexcept { return InheritedSlot{raw}; }
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return raw_; }
};

template <>
struct foundation::core::niche<InheritedSlot> : foundation::core::niche<foundation::core::Ref<int, InheritBrand>> {
    [[nodiscard]] static constexpr InheritedSlot empty(niche_key<InheritedSlot> const&) noexcept {
        return InheritedSlot{0};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<InheritedSlot> const&, InheritedSlot const& slot) noexcept {
        return slot.raw() == 0;
    }
};

int main() { return ::foundation::core::Option<InheritedSlot>::some(InheritedSlot::of(0)).is_some() ? 0 : 1; }
