// The compile-time checks of crucible/canopy/Lifeguard.h.

#include <crucible/canopy/Lifeguard.h>

namespace crucible::canopy {

static_assert(!std::is_constructible_v<LifeguardSwim<4, 8, 4, 8>, SwimPeer>);
static_assert(!std::is_copy_constructible_v<LifeguardSwim<4, 8, 4, 8>>);
static_assert(!std::is_move_constructible_v<LifeguardSwim<4, 8, 4, 8>>);

}  // namespace crucible::canopy
