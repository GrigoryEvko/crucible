// The compile-time checks of crucible/Vigil.h.

#include <crucible/Vigil.h>

namespace crucible {

// A scoped view must not outlive the scope that minted it, so no field of
// Vigil, transitively, is allowed to be one.
static_assert(::fixy::no_scoped_view_field_check<Vigil>());

}  // namespace crucible
