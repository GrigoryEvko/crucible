// The compile-time checks of crucible/canopy/HyParView.h.

#include <crucible/canopy/HyParView.h>

namespace crucible::canopy {

static_assert(!std::is_default_constructible_v<HyParViewMembership<4, 8>>);
static_assert(!std::is_copy_constructible_v<HyParViewMembership<4, 8>>);
static_assert(!std::is_move_constructible_v<HyParViewMembership<4, 8>>);

}  // namespace crucible::canopy
