// The compile-time checks of crucible/canopy/Scuttlebutt.h.

#include <crucible/canopy/Scuttlebutt.h>

namespace crucible::canopy {

static_assert(!std::is_copy_constructible_v<ScuttlebuttSync<4, 4>>);
static_assert(!std::is_move_constructible_v<ScuttlebuttSync<4, 4>>);

}  // namespace crucible::canopy
