// The compile-time checks of crucible/canopy/Swim.h.

#include <crucible/canopy/Swim.h>

namespace crucible::canopy {

static_assert(!std::is_default_constructible_v<SwimMembership<8>>);
static_assert(!std::is_copy_constructible_v<SwimMembership<8>>);
static_assert(!std::is_move_constructible_v<SwimMembership<8>>);

}  // namespace crucible::canopy
