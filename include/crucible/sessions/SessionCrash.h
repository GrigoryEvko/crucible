#pragma once

// Crash-stop extensions to the session-type framework.
//
// The failure model is crash-stop: a participant may halt abruptly, and
// surviving peers detect the halt and handle it through an explicit
// crash branch.  Links between live participants stay reliable, and
// Byzantine behaviour is out of scope.
//
// Three protocol-level consequences follow, and this header encodes each
// one as a type:
//
//   A crashed participant's local type becomes Stop.  Stop is terminal
//   and self-dual, and it is the bottom of the subtype order.
//
//   A queue whose recipient has crashed becomes UnavailableQueue, and
//   later sends into it are dropped rather than delivered.
//
//   A peer that receives from the crashed participant through an Offer
//   takes that Offer's Crash branch and runs the recovery continuation.

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/CrashLattice.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

using ::crucible::algebra::lattices::CrashClass;
using ::crucible::algebra::lattices::CrashLattice;

// A participant whose local type is Stop has crashed and carries no
// further protocol obligation.

template <CrashClass C>
struct Stop_g {
    static constexpr CrashClass crash_class = C;
};

using Stop = Stop_g<CrashClass::Abort>;

template <typename P>
struct is_stop : std::false_type {};

template <CrashClass C>
struct is_stop<Stop_g<C>> : std::true_type {};

template <typename P>
inline constexpr bool is_stop_v = is_stop<P>::value;

// A terminal state ends the session, so destroying a handle at Stop is
// a clean release rather than an abandoned protocol.

template <CrashClass C>
struct is_terminal_state<Stop_g<C>> : std::true_type {};

// The dual of a crashed endpoint is itself crashed, because a crash is
// observed identically from either side of the channel.

template <CrashClass C>
struct dual_of<Stop_g<C>> {
    using type = Stop_g<C>;
};

template <CrashClass C>
struct is_dual_involutive<Stop_g<C>> : std::true_type {};

// Sequencing Q onto Stop does not splice Q in, because a crashed
// endpoint never resumes.  Normal termination behaves the other way:
// compose<End, Q> advances into Q.

template <CrashClass C, typename Q>
struct compose<Stop_g<C>, Q> {
    using type = Stop_g<C>;
};

template <CrashClass C1, CrashClass C2>
struct compose<Stop_g<C1>, Stop_g<C2>> {
    static_assert(CrashLattice::leq(C2, C1),
                  "crucible::session::diagnostic "
                  "[CrashLattice_Composition_Incompatible]: "
                  "composing two crashed endpoints requires the second crash class to sit at or below the first in "
                  "the CrashLattice order.");
    using type = Stop_g<C1>;
};

template <CrashClass C, typename LoopCtx>
struct is_well_formed<Stop_g<C>, LoopCtx> : std::true_type {};

// Stop is the bottom of the subtype order: a crashed endpoint vacuously
// inhabits any protocol, because no peer will ever receive from it, so
// substituting Stop for any T is safe from the peer's side.
//
// The reverse direction is deliberately absent.  T is not a subtype of
// Stop for T other than Stop, since Stop is the bottom and not the top.

template <CrashClass C, typename U>
struct is_subtype_sync_structural<Stop_g<C>, U> : std::true_type {};

template <CrashClass C1, CrashClass C2>
struct is_subtype_sync_structural<Stop_g<C1>, Stop_g<C2>> : std::bool_constant<CrashLattice::leq(C1, C2)> {};

namespace detail::subtype {

template <CrashClass C, typename U>
struct protocol_grade_satisfies<Stop_g<C>, U> : std::true_type {};

template <CrashClass C1, CrashClass C2>
struct protocol_grade_satisfies<Stop_g<C1>, Stop_g<C2>> : std::bool_constant<CrashLattice::leq(C1, C2)> {};

}  // namespace detail::subtype

// The handle exposes no post-crash operation.  A caller that wants the
// channel back establishes a fresh session rather than advancing this
// one.

