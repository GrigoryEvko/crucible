// The compile-time checks of fixy/session/Flow.h.

#include <fixy/session/Flow.h>

#include <foundation/Linear.h>
#include <foundation/effects/Row.h>

#include <type_traits>
#include <utility>

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

// A hold keeps each token on the erased identity of its tag, and a share of
// a pool and its view can carry the erased identity too.
using ErasedBrand = ::foundation::brand::DefaultBrand;

// A take gives the token and the hold of the rest of the set in a Record.
// The end of a hold gives each token in a Record in the order of the set,
// and the end of an empty hold gives an empty Record.
struct SecondRegion {
    using permission_row = ::foundation::effects::Row<>;
};
using TwoRegions = ::foundation::permissions::PermSet<FlowRegion, SecondRegion>;
using FlowToken = ::foundation::permissions::Permission<FlowRegion, ErasedBrand>;
using SecondToken = ::foundation::permissions::Permission<SecondRegion, ErasedBrand>;
static_assert(
    std::is_same_v<decltype(std::declval<PermHold<TwoRegions>>().take<FlowRegion>()),
                   ::foundation::core::Record<FlowToken, PermHold<::foundation::permissions::PermSet<SecondRegion>>>>);
static_assert(std::is_same_v<decltype(std::declval<PermHold<TwoRegions>>().into_permissions()),
                             ::foundation::core::Record<FlowToken, SecondToken>>);
static_assert(
    std::is_same_v<decltype(std::declval<PermHold<::foundation::permissions::EmptyPermSet>>().into_permissions()),
                   ::foundation::core::Record<>>);

// A read through a borrowed loan gives the value and the hold back in a
// Record, and a read of a share gives the value and the reader back.
struct FlowCount {
    int count = 0;
};
using FlowView = ::foundation::permissions::ReadView<FlowRegion, ErasedBrand>;
using FlowReader = SharedReader<FlowRegion, ErasedBrand>;
using Borrowing = PermHold<::foundation::permissions::PermSet<BorrowedIn<FlowRegion>>>;
static_assert(std::is_same_v<decltype(std::declval<Borrowing>().read<FlowRegion>([](FlowView const&) noexcept {
                                 return FlowCount{};
                             })),
                             ::foundation::core::Record<FlowCount, Borrowing>>);
static_assert(
    std::is_same_v<decltype(std::declval<FlowReader>().read([](FlowView const&) noexcept { return FlowCount{}; })),
                   ::foundation::core::Record<FlowCount, FlowReader>>);

}  // namespace fixy::session::detail::flow_checks
