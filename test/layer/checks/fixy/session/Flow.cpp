// The compile-time checks of fixy/session/Flow.h.

#include <fixy/session/Flow.h>

#include <foundation/Linear.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace fixy::session::detail::flow_checks {

struct FlowRegion {
    using permission_row = ::foundation::effects::Row<>;
};

// A marker that carries a Permission moves, and no assignment writes over
// the token of a live marker.  The move and the destructor are trivial for
// a payload with a trivial move, so the Itanium ABI passes the marker in
// registers.  The move assignment of the token is user-provided, so GCC 16
// counts the marker as not trivially copyable, and the marker keeps the shape
// of a linear class.  A compiler that counts it as trivially copyable fails
// the build here.
static_assert(std::is_nothrow_move_constructible_v<Transferable<int, FlowRegion>>);
static_assert(std::is_trivially_move_constructible_v<Transferable<int, FlowRegion>>);
static_assert(std::is_trivially_destructible_v<Transferable<int, FlowRegion>>);
static_assert(!std::is_move_assignable_v<Transferable<int, FlowRegion>>);
static_assert(!std::is_trivially_copyable_v<Transferable<int, FlowRegion>>);
static_assert(::foundation::linear::is_linear_v<Transferable<int, FlowRegion>>);
static_assert(std::is_nothrow_move_constructible_v<Returned<int, FlowRegion>>);
static_assert(std::is_trivially_move_constructible_v<Returned<int, FlowRegion>>);
static_assert(std::is_trivially_destructible_v<Returned<int, FlowRegion>>);
static_assert(!std::is_move_assignable_v<Returned<int, FlowRegion>>);
static_assert(!std::is_trivially_copyable_v<Returned<int, FlowRegion>>);
static_assert(::foundation::linear::is_linear_v<Returned<int, FlowRegion>>);

// A marker that carries a read loan moves, and no assignment writes over the
// loan of a live marker.  The move stays trivial, so the ABI passes the
// marker in registers.  The move assignment of the loan is user-provided, so
// GCC 16 counts the marker as not trivially copyable, and the marker keeps
// the shape of a linear class.  A compiler that counts it as trivially
// copyable fails the build here.
static_assert(std::is_trivially_move_constructible_v<Borrowed<int, FlowRegion>>);
static_assert(!std::is_move_assignable_v<Borrowed<int, FlowRegion>>);
static_assert(!std::is_trivially_copyable_v<Borrowed<int, FlowRegion>>);
static_assert(::foundation::linear::is_linear_v<Borrowed<int, FlowRegion>>);
static_assert(std::is_trivially_move_constructible_v<Released<int, FlowRegion>>);
static_assert(!std::is_move_assignable_v<Released<int, FlowRegion>>);
static_assert(!std::is_trivially_copyable_v<Released<int, FlowRegion>>);
static_assert(::foundation::linear::is_linear_v<Released<int, FlowRegion>>);

}  // namespace fixy::session::detail::flow_checks
