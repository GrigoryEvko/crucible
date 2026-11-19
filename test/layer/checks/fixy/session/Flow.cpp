// The compile-time checks of fixy/session/Flow.h.

#include <fixy/session/Flow.h>

// The header of the hand-off, which the delivery walk enters.
#include <fixy/session/Delegate.h>

#include <foundation/Linear.h>
#include <foundation/effects/Row.h>

#include <type_traits>
#include <utility>

namespace fixy::session::detail::flow_checks {

struct FlowRegion {
    using permission_row = ::foundation::effects::Row<>;
};

// ── The delivery walk ────────────────────────────────────────────────
//
// The regions come in the order of a walk that reads a node first, then the
// protocol of each endpoint that the node receives, then its continuation,
// then each branch.  The walk stops at its first refusal in that order.
template <int Place>
struct OrderedRegion {
    using permission_row = ::foundation::effects::Row<>;
};

struct FlowResource {
    static constexpr Network session_network = Network::Local;
    [[no_unique_address]] MoveOnlyResource move_only{};
};

using FlowPolicy = DefaultAbandonmentPolicy;
using NoRegions = ::foundation::permissions::EmptyPermSet;

using ReceivesInOrder =
    Recv<Transferable<int, OrderedRegion<1>>,
         Recv<DelegatedSession<Recv<Transferable<int, OrderedRegion<3>>, End>, FlowResource, FlowPolicy,
                               ::foundation::permissions::PermSet<OrderedRegion<2>>>,
              Offer<Recv<Transferable<int, OrderedRegion<4>>, End>, Recv<Transferable<int, OrderedRegion<5>>, End>>>>;
static_assert(std::is_same_v<protocol_delivered_regions_t<ReceivesInOrder>,
                             ::foundation::permissions::PermSet<OrderedRegion<1>, OrderedRegion<2>, OrderedRegion<3>,
                                                                OrderedRegion<4>, OrderedRegion<5>>>);

// A received endpoint whose protocol comes back to the named state of the
// receiver adds its regions one time, and the walk ends.
struct ComesBack {
    using protocol =
        Recv<Transferable<int, OrderedRegion<1>>,
             Recv<DelegatedSession<Send<int, Named<ComesBack>>, FlowResource, FlowPolicy, NoRegions>, End>>;
};
static_assert(std::is_same_v<protocol_delivered_regions_t<Named<ComesBack>>,
                             ::foundation::permissions::PermSet<OrderedRegion<1>>>);

// A node of the protocol of a received endpoint comes before the
// continuation of the receive, so its refusal is the first one.
struct UnknownInEndpoint {};
struct UnknownAfterEndpoint {};
using RefusedTwice =
    Recv<DelegatedSession<Recv<int, UnknownInEndpoint>, FlowResource, FlowPolicy, NoRegions>, UnknownAfterEndpoint>;
static_assert(delivery_facts_of(^^RefusedTwice).verdict.unregistered_node == ^^UnknownInEndpoint);

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

// A pack, a lend and a release give the message and the hold of the next set
// in a Record.  The move of each marker is noexcept, so a payload whose move
// can throw gives a Record too.
struct MovesWithThrow {
    int count = 0;

    MovesWithThrow() noexcept = default;
    MovesWithThrow(MovesWithThrow&& other) noexcept(false) : count{other.count} {}
};
static_assert(!std::is_nothrow_move_constructible_v<MovesWithThrow>);
using Lending = PermHold<::foundation::permissions::PermSet<FlowRegion>>;
template <class T>
using Packed =
    ::foundation::core::Record<Transferable<T, FlowRegion>, PermHold<::foundation::permissions::PermSet<SecondRegion>>>;
template <class T>
using Lent = ::foundation::core::Record<Borrowed<T, FlowRegion>,
                                        PermHold<perm_set_after_send_t<Lending::perm_set, Borrowed<T, FlowRegion>>>>;
template <class T>
using Given = ::foundation::core::Record<Released<T, FlowRegion>,
                                         PermHold<perm_set_after_send_t<Borrowing::perm_set, Released<T, FlowRegion>>>>;
static_assert(std::is_same_v<decltype(std::declval<PermHold<TwoRegions>>().pack<FlowRegion>(0)), Packed<int>>);
static_assert(std::is_same_v<decltype(std::declval<Lending>().lend<FlowRegion>(0)), Lent<int>>);
static_assert(std::is_same_v<decltype(std::declval<Borrowing>().release<FlowRegion>(0)), Given<int>>);
static_assert(std::is_same_v<decltype(std::declval<PermHold<TwoRegions>>().pack<FlowRegion>(MovesWithThrow{})),
                             Packed<MovesWithThrow>>);
static_assert(
    std::is_same_v<decltype(std::declval<Lending>().lend<FlowRegion>(MovesWithThrow{})), Lent<MovesWithThrow>>);
static_assert(
    std::is_same_v<decltype(std::declval<Borrowing>().release<FlowRegion>(MovesWithThrow{})), Given<MovesWithThrow>>);

}  // namespace fixy::session::detail::flow_checks
