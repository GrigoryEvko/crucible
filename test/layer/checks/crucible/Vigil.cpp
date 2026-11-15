// The compile-time checks of crucible/Vigil.h.

#include <crucible/Vigil.h>

namespace crucible {

// A scoped view must not outlive the scope that minted it, so no field of
// Vigil, transitively, is allowed to be one.
static_assert(::fixy::no_scoped_view_field_check<Vigil>());

// The region callback of a Vigil binds the address of the Vigil, and bg_
// borrows that callback.  A copy or a move would leave the two behind, so a
// Vigil has no copy and no move.
static_assert(!__is_constructible(Vigil, Vigil const&) && !__is_constructible(Vigil, Vigil&&));
static_assert(!__is_assignable(Vigil&, Vigil const&) && !__is_assignable(Vigil&, Vigil&&));

}  // namespace crucible
