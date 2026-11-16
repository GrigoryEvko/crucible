// The frame door of an Option of a class of the member niche protocol that
// does not state that no door builds its empty value.  The aggregate builds
// the empty value, and a write through the RefMut could write it, so a full
// Option would read as empty.  The door refuses the payload.

#include <foundation/core/Choice.h>

namespace {

struct Slot {
    unsigned raw = 0;
    [[nodiscard]] static constexpr Slot niche_empty(::foundation::core::niche_key<Slot> const&) noexcept {
        return Slot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<Slot> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

}  // namespace

int main() {
    ::foundation::core::Option<Slot> option = ::foundation::core::Option<Slot>::some(Slot{3});
    option.with_mut([](auto slot) noexcept { slot->raw = 4; }, [] noexcept {});
    return 0;
}
