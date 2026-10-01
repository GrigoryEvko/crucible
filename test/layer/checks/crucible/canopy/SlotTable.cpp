// The compile-time checks of crucible/canopy/SlotTable.h.

#include <crucible/canopy/SlotTable.h>

namespace crucible::canopy {

static_assert(sizeof(SlotCount<4>) == sizeof(std::uint16_t));
static_assert(!std::is_assignable_v<SlotCount<4>&, std::uint16_t>, "a count past the bound must stay unrepresentable");
static_assert(!std::is_constructible_v<SlotCount<4>, std::uint16_t>,
              "a count past the bound must stay unrepresentable");

}  // namespace crucible::canopy
