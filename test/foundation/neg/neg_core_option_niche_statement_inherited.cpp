// A niche outside the base takes the statement of the niche of a Ref by
// inheritance, so its own class states nothing.  The check reads only an own
// member of the niche or of the class (the check of the statement of
// foundation/core/Choice.h), so the Option of the class does not compile.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

#include <cstdint>

struct InheritBrand {};

// The one door of the slot builds the first slot with a key, so it passes
// the walk of the doors.
class InheritedSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit InheritedSlot(std::uint32_t raw) noexcept : raw_{raw} {}
    friend struct ::foundation::core::niche<InheritedSlot>;

public:
    [[nodiscard]] static constexpr InheritedSlot first(::foundation::core::niche_key<InheritedSlot> const&) noexcept {
        return InheritedSlot{1u};
    }
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

int main() { return ::foundation::core::Option<InheritedSlot>{}.is_some() ? 1 : 0; }
