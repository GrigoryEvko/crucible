// A niche outside the base brings the statement of the niche of a Ref into
// its own scope with a using-declaration.  The statement is no member that
// the niche declares, and the check reads only a statement whose parent is
// the niche or the class (the check of the statement of
// foundation/core/Choice.h), so the Option of the class does not compile.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

#include <cstdint>

struct UsingBrand {};

// The one door of the slot builds the first slot with a key, so it passes
// the walk of the doors.
class UsingSlot {
    std::uint32_t raw_ = 0;
    constexpr explicit UsingSlot(std::uint32_t raw) noexcept : raw_{raw} {}
    friend struct ::foundation::core::niche<UsingSlot>;

public:
    [[nodiscard]] static constexpr UsingSlot first(::foundation::core::niche_key<UsingSlot> const&) noexcept {
        return UsingSlot{1u};
    }
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return raw_; }
};

template <>
struct foundation::core::niche<UsingSlot> : foundation::core::niche<foundation::core::Ref<int, UsingBrand>> {
    using foundation::core::niche<foundation::core::Ref<int, UsingBrand>>::is_empty_never_built;
    [[nodiscard]] static constexpr UsingSlot empty(niche_key<UsingSlot> const&) noexcept { return UsingSlot{0}; }
    [[nodiscard]] static constexpr bool is_empty(niche_key<UsingSlot> const&, UsingSlot const& slot) noexcept {
        return slot.raw() == 0;
    }
};

int main() { return ::foundation::core::Option<UsingSlot>{}.is_some() ? 1 : 0; }
