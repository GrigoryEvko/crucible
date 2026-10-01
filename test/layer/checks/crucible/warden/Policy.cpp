// The compile-time checks of crucible/warden/Policy.h.

#include <crucible/warden/Policy.h>

namespace crucible::warden {

// The whole structure travels by value. A field that pushes it past
// this bound probably belongs in a configuration of its own.
static_assert(sizeof(Policy) < 256);

}  // namespace crucible::warden
