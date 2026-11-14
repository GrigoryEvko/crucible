// The compile-time checks of fixy/session/Flow.h.

#include <fixy/session/Flow.h>

#include <foundation/effects/Row.h>

#include <type_traits>

namespace fixy::session::detail::flow_checks {

struct FlowRegion {
    using permission_row = ::foundation::effects::Row<>;
};

// A marker that carries a Permission moves, and no assignment writes over
// the token of a live marker.  The move is user-provided, so the marker is
// not trivially copyable, and it keeps the shape of a linear class.
static_assert(std::is_nothrow_move_constructible_v<Transferable<int, FlowRegion>>);
static_assert(!std::is_move_assignable_v<Transferable<int, FlowRegion>>);
static_assert(!std::is_trivially_copyable_v<Transferable<int, FlowRegion>>);
static_assert(std::is_nothrow_move_constructible_v<Returned<int, FlowRegion>>);
static_assert(!std::is_move_assignable_v<Returned<int, FlowRegion>>);
static_assert(!std::is_trivially_copyable_v<Returned<int, FlowRegion>>);

}  // namespace fixy::session::detail::flow_checks