template <CrashClass C, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Stop_g<C>, Resource, LoopCtx>
    : public SessionHandleBase<Stop_g<C>, SessionHandle<Stop_g<C>, Resource, LoopCtx>> {
    Resource resource_;

    template <typename P, typename R, typename L>
    friend class SessionHandle;

    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Stop_g<C>, SessionHandle<Stop_g<C>, Resource, LoopCtx>>{loc}, resource_{std::move(r)} {}

public:
    using protocol = Stop_g<C>;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    static constexpr CrashClass crash_class = C;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    // Stop is terminal, so the inherited destructor accepts a handle
    // that is never closed.  close() is still how a caller reclaims the
    // Resource.
    [[nodiscard]] constexpr Resource close() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return std::move(resource_);
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

// Crash is a payload rather than a combinator, so it occupies a Recv
// message slot.  A crash branch of an Offer is any Recv<Crash<Peer>, K>,
// and K is the recovery continuation the survivor runs.

template <typename PeerTag>
struct Crash {
    using peer = PeerTag;
};

template <typename T>
struct is_crash : std::false_type {};

template <typename PeerTag>
struct is_crash<Crash<PeerTag>> : std::true_type {};

template <typename T>
inline constexpr bool is_crash_v = is_crash<T>::value;

// The queue into a crashed recipient becomes unavailable, and later
// sends into it are dropped instead of delivered.

template <typename PeerTag>
struct UnavailableQueue {
    using peer = PeerTag;
};

// Roles in the set are assumed not to crash inside the protocol's
// scope, so peers need no Crash branch for them.  A role outside the
// set must appear in a Crash branch of every peer that receives from
// it.

template <typename... Roles>
struct ReliableSet {
    static constexpr std::size_t size = sizeof...(Roles);
};

using UnreliableAll = ReliableSet<>;

template <typename R, typename RoleTag>
struct is_reliable;

template <typename... Roles, typename RoleTag>
struct is_reliable<ReliableSet<Roles...>, RoleTag> : std::bool_constant<(std::is_same_v<Roles, RoleTag> || ...)> {};

template <typename R, typename RoleTag>
inline constexpr bool is_reliable_v = is_reliable<R, RoleTag>::value;

// This trait inspects one Offer node.  The whole-tree form appears
// further down.

namespace detail::crash {

template <typename Branch, typename PeerTag>
struct is_crash_branch_for : std::false_type {};

template <typename PeerTag, typename K>
struct is_crash_branch_for<Recv<Crash<PeerTag>, K>, PeerTag> : std::true_type {};

}  // namespace detail::crash

template <typename OfferType, typename PeerTag>
struct has_crash_branch_for_peer : std::false_type {};

template <typename... Branches, typename PeerTag>
struct has_crash_branch_for_peer<Offer<Branches...>, PeerTag>
    : std::bool_constant<(detail::crash::is_crash_branch_for<Branches, PeerTag>::value || ...)> {};

// An Offer that names its sender only needs a crash branch for that
// sender.  A different peer is not the one driving this choice, so its
// crash cannot affect this reception, and the requirement is vacuous.
template <typename Role, typename... Branches, typename PeerTag>
struct has_crash_branch_for_peer<Offer<Sender<Role>, Branches...>, PeerTag>
    : std::bool_constant<!std::is_same_v<Role, PeerTag>
                         || (detail::crash::is_crash_branch_for<Branches, PeerTag>::value || ...)> {};

template <typename OfferType, typename PeerTag>
inline constexpr bool has_crash_branch_for_peer_v = has_crash_branch_for_peer<OfferType, PeerTag>::value;

// Calling this at the boundary reports the failure at the call site
// rather than deep inside a template instantiation.

template <typename OfferType, typename PeerTag>
consteval void assert_has_crash_branch_for() noexcept {
    static_assert(has_crash_branch_for_peer_v<OfferType, PeerTag>,
                  "crucible::session::diagnostic [CrashBranch_Missing]: "
                  "assert_has_crash_branch_for: OfferType lacks a "
                  "Recv<Crash<PeerTag>, _> branch for the specified unreliable "
                  "peer.  A session receiving from an unreliable peer MUST "
                  "handle that peer's crash — add a Recv<Crash<Peer>, "
                  "RecoveryBody> branch to the Offer<>.  If the peer IS "
                  "reliable, add its role to ReliableSet<> and skip the crash "
                  "branch.");
}

// A per-Offer check is local, and a protocol whose first Offer handles
// the crash can still reach a later Offer that does not.  A session
// against an unreliable peer needs the whole-tree property, so this
// walker visits every Offer reachable from the protocol root.
//
// A Delegate or Accept continuation is walked, but the delegated
// protocol itself is not.  That protocol runs at the recipient and is
// checked against the recipient's own reliability assumptions.

namespace detail::crash {

template <typename Proto, typename PeerTag>
struct all_offers_have_crash_branch;

template <typename PeerTag>
struct all_offers_have_crash_branch<End, PeerTag> : std::true_type {};

template <CrashClass C, typename PeerTag>
struct all_offers_have_crash_branch<Stop_g<C>, PeerTag> : std::true_type {};

template <typename PeerTag>
struct all_offers_have_crash_branch<Continue, PeerTag> : std::true_type {};

template <typename T, typename K, typename PeerTag>
struct all_offers_have_crash_branch<Send<T, K>, PeerTag> : all_offers_have_crash_branch<K, PeerTag> {};

template <typename T, typename K, typename PeerTag>
struct all_offers_have_crash_branch<Recv<T, K>, PeerTag> : all_offers_have_crash_branch<K, PeerTag> {};

// A Select is driven by this side, so it carries no crash-branch
// obligation of its own.
template <typename... Bs, typename PeerTag>
struct all_offers_have_crash_branch<Select<Bs...>, PeerTag>
    : std::bool_constant<(all_offers_have_crash_branch<Bs, PeerTag>::value && ...)> {};

template <typename... Bs, typename PeerTag>
struct all_offers_have_crash_branch<Offer<Bs...>, PeerTag>
    : std::bool_constant<has_crash_branch_for_peer_v<Offer<Bs...>, PeerTag>
                         && (all_offers_have_crash_branch<Bs, PeerTag>::value && ...)> {};

// The sender tag exempts this Offer alone.  The recursion still visits
// every continuation, because a nested Offer can name a different
// sender and so does need its own crash branch.  The tag is type-level
// metadata rather than a combinator, so the walk skips it.
template <typename Role, typename... Bs, typename PeerTag>
struct all_offers_have_crash_branch<Offer<Sender<Role>, Bs...>, PeerTag>
    : std::bool_constant<has_crash_branch_for_peer_v<Offer<Sender<Role>, Bs...>, PeerTag>
                         && (all_offers_have_crash_branch<Bs, PeerTag>::value && ...)> {};

// Every iteration runs the same body, so one walk of the body covers
// the whole loop.
template <typename B, typename PeerTag>
struct all_offers_have_crash_branch<Loop<B>, PeerTag> : all_offers_have_crash_branch<B, PeerTag> {};

template <VendorBackend V, typename P, typename PeerTag>
struct all_offers_have_crash_branch<VendorPinned<V, P>, PeerTag> : all_offers_have_crash_branch<P, PeerTag> {};

}  // namespace detail::crash

template <typename Proto, typename PeerTag>
inline constexpr bool every_offer_has_crash_branch_for_peer_v =
    detail::crash::all_offers_have_crash_branch<Proto, PeerTag>::value;

template <typename Proto, typename PeerTag>
consteval void assert_every_offer_has_crash_branch_for() noexcept {
    static_assert(every_offer_has_crash_branch_for_peer_v<Proto, PeerTag>,
                  "crucible::session::diagnostic [CrashBranch_Missing_In_Tree]: "
                  "assert_every_offer_has_crash_branch_for: at least one Offer<> "
                  "in the protocol tree lacks a Recv<Crash<PeerTag>, _> branch "
                  "for the specified unreliable peer.  Every Offer reachable "
                  "from an unreliable-peer session MUST handle that peer's "
                  "crash — add a Recv<Crash<Peer>, RecoveryBody> branch to the "
                  "offending Offer<>.  If the peer IS reliable along this path, "
                  "add its role to ReliableSet<> so the static check knows to "
                  "skip crash-branch enforcement.");
}

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::crash::crash_self_test {

struct Alice {};
struct Bob {};
struct Carol {};

struct Msg {};
struct Ack {};

static_assert(is_stop_v<Stop>);
static_assert(is_stop_v<Stop_g<CrashClass::Throw>>);
static_assert(Stop::crash_class == CrashClass::Abort);
static_assert(Stop_g<CrashClass::NoThrow>::crash_class == CrashClass::NoThrow);
static_assert(!is_stop_v<End>);
static_assert(!is_stop_v<Send<int, End>>);
static_assert(!is_stop_v<Loop<Send<int, Continue>>>);

static_assert(is_terminal_state_v<End>);
static_assert(is_terminal_state_v<Stop>);
static_assert(is_terminal_state_v<Stop_g<CrashClass::NoThrow>>);
static_assert(!is_terminal_state_v<Send<int, End>>);
static_assert(!is_terminal_state_v<Loop<Send<int, Continue>>>);
static_assert(!is_terminal_state_v<Continue>);

static_assert(is_head_v<Stop>);
static_assert(is_head_v<End>);

static_assert(std::is_same_v<dual_of_t<Stop>, Stop>);
static_assert(std::is_same_v<dual_of_t<Stop_g<CrashClass::ErrorReturn>>, Stop_g<CrashClass::ErrorReturn>>);
static_assert(std::is_same_v<dual_of_t<dual_of_t<Stop>>, Stop>);

static_assert(std::is_same_v<compose_t<Stop, End>, Stop>);
static_assert(std::is_same_v<compose_t<Stop, Send<int, End>>, Stop>);
static_assert(std::is_same_v<compose_t<Stop, Loop<Send<int, Continue>>>, Stop>);
static_assert(std::is_same_v<compose_t<Stop_g<CrashClass::Throw>, Send<int, End>>, Stop_g<CrashClass::Throw>>);
static_assert(
    std::is_same_v<compose_t<Stop_g<CrashClass::NoThrow>, Stop_g<CrashClass::Abort>>, Stop_g<CrashClass::NoThrow>>);

// Composition rewrites a trailing End but leaves a trailing Stop in
// place, at any depth.
static_assert(std::is_same_v<compose_t<Send<int, Stop>, Recv<bool, End>>, Send<int, Stop>>);
static_assert(std::is_same_v<compose_t<Offer<Recv<int, Stop>, Recv<bool, End>>, Send<int, End>>,
                             Offer<Recv<int, Stop>, Recv<bool, Send<int, End>>>>);

static_assert(is_well_formed_v<Stop>);
static_assert(is_well_formed_v<Stop_g<CrashClass::NoThrow>>);
// A Stop branch inside a loop is fine, but a loop whose entire body is
// Stop can never reach Continue and is rejected.
static_assert(is_well_formed_v<Loop<Select<Send<int, Continue>, Stop>>>);
static_assert(!is_well_formed_v<Loop<Stop>>);
static_assert(!is_well_formed_v<Loop<Stop_g<CrashClass::NoThrow>>>);
static_assert(!is_well_formed_v<Loop<Stop_g<CrashClass::Throw>>>);
static_assert(!is_well_formed_v<Loop<Stop_g<CrashClass::ErrorReturn>>>);

static_assert(is_subtype_sync_v<Stop, End>);
static_assert(is_subtype_sync_v<Stop, Stop>);
static_assert(is_subtype_sync_v<Stop, Send<int, End>>);
static_assert(is_subtype_sync_v<Stop, Recv<int, End>>);
static_assert(is_subtype_sync_v<Stop, Select<End>>);
static_assert(is_subtype_sync_v<Stop, Offer<End, End>>);
static_assert(is_subtype_sync_v<Stop, Loop<Send<int, Continue>>>);

// Two crashed endpoints compare by CrashLattice order rather than by
// the bottom rule.
static_assert(is_subtype_sync_v<Stop_g<CrashClass::Abort>, Stop_g<CrashClass::NoThrow>>);
static_assert(is_subtype_sync_v<Stop_g<CrashClass::Throw>, Stop_g<CrashClass::ErrorReturn>>);
static_assert(!is_subtype_sync_v<Stop_g<CrashClass::NoThrow>, Stop_g<CrashClass::Abort>>);
static_assert(!is_subtype_sync_v<Stop_g<CrashClass::ErrorReturn>, Stop_g<CrashClass::Throw>>);

static_assert(!is_subtype_sync_v<End, Stop>);
static_assert(!is_subtype_sync_v<Send<int, End>, Stop>);
static_assert(!is_subtype_sync_v<Loop<Send<int, Continue>>, Stop>);

static_assert(equivalent_sync_v<Stop, Stop>);
static_assert(!equivalent_sync_v<Stop, End>);
static_assert(!equivalent_sync_v<End, Stop>);

static_assert(is_strict_subtype_sync_v<Stop, End>);
static_assert(is_strict_subtype_sync_v<Stop, Send<int, End>>);
static_assert(!is_strict_subtype_sync_v<Stop, Stop>);

static_assert(is_crash_v<Crash<Alice>>);
static_assert(!is_crash_v<Msg>);
static_assert(!is_crash_v<Stop>);
static_assert(!is_crash_v<End>);

static_assert(std::is_same_v<typename Crash<Alice>::peer, Alice>);
static_assert(std::is_same_v<typename Crash<Bob>::peer, Bob>);

using NoneReliable = ReliableSet<>;
using AliceReliable = ReliableSet<Alice>;
using AliceAndBob = ReliableSet<Alice, Bob>;
using Everyone = ReliableSet<Alice, Bob, Carol>;

static_assert(NoneReliable::size == 0);
static_assert(AliceReliable::size == 1);
static_assert(AliceAndBob::size == 2);
static_assert(Everyone::size == 3);

static_assert(std::is_same_v<UnreliableAll, NoneReliable>);

static_assert(!is_reliable_v<NoneReliable, Alice>);
static_assert(is_reliable_v<AliceReliable, Alice>);
static_assert(!is_reliable_v<AliceReliable, Bob>);
static_assert(is_reliable_v<AliceAndBob, Alice>);
static_assert(is_reliable_v<AliceAndBob, Bob>);
static_assert(!is_reliable_v<AliceAndBob, Carol>);
static_assert(is_reliable_v<Everyone, Carol>);

using NormalOffer = Offer<Recv<Msg, End>, Recv<Ack, End>>;
static_assert(!has_crash_branch_for_peer_v<NormalOffer, Alice>);
static_assert(!has_crash_branch_for_peer_v<NormalOffer, Bob>);

using AliceCrashOffer = Offer<Recv<Msg, End>, Recv<Crash<Alice>, End>>;
static_assert(has_crash_branch_for_peer_v<AliceCrashOffer, Alice>);
static_assert(!has_crash_branch_for_peer_v<AliceCrashOffer, Bob>);

using BothCrashOffer = Offer<Recv<Msg, End>, Recv<Crash<Alice>, End>, Recv<Crash<Bob>, End>>;
static_assert(has_crash_branch_for_peer_v<BothCrashOffer, Alice>);
static_assert(has_crash_branch_for_peer_v<BothCrashOffer, Bob>);
static_assert(!has_crash_branch_for_peer_v<BothCrashOffer, Carol>);

static_assert(!has_crash_branch_for_peer_v<Offer<>, Alice>);

static_assert(!has_crash_branch_for_peer_v<End, Alice>);
static_assert(!has_crash_branch_for_peer_v<Send<int, End>, Alice>);
// A crash branch is meaningful only under Offer, where the peer drives
// the choice.  The same branch under Select would say that the
// deciding party chooses to emit a crash, and a crash is detected
// rather than chosen.
static_assert(!has_crash_branch_for_peer_v<Select<Recv<Crash<Alice>, End>>, Alice>);

consteval bool check_assert_crash_branch() {
    assert_has_crash_branch_for<AliceCrashOffer, Alice>();
    assert_has_crash_branch_for<BothCrashOffer, Bob>();
    return true;
}
static_assert(check_assert_crash_branch());

// A requester that sends one message and then offers two receptions:
// the reply, or the responder's crash.
using CrashHandledClient = Send<Msg, Offer<Recv<Ack, End>, Recv<Crash<Alice>, End>>>;

using CrashHandledServer = dual_of_t<CrashHandledClient>;

// Duality flips Send against Recv and Offer against Select, and leaves
// the Crash payload alone, because a payload is a value type rather
// than a session type.
static_assert(std::is_same_v<CrashHandledServer, Recv<Msg, Select<Send<Ack, End>, Send<Crash<Alice>, End>>>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<CrashHandledClient>>, CrashHandledClient>);

static_assert(is_well_formed_v<CrashHandledClient>);
static_assert(is_well_formed_v<CrashHandledServer>);

using ClientOffer = typename CrashHandledClient::next;
static_assert(has_crash_branch_for_peer_v<ClientOffer, Alice>);
static_assert(!has_crash_branch_for_peer_v<ClientOffer, Bob>);

static_assert(is_dual_involutive_v<Stop_g<CrashClass::Abort>>);
static_assert(is_dual_involutive_v<Stop_g<CrashClass::Throw>>);
static_assert(is_dual_involutive_v<Stop_g<CrashClass::ErrorReturn>>);
static_assert(is_dual_involutive_v<Stop_g<CrashClass::NoThrow>>);
static_assert(is_dual_involutive_v<Stop>);

}  // namespace detail::crash::crash_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
