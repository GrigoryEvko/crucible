// What fixy/session/Handle.h claims, checked.
//
// The compile-time half asserts the Stepping contract for every head
// specialization, the linearity of a handle, the size the two
// abandonment policies produce, and the exact type a step lands on.
// The runtime half walks a protocol end to end and then forks a child
// to prove that the Enforced policy's destructor really aborts on an
// abandoned handle and that the Off policy's really does not.
//
// The fork is not decoration, and what it checks is not a compile-time
// property.  Whether a policy claims to check is compile-time —
// Policy::checks_abandonment, asserted above.  Whether the DESTRUCTOR
// acts on that claim is runtime, and the two can disagree silently.  A
// tracker that returns a hardcoded "consumed", or a destructor body
// inside `#ifndef NDEBUG`, leaves a build whose constant says
// "checking" and whose destructor does nothing, and no static_assert
// can see that.  The only way to
// observe a std::abort is to run it somewhere the failure is
// recoverable, which is a child process.  expect_dies of the Check family
// requires SIGABRT and the text of the diagnostic, which is also stricter
// than ctest's WILL_FAIL, which would accept any non-zero exit.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/ScopedView.h>
#include <fixy/check/Check.h>
#include <fixy/os/Process.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>
#include <fixy/session/Projection.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <meta>
#include <optional>
#include <source_location>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "../padding_bytes.h"

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace s = fixy::session;

// The permission tree of the forked-channel walk.  The split manifest
// must be written in foundation::permissions, so the tags are declared
// before the anonymous namespace below opens.
namespace channel_tags {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Self {
    using permission_row = ::foundation::effects::Row<>;
};
struct Peer {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace channel_tags

template <>
struct foundation::permissions::can_split_into_pack<channel_tags::Whole, channel_tags::Self, channel_tags::Peer>
    : std::true_type {};
template <>
struct foundation::permissions::has_split_pack_authoring_witness<channel_tags::Whole, channel_tags::Self,
                                                                 channel_tags::Peer> : std::true_type {};

namespace {

struct Ping {
    int value = 0;
};
struct Pong {
    int value = 0;
};
struct Stop {};

// A resource with a stable address is admitted by reference.  Deriving
// from foundation::Pinned is exactly what SessionResource asks for.
struct PinnedWire : ::foundation::Pinned<PinnedWire> {
    int last_sent = 0;
};

struct ValueWire {
    int last_sent = 0;
};

// ── The Stepping contract, per head ──────────────────────────────────

using AtEnd = s::SessionHandle<s::End, ValueWire>;
using AtSend = s::SessionHandle<s::Send<Ping, s::End>, ValueWire>;
using AtRecv = s::SessionHandle<s::Recv<Pong, s::End>, ValueWire>;
using AtSelect = s::SessionHandle<s::Select<s::Send<Ping, s::End>, s::Send<Stop, s::End>>, ValueWire>;
using AtOffer = s::SessionHandle<s::Offer<s::Recv<Ping, s::End>, s::Recv<Stop, s::End>>, ValueWire>;

static_assert(s::SteppingGraded<AtEnd>);
static_assert(s::SteppingGraded<AtSend>);
static_assert(s::SteppingGraded<AtRecv>);
static_assert(s::SteppingGraded<AtSelect>);
static_assert(s::SteppingGraded<AtOffer>);
static_assert(!s::SteppingGraded<int> && !s::SteppingGraded<ValueWire>, "a value that is not a handle does not step");

// The modality is the one Modality.h reserved for session handles, and
// it is the same for every head — the protocol advances, the modality
// does not.
static_assert(AtSend::modality == ::foundation::algebra::ModalityKind::Stepping);
static_assert(AtEnd::modality == AtOffer::modality);

// is_terminal() agrees with the protocol trait.  A handle answering
// otherwise would be destroyed without complaint at a position where
// the protocol still owes a message; the concept rejects it, and these
// two lines say which way each head answers.
static_assert(AtEnd::is_terminal());
static_assert(!AtSend::is_terminal());

// The name forwarder cannot lie: SteppingGraded compares it against the
// tree's one spelling of the protocol's name.
static_assert(AtSend::protocol_name() == s::type_display_name_v<s::Send<Ping, s::End>>);
static_assert(AtSend::protocol_name().find("Send") != std::string_view::npos);

// Linearity.  Two handles at one protocol position would each believe
// they owe the next step.
static_assert(!std::is_copy_constructible_v<AtSend>);
static_assert(!std::is_copy_assignable_v<AtSend>);
static_assert(std::is_move_constructible_v<AtSend>);

// A handle offers the step of its head and no other step.  A handle at a
// Recv cannot send before its message arrives, and a handle at a Send
// cannot receive.  The check reads the members that each head declares.
[[nodiscard]] consteval bool declares_member(std::meta::info handle, std::string_view name) {
    for (const std::meta::info member : std::meta::members_of(handle, std::meta::access_context::current())) {
        if (std::meta::has_identifier(member) && std::meta::identifier_of(member) == name) return true;
    }
    return false;
}
static_assert(declares_member(^^AtSend, "send") && !declares_member(^^AtSend, "recv"));
static_assert(declares_member(^^AtRecv, "recv") && !declares_member(^^AtRecv, "send"));
static_assert(!declares_member(^^AtEnd, "send") && !declares_member(^^AtEnd, "recv"));
static_assert(!declares_member(^^AtSelect, "send") && !declares_member(^^AtSelect, "recv"));
static_assert(!declares_member(^^AtOffer, "send") && !declares_member(^^AtOffer, "recv"));

// A call site cannot build a handle directly: every value constructor
// is private and befriends only the factory and the handle family, so
// the well-formedness gate and the Loop unroll cannot be walked around.
static_assert(!std::is_constructible_v<AtSend, ValueWire>);

// ── What the policy costs ────────────────────────────────────────────
//
// The abandonment-policy decision, stated as two sizes.  Under Off the
// tracker is empty, [[no_unique_address]] collapses it, and a handle
// costs exactly its Resource.  Under Enforced it carries a flag and a
// source_location, and a test binary pays for them deliberately.

using OffHandle = s::SessionHandle<s::Send<Ping, s::End>, ValueWire, void, s::check::Off>;
using EnforcedHandle = s::SessionHandle<s::Send<Ping, s::End>, ValueWire, void, s::check::Enforced>;

static_assert(sizeof(OffHandle) == sizeof(ValueWire));
static_assert(sizeof(EnforcedHandle) > sizeof(ValueWire));

// The record of fixy/session/Watch.h sits in the padding of the policy,
// and the count of steps is one more word, so a checked handle over a
// small resource is four words.
static_assert(sizeof(EnforcedHandle) == 4 * sizeof(void*));

// A handle refuses a new-expression, as Permission and ReadView do.  A
// container builds it in place, so an optional of a handle stays legal.
template <typename H>
concept HeapNewable = requires(H&& handle) { new H(std::move(handle)); };
static_assert(!HeapNewable<AtSend>);
static_assert(!HeapNewable<OffHandle>);
static_assert(std::is_constructible_v<std::optional<AtSend>, AtSend&&>);

// The policy is IN THE TYPE.  These two are different types, so a
// Debug object and a Release object that disagree about the layout
// cannot silently share one definition — the mismatch is a diagnostic
// at the boundary instead.
static_assert(!std::is_same_v<OffHandle, EnforcedHandle>);
static_assert(
    std::is_same_v<AtSend, s::SessionHandle<s::Send<Ping, s::End>, ValueWire, void, s::DefaultAbandonmentPolicy>>);

// The default is the checking policy in every build mode.  NDEBUG does
// not change it, so a Release binary catches a dropped protocol in the
// same way as this test.
static_assert(s::default_policy_checks_abandonment);
static_assert(std::is_same_v<s::DefaultAbandonmentPolicy, s::check::Enforced>);

// check::Cancel carries the same state as check::Enforced and a
// different action.  It is a different type, so a handle under it is a
// different handle type.
using CancelHandle = s::SessionHandle<s::Send<Ping, s::End>, ValueWire, void, s::check::Cancel>;
static_assert(s::AbandonmentPolicy<s::check::Cancel>);
static_assert(s::check::Cancel::action == s::AbandonAction::Cancel);
static_assert(s::check::Enforced::action == s::AbandonAction::Abort);
static_assert(s::check::Off::action == s::AbandonAction::Ignore);
static_assert(!std::is_same_v<CancelHandle, EnforcedHandle>);

// ── One handle ───────────────────────────────────────────────────────
//
// The permission set is the last parameter, and Handle names the same
// class with the parameters in the design's order.  The set is a type
// only, so it adds no byte.

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
// The brand of the type checks.  No check mints a token of it.
struct HandleBrand {};
using Token = ::foundation::permissions::Permission<Region, HandleBrand>;
using NoPerms = ::foundation::permissions::EmptyPermSet;
using HoldsRegion = ::foundation::permissions::PermSet<Token>;

static_assert(std::is_same_v<s::Handle<s::End, NoPerms, ValueWire>, AtEnd>);
static_assert(std::is_same_v<typename AtSend::perm_set, NoPerms>);
static_assert(sizeof(s::Handle<s::Send<Ping, s::End>, HoldsRegion, ValueWire, void, s::check::Enforced>)
              == sizeof(EnforcedHandle));

// A loop records the permission set it saw at its entry, so a Continue
// can check that one iteration did not change it.  With an empty set the
// record is the Loop itself.
static_assert(std::is_same_v<s::detail::loop_frame_t<s::Loop<s::Send<Ping, s::Continue>>, NoPerms>,
                             s::Loop<s::Send<Ping, s::Continue>>>);
static_assert(std::is_same_v<s::detail::loop_frame_t<s::Loop<s::Send<Ping, s::Continue>>, HoldsRegion>,
                             s::detail::PermLoopFrame<s::Loop<s::Send<Ping, s::Continue>>, HoldsRegion>>);
static_assert(std::is_same_v<s::detail::loop_entry_perm_set_t<s::detail::PermLoopFrame<s::Loop<s::End>, HoldsRegion>>,
                             HoldsRegion>);

// A loop context is void, a Loop, a frame or a brand around one of these,
// and the body comes from the template argument of the Loop.  A brand
// passes through to the context that it wraps.
using ReceivesForever = s::Loop<s::Recv<Pong, s::Continue>>;
static_assert(std::is_same_v<s::detail::loop_body_t<ReceivesForever>, s::Recv<Pong, s::Continue>>);
static_assert(std::is_same_v<s::detail::loop_body_t<s::detail::PermLoopFrame<ReceivesForever, HoldsRegion>>,
                             s::Recv<Pong, s::Continue>>);
static_assert(
    std::is_same_v<s::session_loop_ctx_inner_t<s::detail::session_brand<Region, ReceivesForever>>, ReceivesForever>);
static_assert(std::is_same_v<s::session_loop_ctx_inner_t<void>, void>);
static_assert(
    std::is_same_v<s::session_loop_ctx_rebind_inner_t<s::detail::session_brand<Region, void>, ReceivesForever>,
                   s::detail::session_brand<Region, ReceivesForever>>);
static_assert(std::is_same_v<s::session_loop_ctx_rebind_inner_t<void, ReceivesForever>, ReceivesForever>);
static_assert(
    std::is_same_v<s::detail::loop_entry_perm_set_t<s::detail::session_brand<Region, ReceivesForever>>, NoPerms>);

// The delta of one message comes from fixy/session/Flow.h.  A send of
// a token takes its region from the set, and the matching receive adds
// it.  A sender that does not hold the region cannot send the token, and
// a receiver that holds it already cannot receive a second owner.  End
// refuses an open loan and admits an owned tag.
using LendsRegion = ::foundation::permissions::PermSet<s::LentOut<Region, HandleBrand>>;
static_assert(::foundation::permissions::perm_set_equal(^^s::detail::perm_set_after_send_t<HoldsRegion, Token>,
                                                        ^^NoPerms));
static_assert(::foundation::permissions::perm_set_equal(^^s::detail::perm_set_after_recv_t<NoPerms, Token>,
                                                        ^^HoldsRegion));
static_assert(::foundation::permissions::perm_set_equal(^^s::detail::perm_set_after_send_t<NoPerms, Ping>, ^^NoPerms));
static_assert(s::detail::handle_admits_send_v<HoldsRegion, Token>);
static_assert(!s::detail::handle_admits_send_v<NoPerms, Token>);
static_assert(s::detail::handle_admits_recv_v<NoPerms, Token>);
static_assert(!s::detail::handle_admits_recv_v<HoldsRegion, Token>);
static_assert(s::detail::perm_set_admits_close_v<HoldsRegion>);
static_assert(!s::detail::perm_set_admits_close_v<LendsRegion>);

// The mint walks every path, so an arm that no run selects is checked.
// Each refused protocol differs from its admitted neighbour in one step.
using Lend = s::Borrowed<int, Region, HandleBrand>;
static_assert(s::PermissionFlowCloses<s::Send<Token, s::End>, HoldsRegion>);
static_assert(!s::PermissionFlowCloses<s::Send<Token, s::End>, NoPerms>);
static_assert(!s::PermissionFlowCloses<s::Select<s::End, s::Send<Token, s::End>>, NoPerms>,
              "the second arm sends a region that the set does not hold");
static_assert(!s::PermissionFlowCloses<s::Select<s::End, s::Send<Lend, s::End>>, HoldsRegion>,
              "the second arm lends the region and reaches End with the loan open");
static_assert(!s::PermissionFlowCloses<s::Offer<s::End, s::Recv<Token, s::End>>, HoldsRegion>,
              "the second arm receives a second owner of the region");
static_assert(s::PermissionFlowCloses<s::Loop<s::Send<Token, s::Recv<Token, s::Continue>>>, HoldsRegion>,
              "each iteration sends the region and receives it back");
static_assert(!s::PermissionFlowCloses<s::Loop<s::Send<Token, s::Continue>>, HoldsRegion>,
              "one iteration gives the region away");
static_assert(s::PermissionFlowCloses<s::VendorPinned<::fixy::session::VendorBackend::NV, s::End>, NoPerms>);

// Each level of doubled_arms is a Select whose two arms are one protocol.
// The protocol of 16 levels has 17 distinct nodes, and its expanded tree
// has 2^16 leaves.  A walk of the expanded tree passes the operation limit
// of a constant evaluation.  The walk keeps the answer of each node, so it
// visits each distinct node one time.
template <int Depth>
struct doubled_arms {
    using type = s::Select<typename doubled_arms<Depth - 1>::type, typename doubled_arms<Depth - 1>::type>;
};
template <>
struct doubled_arms<0> {
    using type = s::Send<Token, s::End>;
};
static_assert(s::PermissionFlowCloses<doubled_arms<16>::type, HoldsRegion>,
              "the walk visits each shared continuation one time");
static_assert(!s::PermissionFlowCloses<doubled_arms<16>::type, NoPerms>, "each leaf sends a region that no set holds");

// A chain of 300 messages with no shared node.  The walk steps into each
// message one call deep, in one evaluation, so a deep protocol stays under
// the depth limit of a constant evaluation and nests no instantiation.
template <int Length>
struct message_chain {
    using type = s::Send<Ping, typename message_chain<Length - 1>::type>;
};
template <>
struct message_chain<0> {
    using type = s::End;
};
static_assert(s::PermissionFlowCloses<message_chain<300>::type, NoPerms>, "a deep protocol walks in one evaluation");

// The same rules hold for each payload that moves the token: a bare
// Permission, a Transferable and a Returned.  A send needs the region.  A
// loop that receives the region must give it back in the same iteration,
// and a loop that gives it away must receive it back.  A nested loop that
// sends it on each inner iteration has no region after the first, unless
// the inner iteration receives it back.
template <class Moves>
[[nodiscard]] consteval bool moves_the_region() noexcept {
    return s::PermissionFlowCloses<s::Send<Moves, s::End>, HoldsRegion>
        && !s::PermissionFlowCloses<s::Send<Moves, s::End>, NoPerms>
        && s::PermissionFlowCloses<s::Loop<s::Recv<Moves, s::Send<Moves, s::Continue>>>, NoPerms>
        && !s::PermissionFlowCloses<s::Loop<s::Recv<Moves, s::Continue>>, NoPerms>
        && !s::PermissionFlowCloses<s::Loop<s::Send<Moves, s::Continue>>, HoldsRegion>
        && !s::PermissionFlowCloses<s::Loop<s::Recv<Moves, s::Loop<s::Send<Moves, s::Continue>>>>, NoPerms>
        && s::PermissionFlowCloses<s::Loop<s::Recv<Moves, s::Loop<s::Send<Moves, s::Recv<Moves, s::Continue>>>>>,
                                   NoPerms>;
}
static_assert(moves_the_region<Token>());
static_assert(moves_the_region<s::Transferable<int, Region, HandleBrand>>());
static_assert(moves_the_region<s::Returned<int, Region, HandleBrand>>());
// A payload of another brand of the tag moves a token that the set does not
// hold, so the send of the first step is refused.
struct OtherBrand {};
static_assert(!s::PermissionFlowCloses<s::Send<s::Transferable<int, Region, OtherBrand>, s::End>, HoldsRegion>);
static_assert(
    !s::PermissionFlowCloses<s::Send<::foundation::permissions::Permission<Region, OtherBrand>, s::End>, HoldsRegion>);

// A vendor pin is a declaration for the layer above.  The handle steps the
// protocol that the pin wraps, at the top and after a step.
using PinnedSend = s::VendorPinned<::fixy::session::VendorBackend::NV, s::Send<Ping, s::End>>;
static_assert(std::is_same_v<s::detail::first_handle_t<PinnedSend, ValueWire, s::check::Enforced>,
                             s::Handle<s::Send<Ping, s::End>, NoPerms, ValueWire, void, s::check::Enforced>>);
static_assert(
    std::is_same_v<s::detail::first_handle_t<s::Send<Ping, s::VendorPinned<::fixy::session::VendorBackend::NV, s::End>>,
                                             ValueWire, s::check::Enforced>,
                   s::Handle<s::Send<Ping, s::VendorPinned<::fixy::session::VendorBackend::NV, s::End>>, NoPerms,
                             ValueWire, void, s::check::Enforced>>);

// ── Where a step lands ───────────────────────────────────────────────

using Once = s::Send<Ping, s::Recv<Pong, s::End>>;
using Looping = s::Loop<s::Send<Ping, s::Continue>>;

// Minting a Loop unrolls one iteration: the handle sits at the body's
// head and carries the Loop as its context, which is what binds the
// Continue inside it.
using LoopHead =
    decltype(s::mint_session<Looping>(std::declval<::fixy::TestRunnerCtx const&>(), std::declval<ValueWire>()));
static_assert(std::is_same_v<typename LoopHead::protocol, s::Send<Ping, s::Continue>>);
static_assert(std::is_same_v<typename LoopHead::loop_ctx, Looping>);

// A non-Loop head passes through with a void context.
using OnceHead =
    decltype(s::mint_session<Once>(std::declval<::fixy::TestRunnerCtx const&>(), std::declval<ValueWire>()));
static_assert(std::is_same_v<typename OnceHead::protocol, Once>);
static_assert(std::is_same_v<typename OnceHead::loop_ctx, void>);

// A reference to a Pinned resource is admitted.  The handle stores the
// reference, and the caller keeps the object alive.
static_assert(s::SessionResource<PinnedWire&>);
static_assert(s::SessionResource<ValueWire>);
static_assert(!s::SessionResource<ValueWire&>);
static_assert(!s::SessionResource<PinnedWire&&>);

// The mint's gate is a concept, so a downstream requires-expression can
// ask whether a handle is mintable and get an answer instead of a hard
// error at instantiation.  Each rejection is paired with the nearest
// protocol that IS accepted, so a negative fixture is not the only thing
// standing between a broken gate and a green build: a gate that grew too
// broad breaks the accepting line here instead.
static_assert(s::WellFormedRunnableProtocol<Once>);
static_assert(!s::WellFormedRunnableProtocol<s::Continue>);
static_assert(s::WellFormedRunnableProtocol<s::Loop<s::Send<Ping, s::Continue>>>);
static_assert(!s::WellFormedRunnableProtocol<s::Send<Ping, s::Continue>>);
static_assert(s::WellFormedRunnableProtocol<s::Send<Ping, s::Select<s::Send<Stop, s::End>>>>);
static_assert(!s::WellFormedRunnableProtocol<s::Send<Ping, s::Select<>>>);

// The detach roster admits its own tags and nothing else.
static_assert(s::DetachReason<s::detach_reason::TestInstrumentation>);
static_assert(!s::DetachReason<s::detach_reason::tag_base>);
static_assert(!s::DetachReason<int>);

// ── Runtime: walking a protocol ──────────────────────────────────────

[[nodiscard]] int walk_once() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<Once>(ctx, ValueWire{});

    auto after_send = std::move(handle).send(Ping{3}, [](ValueWire& w, Ping& p) noexcept {
        w.last_sent = p.value;
        return true;
    });
    static_assert(std::is_same_v<typename decltype(after_send)::protocol, s::Recv<Pong, s::End>>);

    auto [pong, at_end] =
        std::move(after_send).recv([](ValueWire& w) noexcept { return ::fixy::some(Pong{w.last_sent}); });
    if (pong.value != 3) {
        std::fprintf(stderr, "the value the transport wrote did not travel with the resource\n");
        return 1;
    }

    // close() is reachable only at End, and it is what hands the
    // resource back.  The value it returns is the one every step
    // carried, which is how a caller recovers its channel after the
    // protocol has run out.
    const ValueWire returned = std::move(at_end).close();
    if (returned.last_sent != 3) {
        std::fprintf(stderr, "close returned a resource that lost the protocol's effect\n");
        return 1;
    }
    return 0;
}

// The Pinned-reference branch of SessionResource, walked rather than
// asserted.  A factory that took `Resource r` and passed `std::move(r)`
// would admit this Resource shape and then fail to mint one: for
// Resource = Wire& the moved value is an rvalue that cannot bind back to
// Wire&.  The factory forwards instead of moving, and that is what makes
// this function compile.
[[nodiscard]] int walk_pinned_reference() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    PinnedWire wire{};
    const PinnedWire* const address_before = &wire;

    auto handle =
        s::mint_session<s::Send<Ping, s::End>, s::DefaultAbandonmentPolicy, ::fixy::TestRunnerCtx, PinnedWire&>(ctx,
                                                                                                                wire);
    auto at_end = std::move(handle).send(Ping{11}, [](PinnedWire& w, Ping& p) noexcept {
        w.last_sent = p.value;
        return true;
    });
    if (wire.last_sent != 11) {
        std::fprintf(stderr, "the transport did not reach the referenced resource\n");
        return 1;
    }

    // close() hands back the reference, not a copy — a Pinned resource
    // cannot be copied or moved, which is why the concept admits it by
    // reference in the first place.
    PinnedWire& returned = std::move(at_end).close();
    if (&returned != address_before) {
        std::fprintf(stderr, "close returned a different object than the one the handle borrowed\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int walk_choice() {
    using Choice = s::Select<s::Send<Ping, s::End>, s::Send<Stop, s::End>>;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<Choice>(ctx, ValueWire{});

    // select<I> tells the peer which branch was taken; select_local<I>
    // deliberately does not, and the two names are what keep that
    // difference visible at the call site.
    std::size_t signalled = 99;
    auto chosen = std::move(handle).select<1>([&signalled](ValueWire&, std::size_t label) noexcept {
        signalled = label;
        return true;
    });
    if (signalled != 1) {
        std::fprintf(stderr, "select did not signal the branch index\n");
        return 1;
    }
    static_assert(std::is_same_v<typename decltype(chosen)::protocol, s::Send<Stop, s::End>>);

    auto at_end = std::move(chosen).send(Stop{}, [](ValueWire&, Stop&) noexcept { return true; });
    (void)std::move(at_end).close();
    return 0;
}

[[nodiscard]] int walk_offer() {
    using Served = s::Offer<s::Recv<Ping, s::End>, s::Recv<Stop, s::End>>;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<Served>(ctx, ValueWire{});

    // The handler runs once, for the branch the peer's label names.
    const int taken = std::move(handle).branch(
        [](ValueWire&) noexcept { return ::fixy::some(std::size_t{0}); },
        [](auto branch_handle) {
            using B = typename decltype(branch_handle)::protocol;
            using Message = typename decltype(branch_handle)::message_type;
            auto [msg, at_end] =
                std::move(branch_handle).recv([](ValueWire&) noexcept { return ::fixy::some(Message{}); });
            (void)msg;
            (void)std::move(at_end).close();
            return std::is_same_v<B, s::Recv<Ping, s::End>> ? 0 : 1;
        });
    if (taken != 0) {
        std::fprintf(stderr, "branch entered the wrong arm\n");
        return 1;
    }
    return 0;
}

// ── The word of a choice ─────────────────────────────────────────────
//
// A PeerMsg names a label key, so a choice of PeerMsg branches is keyed:
// its wire word is the label word, and the peer enters the branch of the
// same label in whatever position it holds.  A choice of plain payloads
// is positional: its word is the position.
//
// A role and a label key feed a stable id, so each has external linkage.

}  // namespace

namespace test_session_handle_labels {
struct Bob {};
struct Hello {};
struct Bye {};
}  // namespace test_session_handle_labels

namespace {

using test_session_handle_labels::Bob;
using test_session_handle_labels::Bye;
using test_session_handle_labels::Hello;

// Hello continues with a reply, so the branch that a handle enters shows
// in its type.
using KeyedSelect =
    s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::Recv<int, s::End>>, s::Send<s::PeerMsg<Bob, Bye, int>, s::End>>;
using KeyedOfferSwapped =
    s::Offer<s::Recv<s::PeerMsg<Bob, Bye, int>, s::End>, s::Recv<s::PeerMsg<Bob, Hello, int>, s::Send<int, s::End>>>;
using PlainOffer = s::Offer<s::Recv<Ping, s::End>, s::Recv<Stop, s::End>>;

static_assert(s::is_keyed_choice_v<KeyedSelect> && s::is_keyed_choice_v<KeyedOfferSwapped>);
static_assert(!s::is_keyed_choice_v<PlainOffer>);
static_assert(s::branch_wire_word_v<KeyedSelect, 0> == s::branch_wire_word_v<KeyedOfferSwapped, 1>,
              "a label keeps its word in another position");
static_assert(s::branch_of_wire_word<KeyedOfferSwapped>(s::branch_wire_word_v<KeyedSelect, 0>) == 1);
static_assert(s::branch_of_wire_word<KeyedOfferSwapped>(s::branch_wire_word_v<KeyedSelect, 1>) == 0);
static_assert(s::branch_of_wire_word<KeyedOfferSwapped>(0) == s::no_branch
                  && s::branch_of_wire_word<KeyedOfferSwapped>(1) == s::no_branch,
              "a position is no word of a keyed choice");
static_assert(s::branch_wire_word_v<PlainOffer, 1> == 1 && s::branch_of_wire_word<PlainOffer>(1) == 1);
static_assert(s::branch_of_wire_word<PlainOffer>(s::branch_wire_word_v<KeyedSelect, 0>) == s::no_branch,
              "a label word is no position");

// A queue of words that the two ends share, in the order of the writes.
// The two ends take turns, so one queue carries each direction.  It is
// Pinned, so each handle holds it by reference and no copy of a pointer to
// it exists.  A label word and a value each take one word.
struct WordQueue : ::foundation::Pinned<WordQueue> {
    std::array<std::uint64_t, 8> words{};
    std::size_t head = 0;
    std::size_t tail = 0;
};

constexpr auto push_word = [](WordQueue& queue, std::size_t word) noexcept {
    if (queue.tail == queue.words.size()) return false;
    queue.words[queue.tail++] = word;
    return true;
};
constexpr auto push_int = [](WordQueue& queue, int& value) noexcept {
    if (queue.tail == queue.words.size()) return false;
    queue.words[queue.tail++] = static_cast<std::uint64_t>(value);
    return true;
};
constexpr auto pop_word = [](WordQueue& queue) noexcept -> ::fixy::Option<std::size_t> {
    if (queue.head == queue.tail) return ::fixy::none;
    return ::fixy::some(static_cast<std::size_t>(queue.words[queue.head++]));
};
constexpr auto pop_int = [](WordQueue& queue) noexcept -> ::fixy::Option<int> {
    if (queue.head == queue.tail) return ::fixy::none;
    return ::fixy::some(static_cast<int>(queue.words[queue.head++]));
};

// The Select holds Hello first and the Offer holds it second.  The word
// of Hello reaches the Hello branch, and the value of Hello follows it.
[[nodiscard]] int walk_keyed_choice_in_another_order() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    WordQueue wire{};
    auto sender =
        s::mint_session<KeyedSelect, s::DefaultAbandonmentPolicy, ::fixy::TestRunnerCtx, WordQueue&>(ctx, wire);
    auto receiver =
        s::mint_session<KeyedOfferSwapped, s::DefaultAbandonmentPolicy, ::fixy::TestRunnerCtx, WordQueue&>(ctx, wire);

    // The select sends the label word of Hello, and the sender then stands
    // at the value step of Hello.
    auto value_step = std::move(sender).select<0>(push_word);
    static_assert(std::is_same_v<typename decltype(value_step)::protocol, s::Send<int, s::Recv<int, s::End>>>);
    auto awaiting = std::move(value_step).send(5, push_int);
    static_assert(std::is_same_v<typename decltype(awaiting)::protocol, s::Recv<int, s::End>>);

    int hello_value = 0;
    const int taken = std::move(receiver).branch(pop_word, [&hello_value](auto branch_handle) {
        using B = typename decltype(branch_handle)::protocol;
        if constexpr (std::is_same_v<B, s::Recv<int, s::Send<int, s::End>>>) {
            auto [value, replying] = std::move(branch_handle).recv(pop_int);
            hello_value = value;
            auto at_end = std::move(replying).send(7, push_int);
            (void)std::move(at_end).close();
            return 0;
        } else {
            auto [value, at_end] = std::move(branch_handle).recv(pop_int);
            static_cast<void>(value);
            (void)std::move(at_end).close();
            return 1;
        }
    });
    if (taken != 0) {
        std::fprintf(stderr, "the label word of Hello did not reach the Hello branch of the peer\n");
        return 1;
    }
    if (hello_value != 5) {
        std::fprintf(stderr, "the value of Hello is %d on the receiving side, want 5\n", hello_value);
        return 1;
    }
    auto [reply, sender_end] = std::move(awaiting).recv(pop_int);
    (void)std::move(sender_end).close();
    return reply == 7 ? 0 : 1;
}

// ── Viewing a position ───────────────────────────────────────────────
//
// A view names the handle's position with a tag.  A tag for any other
// position has no view_ok, so the gate refuses it when the program
// compiles.
static_assert(::fixy::CarrierDeclaresViewState<AtSend, s::position::AtSend>);
static_assert(!::fixy::CarrierDeclaresViewState<AtSend, s::position::AtRecv>);
static_assert(!::fixy::CarrierDeclaresViewState<AtSend, s::position::AtEnd>);
static_assert(::fixy::CarrierDeclaresViewState<AtEnd, s::position::AtEnd>);
static_assert(::fixy::CarrierDeclaresViewState<AtOffer, s::position::AtOffer>);
static_assert(!::fixy::CarrierDeclaresViewState<AtOffer, s::position::AtSelect>);
static_assert(!::fixy::CarrierDeclaresViewState<AtSend, int>, "a tag outside the family names no position");

[[nodiscard]] int view_a_position() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<s::Send<Ping, s::End>>(ctx, ValueWire{});
    {
        const auto view = ::fixy::mint_view<s::position::AtSend>(handle);
        if (view.carrier().protocol_name() != AtSend::protocol_name()) {
            std::fprintf(stderr, "the view does not look at the handle it was minted from\n");
            return 1;
        }
    }
    auto at_end = std::move(handle).send(Ping{9}, [](ValueWire& w, Ping& p) noexcept {
        w.last_sent = p.value;
        return true;
    });
    (void)std::move(at_end).close();
    return 0;
}

// ── Runtime: the callback entry point ────────────────────────────────
//
// The library builds the handle and closes the one the body returns.
// Every handle of the session carries the body's brand, which is how the
// entry point knows that the End handle is the one it gave out.
[[nodiscard]] int walk_with_session() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    const ValueWire back = s::with_session<Once>(ctx, ValueWire{}, [](auto head) noexcept {
        static_assert(!s::detail::carries_brand<typename decltype(head)::loop_ctx, void>,
                      "a handle of an owned session carries the brand of its body");
        auto after = std::move(head).send(Ping{5}, [](ValueWire& w, Ping& p) noexcept {
            w.last_sent = p.value;
            return true;
        });
        auto [pong, at_end] =
            std::move(after).recv([](ValueWire& w) noexcept { return ::fixy::some(Pong{w.last_sent}); });
        (void)pong;
        return std::move(at_end);
    });
    if (back.last_sent != 5) {
        std::fprintf(stderr, "with_session returned a resource that lost the protocol's effect\n");
        return 1;
    }
    return 0;
}

// ── Runtime: a receiving loop ────────────────────────────────────────
//
// A handle of Loop<Recv<Pong, Continue>> receives, and after its Continue
// it stands at the same reception again.
[[nodiscard]] int walk_receiving_loop() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_session<ReceivesForever>(ctx, ValueWire{7});
    int received = 0;
    for (int round = 0; round < 3; ++round) {
        auto [pong, next] = std::move(head).recv([](ValueWire& w) noexcept { return ::fixy::some(Pong{w.last_sent}); });
        static_assert(std::is_same_v<decltype(next), decltype(head)>, "a Continue lands on the reception again");
        received += pong.value;
        head = std::move(next);
    }
    std::move(head).detach(s::detach_reason::InfiniteLoopProtocol{});
    if (received != 21) {
        std::fprintf(stderr, "the receiving loop did not receive three times\n");
        return 1;
    }
    return 0;
}

// ── Runtime: a loop with a permission set ────────────────────────────
//
// A handle that holds a permission walks a loop three times.  Each
// Continue checks that the iteration left the set as the loop found it.
// The mint consumes the token of the region, and the slots of the handle
// keep it.
[[nodiscard]] int walk_loop_with_permission_set() {
    using Forever = s::Loop<s::Send<Ping, s::Continue>>;
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    const BgCtx ctx{::foundation::effects::testing::bg()};
    auto root = ::foundation::permissions::mint_permission_root<Region>();
    using RootSet = ::foundation::permissions::PermSet<
        ::foundation::permissions::Permission<Region, ::foundation::brand::brand_of_t<decltype(root)>>>;
    auto head = s::mint_permissioned_session<Forever, s::check::Enforced>(ctx, ValueWire{}, std::move(root));
    static_assert(std::is_same_v<typename decltype(head)::perm_set, RootSet>);
    static_assert(std::is_same_v<typename decltype(head)::loop_ctx, s::detail::PermLoopFrame<Forever, RootSet>>);
    for (int round = 0; round < 3; ++round) {
        auto next = std::move(head).send(Ping{round}, [](ValueWire& w, Ping& p) noexcept {
            w.last_sent = p.value;
            return true;
        });
        static_assert(std::is_same_v<decltype(next), decltype(head)>, "a Continue lands on the loop head again");
        head = std::move(next);
    }
    if (head.resource().last_sent != 2) {
        std::fprintf(stderr, "the loop did not carry the resource through its iterations\n");
        return 1;
    }
    std::move(head).detach(s::detach_reason::InfiniteLoopProtocol{});
    return 0;
}

// ── Runtime: a vendor-pinned session ─────────────────────────────────

[[nodiscard]] int walk_vendor_pinned_session() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_session<PinnedSend>(ctx, ValueWire{});
    auto at_end = std::move(head).send(Ping{7}, [](ValueWire& w, Ping& p) noexcept {
        w.last_sent = p.value;
        return true;
    });
    if (std::move(at_end).close().last_sent != 7) {
        std::fprintf(stderr, "the vendor-pinned session did not carry its message\n");
        return 1;
    }
    return 0;
}

// A vendor pin around a Loop.  The handle removes the pin first and then
// enters the Loop, so each Continue lands at the head of the body again.
using PinnedLoopBody = s::Select<s::Send<Ping, s::Continue>, s::End>;
using PinnedLoop = s::VendorPinned<::fixy::session::VendorBackend::NV, s::Loop<PinnedLoopBody>>;

[[nodiscard]] int walk_vendor_pinned_loop() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_session<PinnedLoop>(ctx, ValueWire{});
    static_assert(std::is_same_v<typename decltype(head)::protocol, PinnedLoopBody>);
    static_assert(std::is_same_v<typename decltype(head)::loop_ctx, s::Loop<PinnedLoopBody>>);
    std::size_t labels = 0;
    const auto push_label = [&labels](ValueWire&, std::size_t label) noexcept {
        labels += label + 1;
        return true;
    };
    for (int round = 0; round < 3; ++round) {
        auto sending = std::move(head).select<0>(push_label);
        auto next = std::move(sending).send(Ping{round}, [](ValueWire& w, Ping& p) noexcept {
            w.last_sent = p.value;
            return true;
        });
        static_assert(std::is_same_v<decltype(next), decltype(head)>, "a Continue lands at the head of the body");
        head = std::move(next);
    }
    auto at_end = std::move(head).select<1>(push_label);
    if (std::move(at_end).close().last_sent != 2 || labels != 5) {
        ::fixy::report(::fixy::Sink::Err, "the vendor-pinned loop did not walk three rounds and then end\n");
        return 1;
    }
    return 0;
}

// ── Runtime: a token moves through a session ─────────────────────────
//
// The sender holds Region and sends its token, so its End handle holds
// nothing.  The receiver starts with nothing and receives the token, so
// its End handle holds Region.  The token moves out of the slots of the
// sender and into the slots of the receiver, so the caller never holds it:
// the send takes no value, the receive gives no value, and close() at End
// gives the token back with the Resource.  The Resource is the wire: the
// sender closes with the token in it, and the receiver opens on it.

template <class TokenType>
struct TokenWire {
    ::fixy::FixedList<TokenType, 1> slot{};
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

[[nodiscard]] int move_token_through_session() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    const BgCtx ctx{::foundation::effects::testing::bg()};
    auto root = ::foundation::permissions::mint_permission_root<Region>();
    using RootToken = decltype(root);
    using Wire = TokenWire<RootToken>;
    auto sender =
        s::mint_permissioned_session<s::Send<RootToken, s::End>, s::check::Enforced>(ctx, Wire{}, std::move(root));
    auto sender_done = std::move(sender).send([](Wire& w, RootToken& token) noexcept {
        static_cast<void>(w.slot.append(std::move(token)).expect("the wire is empty"));
        return true;
    });
    static_assert(std::is_same_v<typename decltype(sender_done)::perm_set, NoPerms>);
    Wire wire = std::move(sender_done).close();

    auto receiver = s::mint_session<s::Recv<RootToken, s::End>>(ctx, std::move(wire));
    auto receiver_done = std::move(receiver).recv([](Wire& w) noexcept { return w.slot.take_last(); });
    static_assert(::foundation::permissions::perm_set_equal(^^typename decltype(receiver_done)::perm_set,
                                                            ^^::foundation::permissions::PermSet<RootToken>),
                  "the receive puts the token into the slots, and the caller holds no token");
    auto [back, token] = std::move(receiver_done).close();
    static_assert(std::is_same_v<decltype(token), RootToken>,
                  "the token leaves the session with the brand of its root");
    const bool is_wire_empty = back.slot.size() == 0;
    ::foundation::linear::drop(std::move(token));
    ::foundation::linear::drop(std::move(back));
    if (!is_wire_empty) {
        std::fprintf(stderr, "the receive left the token on the wire\n");
        return 1;
    }
    return 0;
}

// ── Runtime: the two channel mints ───────────────────────────────────
//
// A one-slot mailbox in each direction.  The two endpoints hold a pointer
// to the same pipe, so each side of the protocol reads what the other
// side wrote.  A read polls, so each wait goes through the watch of
// fixy/session/Watch.h.

struct Mailbox {
    ::fixy::Atomic<int> value{0};
    ::fixy::Atomic<bool> full{false};
};

// The brand of each Ref of a pipe: a pipe lives for its whole test, as the
// ends that reach it do.
struct PipeBrand {};

struct Pipe : ::foundation::Pinned<Pipe> {
    using brand_type = PipeBrand;
    Mailbox to_peer;
    Mailbox to_self;
};

struct SelfEnd {
    ::fixy::Ref<Pipe, PipeBrand> pipe;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
struct PeerEnd {
    ::fixy::Ref<Pipe, PipeBrand> pipe;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

// The two ends reach the pipe only through its atomics, so each end moves to
// the thread of its side (foundation/Transfer.h).
static_assert(::foundation::transfer::Sendable<SelfEnd> && ::foundation::transfer::Sendable<PeerEnd>);

// A trying write: the one slot takes a value only while it is empty.
[[nodiscard]] bool put(Mailbox& box, int value) noexcept {
    if (box.full.load_acquire()) return false;
    box.value.store_release(value);
    box.full.store_release(true);
    return true;
}

// A polling read: no value while the slot is empty.
[[nodiscard]] ::fixy::Option<int> try_take(Mailbox& box) noexcept {
    if (!box.full.load_acquire()) return ::fixy::none;
    const int value = box.value.load_acquire();
    box.full.store_release(false);
    return ::fixy::some(value);
}

// A polling read of a message that carries one int.
template <typename Message>
[[nodiscard]] ::fixy::Option<Message> try_take_as(Mailbox& box) noexcept {
    ::fixy::Option<int> value = try_take(box);
    if (value.is_none()) return ::fixy::none;
    return ::fixy::some(Message{std::move(value).expect("the test found a value")});
}

// The row gate of a channel.  One context runs the two sides of a
// channel, so it must hold each effect that a payload of either side
// carries.  The background context holds Bg and Alloc, the compile
// context holds IO too, and the test context holds IO and no Bg.
using ChannelBgCtx = ::fixy::BgDrainCtx;
using ChannelBgIoCtx = ::fixy::BgCompileCtx;
using ChannelTestCtx = ::fixy::TestRunnerCtx;
using IoWork = ::foundation::effects::Computation<::foundation::effects::Row<::foundation::effects::Effect::IO>, int>;
using BgWork = ::foundation::effects::Computation<::foundation::effects::Row<::foundation::effects::Effect::Bg>, int>;
using SendsIo = s::Send<IoWork, s::End>;
using SendsBg = s::Send<BgWork, s::End>;

template <typename Ctx, typename Proto>
inline constexpr bool forked_gate_admits_v =
    s::CtxFitsForkedChannel<Ctx, Proto, channel_tags::Whole, channel_tags::Self, channel_tags::Peer>;

static_assert(!s::CtxAdmitsChannelRow<ChannelBgCtx, Once, s::Recv<IoWork, s::End>>,
              "the row of the peer side counts as the row of the self side does");
static_assert(s::CtxAdmitsChannelRow<ChannelBgIoCtx, Once, s::Recv<IoWork, s::End>>);
static_assert(!forked_gate_admits_v<ChannelBgCtx, SendsIo>, "the background context holds no IO");
static_assert(forked_gate_admits_v<ChannelBgIoCtx, SendsIo>);
static_assert(!s::CtxFitsTestChannel<ChannelTestCtx, SendsBg>, "the test context holds no Bg");
static_assert(s::CtxFitsTestChannel<ChannelTestCtx, SendsIo>);

// The two endpoints on one thread, through the test hatch.  The order is
// fixed: the self side sends before the peer side receives.
[[nodiscard]] int walk_test_channel() {
    Pipe pipe{};
    const ::foundation::effects::detail::ctx_witnesses::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [self_head, peer_head] = s::mint_test_channel<Once>(ctx, SelfEnd{::fixy::to(pipe)}, PeerEnd{::fixy::to(pipe)});

    auto self_waits =
        std::move(self_head).send(Ping{7}, [](SelfEnd& e, Ping& p) noexcept { return put(e.pipe->to_peer, p.value); });
    auto [ping, peer_sends] =
        std::move(peer_head).recv([](PeerEnd& e) noexcept { return try_take_as<Ping>(e.pipe->to_peer); });
    auto peer_done = std::move(peer_sends).send(Pong{ping.value + 1}, [](PeerEnd& e, Pong& p) noexcept {
        return put(e.pipe->to_self, p.value);
    });
    auto [pong, self_done] =
        std::move(self_waits).recv([](SelfEnd& e) noexcept { return try_take_as<Pong>(e.pipe->to_self); });
    (void)std::move(peer_done).close();
    (void)std::move(self_done).close();
    if (pong.value != 8) {
        std::fprintf(stderr, "the test channel did not carry the reply\n");
        return 1;
    }
    return 0;
}

// The two ends of a test channel at End rewind together to the start of
// Once.  Each rewound end has a live record of its own, and the self end
// sends its Ping.  The detach of each end releases its record.
[[nodiscard]] int rewind_test_channel() {
    Pipe pipe{};
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [self_end, peer_end] = s::mint_test_channel<s::End>(ctx, SelfEnd{::fixy::to(pipe)}, PeerEnd{::fixy::to(pipe)});
    auto [self_again, peer_again] =
        s::HandleFactory::rewind_channel<Once, void>(std::move(self_end), std::move(peer_end));
    const bool is_each_end_live =
        s::watch::is_live(self_again.watch_endpoint()) && s::watch::is_live(peer_again.watch_endpoint());
    auto self_waits = std::move(self_again).send(Ping{20}, [](SelfEnd& e, Ping& p) noexcept {
        return put(e.pipe->to_peer, p.value);
    });
    const bool is_ping_sent = try_take(pipe.to_peer) == ::fixy::some(20);
    std::move(self_waits).detach(s::detach_reason::TestInstrumentation{});
    std::move(peer_again).detach(s::detach_reason::TestInstrumentation{});
    if (!is_each_end_live) {
        ::fixy::report(::fixy::Sink::Err, "a rewound end of the test channel has no live record\n");
        return 1;
    }
    if (!is_ping_sent) {
        ::fixy::report(::fixy::Sink::Err, "the rewound self end did not send its Ping\n");
        return 1;
    }
    return 0;
}

// The fork-shaped mint: each side runs on its own thread and never sees
// the other endpoint.
[[nodiscard]] int walk_forked_channel() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    Pipe pipe{};
    ::fixy::Atomic<int> seen_pong{0};
    const BgCtx ctx{::foundation::effects::testing::bg()};
    auto whole = ::foundation::permissions::mint_permission_root<channel_tags::Whole>();

    auto back = s::mint_forked_channel<Once, channel_tags::Self, channel_tags::Peer>(
        ctx, std::move(whole), SelfEnd{::fixy::to(pipe)}, PeerEnd{::fixy::to(pipe)},
        [&seen_pong](auto head, auto const& /*self_view*/, BgCtx const&) noexcept {
            auto waits = std::move(head).send(
                Ping{20}, [](SelfEnd& e, Ping& p) noexcept { return put(e.pipe->to_peer, p.value); });
            auto [pong, done] =
                std::move(waits).recv([](SelfEnd& e) noexcept { return try_take_as<Pong>(e.pipe->to_self); });
            seen_pong.store_release(pong.value);
            return std::move(done);
        },
        [](auto head, auto const& /*peer_view*/, BgCtx const&) noexcept {
            auto [ping, sends] =
                std::move(head).recv([](PeerEnd& e) noexcept { return try_take_as<Ping>(e.pipe->to_peer); });
            return std::move(sends).send(Pong{ping.value + 1},
                                         [](PeerEnd& e, Pong& p) noexcept { return put(e.pipe->to_self, p.value); });
        });
    ::foundation::linear::drop(std::move(back));

    if (seen_pong.load_acquire() != 21) {
        std::fprintf(stderr, "the forked channel did not carry the reply\n");
        return 1;
    }
    return 0;
}

// ── Runtime: the policy actually runs ────────────────────────────────
//
// Builds a handle at a non-terminal position and drops it.  Under
// Enforced the destructor prints and aborts; under Off it does nothing.
// Each case runs in a child process of the child door of
// fixy/os/Process.h: expect_dies of the Check family for an abort, and
// run_in_child with the exit code of the report for a child that must not
// abort.

template <typename Policy>
void abandon_a_handle() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    {
        auto handle = s::mint_session<Once, Policy>(ctx, ValueWire{});
        (void)handle;
    }
}

// Drops the handle that a step built.  A step gives no site, so the report of
// the drop reads the site of the mint from the record of the session in the
// watch, and it names no line of fixy/session/Handle.h.
void abandon_a_stepped_handle() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_session<Once>(ctx, ValueWire{});
    {
        auto stepped = std::move(head).send(Ping{1}, [](ValueWire&, Ping&) noexcept { return true; });
        static_cast<void>(stepped);
    }
}

constexpr ::fixy::TextView kAbandoned{"fixy::session: ABANDONMENT DETECTED (non-terminal handle)"};
constexpr ::fixy::TextView kSiteOfTheMint{"Construction at:  test/fixy/test_session_handle.cpp:"};

// Runs `body` in a child, and gives the report of its end.
template <typename Body>
[[nodiscard]] ::fixy::process::ChildReport run_in_child(::fixy::TestRunnerCtx const& test, Body body) {
    ::fixy::process::Child child = ::fixy::process::spawn_child(test, body).expect("the test starts a child");
    return ::fixy::process::wait(std::move(child)).expect("the test waits for its child");
}

void policy_runs(::fixy::TestRunnerCtx const& test) {
    // check::Enforced aborts on an abandoned handle, so the policy is not
    // only a name.
    ::fixy::check::expect_dies(test, abandon_a_handle<s::check::Enforced>, kAbandoned);
    // The report of the first handle and the report of a handle that a step
    // built name the site of the mint of the session.
    ::fixy::check::expect_dies(test, abandon_a_handle<s::check::Enforced>, kSiteOfTheMint);
    ::fixy::check::expect_dies(test, abandon_a_stepped_handle, kSiteOfTheMint);
    // check::Off does not check.
    CRUCIBLE_TEST_ASSERT(test, run_in_child(test, abandon_a_handle<s::check::Off>).is_exit_with(0));

    // Detaching with a reason tag retires the handle without advancing
    // it, and the check must then stay quiet under the checking policy.
    CRUCIBLE_TEST_ASSERT(test, run_in_child(test, [] {
                                   const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
                                   auto handle = s::mint_session<Once, s::check::Enforced>(ctx, ValueWire{});
                                   std::move(handle).detach(s::detach_reason::TestInstrumentation{});
                               }).is_exit_with(0));

    // Moving marks the SOURCE consumed.  Without that, every step
    // through a protocol would leave a moved-from handle behind whose
    // destructor reports an abandonment that did not happen.
    CRUCIBLE_TEST_ASSERT(test, run_in_child(test, [] {
                                   const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
                                   auto handle = s::mint_session<Once, s::check::Enforced>(ctx, ValueWire{});
                                   auto moved = std::move(handle);
                                   std::move(moved).detach(s::detach_reason::TestInstrumentation{});
                               }).is_exit_with(0));
    ::fixy::check::pass(test, "  policy_runs: PASSED\n");
}

// ── Runtime: the liveness check and check::Cancel ────────────────────

// A resource that can tell the peer that its side stops.  The counter
// stands in for the message a real transport would send.
::fixy::Atomic<int> g_cancellations{0};

struct CancelWire {
    int last_sent = 0;
};

void cancel_session(CancelWire&) noexcept { static_cast<void>(g_cancellations.fetch_add_acq_rel(1)); }

static_assert(s::CancellableResource<CancelWire>);
static_assert(!s::CancellableResource<ValueWire>);

// The exit code of a child that counted COUNT cancellations: 0 when the
// count is the expected one, and 3 when it is not.
[[nodiscard]] std::uint8_t exit_code_of_cancellations(int expected) noexcept {
    return g_cancellations.load_acquire() == expected ? std::uint8_t{0} : std::uint8_t{3};
}

void liveness_and_cancel_run(::fixy::TestRunnerCtx const& test) {
    // A consumed handle is dead.  The next operation on it aborts under
    // the default policy, so a moved-from handle cannot step the protocol
    // a second time.
    ::fixy::check::expect_dies(
        test,
        [] {
            const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
            auto handle = s::mint_session<Once>(ctx, ValueWire{});
            auto moved = std::move(handle);
            std::move(moved).detach(s::detach_reason::TestInstrumentation{});
            auto stepped = std::move(handle).send(Ping{1}, [](ValueWire&, Ping&) noexcept { return true; });
            std::move(stepped).detach(s::detach_reason::TestInstrumentation{});
        },
        ::fixy::TextView{"fixy::session: USE OF A CONSUMED HANDLE"});

    // An assignment over a live handle drops the protocol that the target
    // held, and the target's policy acts on the drop.
    ::fixy::check::expect_dies(
        test,
        [] {
            const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
            auto target = s::mint_session<Once>(ctx, ValueWire{});
            auto source = s::mint_session<Once>(ctx, ValueWire{});
            target = std::move(source);
            std::move(target).detach(s::detach_reason::TestInstrumentation{});
        },
        kAbandoned);

    // Under check::Cancel a dropped handle sends the cancellation and the
    // program continues.  The child counts from the count of the test
    // process, which sent no cancellation.
    CRUCIBLE_TEST_ASSERT(test, run_in_child(test, [&test] {
                                   const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
                                   {
                                       auto handle = s::mint_session<Once, s::check::Cancel>(ctx, CancelWire{});
                                       (void)handle;
                                   }
                                   ::fixy::process::exit_now(test, exit_code_of_cancellations(1));
                               }).is_exit_with(0));

    // An explicit cancel() sends it once, and the destructor then finds a
    // consumed handle and sends nothing more.
    CRUCIBLE_TEST_ASSERT(test, run_in_child(test, [&test] {
                                   const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
                                   {
                                       auto handle = s::mint_session<Once, s::check::Cancel>(ctx, CancelWire{});
                                       std::move(handle).cancel();
                                   }
                                   ::fixy::process::exit_now(test, exit_code_of_cancellations(1));
                               }).is_exit_with(0));

    // A handle that finishes its protocol sends no cancellation.
    CRUCIBLE_TEST_ASSERT(test, run_in_child(test, [&test] {
                                   const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
                                   auto handle = s::mint_session<Once, s::check::Cancel>(ctx, CancelWire{});
                                   auto after = std::move(handle).send(Ping{2}, [](CancelWire& w, Ping& p) noexcept {
                                       w.last_sent = p.value;
                                       return true;
                                   });
                                   auto [pong, at_end] = std::move(after).recv(
                                       [](CancelWire& w) noexcept { return ::fixy::some(Pong{w.last_sent}); });
                                   (void)pong;
                                   (void)std::move(at_end).close();
                                   ::fixy::process::exit_now(test, exit_code_of_cancellations(0));
                               }).is_exit_with(0));
    ::fixy::check::pass(test, "  liveness_and_cancel_run: PASSED\n");
}

// ── Runtime: the watch over live sessions ────────────────────────────
//
// Every mint under a checking policy claims a record, and every way a
// session ends gives it back: a step to End, detach, cancel.  check::Off
// claims nothing.  If a record stays live, the exit hook reports it, and
// this binary aborts when main returns.

[[nodiscard]] bool live_count_is(std::uint32_t expected, const char* after) noexcept {
    const std::uint32_t seen = s::watch::live_count();
    if (seen == expected) return true;
    std::fprintf(stderr, "the watch holds %u live session(s) after %s, not %u\n", seen, after, expected);
    return false;
}

[[nodiscard]] int watch_counts_sessions() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    const std::uint32_t baseline = s::watch::live_count();

    auto handle = s::mint_session<Once>(ctx, ValueWire{});
    if (!live_count_is(baseline + 1, "a mint")) return 1;
    auto waits = std::move(handle).send(Ping{1}, [](ValueWire& w, Ping& p) noexcept {
        w.last_sent = p.value;
        return true;
    });
    if (!live_count_is(baseline + 1, "a step that does not end")) return 1;
    auto [pong, at_end] = std::move(waits).recv([](ValueWire& w) noexcept { return ::fixy::some(Pong{w.last_sent}); });
    (void)pong;
    if (!live_count_is(baseline, "the step to End")) return 1;
    (void)std::move(at_end).close();

    auto detached = s::mint_session<Once>(ctx, ValueWire{});
    std::move(detached).detach(s::detach_reason::TestInstrumentation{});
    if (!live_count_is(baseline, "a detach")) return 1;

    auto cancelled = s::mint_session<Once, s::check::Cancel>(ctx, CancelWire{});
    std::move(cancelled).cancel();
    if (!live_count_is(baseline, "a cancel")) return 1;
    g_cancellations.store_release(0);

    {
        auto unchecked = s::mint_session<Once, s::check::Off>(ctx, ValueWire{});
        (void)unchecked;
        if (!live_count_is(baseline, "a mint under check::Off")) return 1;
    }

    // A moved handle keeps its record, and the move does not claim one.
    auto first = s::mint_session<Once>(ctx, ValueWire{});
    auto second = std::move(first);
    if (!live_count_is(baseline + 1, "a move")) return 1;
    std::move(second).detach(s::detach_reason::TestInstrumentation{});

    if (walk_test_channel() != 0 || !live_count_is(baseline, "a test channel")) return 1;
    if (rewind_test_channel() != 0 || !live_count_is(baseline, "a rewound test channel")) return 1;
    if (walk_forked_channel() != 0 || !live_count_is(baseline, "a forked channel")) return 1;
    return 0;
}

// A polling transport over a mailbox: an empty result while it is empty.
struct PollBox {
    Mailbox* (*box_of)(Pipe&) = nullptr;

    template <typename End>
    [[nodiscard]] ::fixy::Option<int> operator()(End& end) const noexcept {
        Mailbox& box = *box_of(*end.pipe);
        if (!box.full.load_acquire()) return ::fixy::none;
        const int value = box.value.load_acquire();
        box.full.store_release(false);
        return ::fixy::some(value);
    }
};

// A forked channel whose two sides wait through the watch, and whose
// peer answers late.  Each wait lasts longer than two checks of the
// deadlock detector, and the waits form no cycle, so the watch must not
// abort.  Under TSan this also checks the watch for races.
[[nodiscard]] int polling_channel_waits_without_a_false_deadlock() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    using Twice = s::Send<int, s::Recv<int, s::Send<int, s::Recv<int, s::End>>>>;
    constexpr auto late = std::chrono::milliseconds{120};
    Pipe pipe{};
    ::fixy::Atomic<int> total{0};
    const BgCtx ctx{::foundation::effects::testing::bg()};
    const PollBox to_self{[](Pipe& p) noexcept { return &p.to_self; }};
    const PollBox to_peer{[](Pipe& p) noexcept { return &p.to_peer; }};
    const auto send_to_peer = [](SelfEnd& e, int& v) noexcept { return put(e.pipe->to_peer, v); };
    const auto send_to_self = [](PeerEnd& e, int& v) noexcept { return put(e.pipe->to_self, v); };

    auto back = s::mint_forked_channel<Twice, channel_tags::Self, channel_tags::Peer>(
        ctx, ::foundation::permissions::mint_permission_root<channel_tags::Whole>(), SelfEnd{::fixy::to(pipe)},
        PeerEnd{::fixy::to(pipe)},
        [&](auto head, auto const& /*self_view*/, BgCtx const&) noexcept {
            auto first_wait = std::move(head).send(1, send_to_peer);
            auto [first_reply, second_send] = std::move(first_wait).recv(to_self);
            auto second_wait = std::move(second_send).send(first_reply + 1, send_to_peer);
            auto [second_reply, done] = std::move(second_wait).recv(to_self);
            total.store_release(second_reply);
            return std::move(done);
        },
        [&](auto head, auto const& /*peer_view*/, BgCtx const&) noexcept {
            auto [first, first_answer] = std::move(head).recv(to_peer);
            std::this_thread::sleep_for(late);
            auto second_wait = std::move(first_answer).send(first + 1, send_to_self);
            auto [second, second_answer] = std::move(second_wait).recv(to_peer);
            std::this_thread::sleep_for(late);
            return std::move(second_answer).send(second + 1, send_to_self);
        });
    ::foundation::linear::drop(std::move(back));
    if (total.load_acquire() != 4) {
        std::fprintf(stderr, "the polling channel carried %d, not 4\n", total.load_acquire());
        return 1;
    }
    return 0;
}

// ── Runtime: the four transport shapes ───────────────────────────────
//
// A trying write that finds no room returns false, and the handle waits
// through the watch and tries again.  A declared read takes the scope
// that the handle opened, and polls it while it waits.  The session is
// the only one the thread holds, so the watch admits each wait.

struct SlowWire {
    int value = 0;
    int refusals = 2;
};

[[nodiscard]] int each_transport_shape_waits_through_the_watch() {
    using Twice = s::Send<int, s::Recv<int, s::Send<int, s::Recv<int, s::End>>>>;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_session<Twice>(ctx, SlowWire{});
    int tries = 0;
    auto first_read = std::move(head).send(5, [&tries](SlowWire& w, int& v) noexcept {
        ++tries;
        if (w.refusals > 0) {
            --w.refusals;
            return false;
        }
        w.value = v;
        return true;
    });
    auto [first, second_write] = std::move(first_read).recv([](SlowWire& w, s::watch::wait_scope& wait) noexcept {
        wait.poll();
        return w.value;
    });
    auto second_read =
        std::move(second_write).send(first + 1, [](SlowWire& w, int&& v, s::watch::wait_scope&) noexcept {
            w.value = v;
        });
    auto [second, done] = std::move(second_read).recv([](SlowWire& w) noexcept { return ::fixy::some(w.value); });
    (void)std::move(done).close();
    if (tries != 3 || first != 5 || second != 6) {
        std::fprintf(stderr, "the transport shapes gave tries=%d first=%d second=%d, not 3, 5 and 6\n", tries, first,
                     second);
        return 1;
    }
    return 0;
}

// ── Runtime: the holder of a channel end ─────────────────────────────
//
// A channel mint claims the records of its two ends on the thread that
// makes the channel, and names no holder.  Each end's thread becomes its
// holder when it opens the end.  So the thread that made the channel
// holds neither end, and a wait of that thread on another session is not
// refused for the two records.

struct LateWire {
    int value = 5;
};

// A read that finds nothing on its first call and the value after it.
struct LateRead {
    int calls = 0;

    [[nodiscard]] ::fixy::Option<int> operator()(LateWire& wire) noexcept {
        if (calls++ == 0) return ::fixy::none;
        return ::fixy::some(wire.value);
    }
};

// The texts of the two claims below.  A claim takes its text as a template
// argument, so each text is an object of static storage.
constexpr ::foundation::core::TextView kFirstEndText{"first end"};
constexpr ::foundation::core::TextView kSecondEndText{"second end"};

[[nodiscard]] int channel_claim_names_no_holder() {
    namespace watch = s::watch;
    const ::foundation::core::Site site{};
    const watch::endpoint_id first =
        watch::claim<kFirstEndText>(site, watch::priority::lowest, watch::holder_on_claim::none_yet);
    const watch::endpoint_id second =
        watch::claim<kSecondEndText>(site, watch::priority::lowest, watch::holder_on_claim::none_yet);
    watch::link(first, second);
    const auto owner_of = [](watch::endpoint_id endpoint) noexcept {
        return watch::detail::record_at_(static_cast<std::uint32_t>(endpoint)).owner.load_acquire();
    };
    if (owner_of(first) != 0 || owner_of(second) != 0) {
        std::fprintf(stderr, "a channel claim named the claiming thread as the holder of an end\n");
        return 1;
    }

    // This thread waits on its own session while the two records live.
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto other = s::mint_session<s::Recv<int, s::End>>(ctx, LateWire{});
    auto [value, done] = std::move(other).recv(LateRead{});
    (void)std::move(done).close();
    if (value != 5) {
        std::fprintf(stderr, "the late read gave %d, not 5\n", value);
        return 1;
    }

    // The thread that opens an end holds it.
    std::uint64_t opener = 0;
    std::jthread open_first{[&] {
        static_cast<void>(watch::note_holder(first));
        opener = watch::detail::owner_token();
    }};
    open_first.join();
    const bool is_held_by_opener = owner_of(first) == opener && opener != 0;
    watch::release(first);
    watch::release(second);
    if (!is_held_by_opener) {
        std::fprintf(stderr, "the thread that opened an end is not its holder\n");
        return 1;
    }
    return 0;
}

// The record of a session keeps the site of the call of its mint: the file
// and the line of this test, and no place in a header of the session layer.
// The site is a constant, so the file of the record is a static text.
constexpr ::foundation::core::TextView kThisFile{"test_session_handle.cpp"};

[[nodiscard]] int record_keeps_the_site_of_the_mint() {
    namespace watch = s::watch;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    constexpr unsigned mint_line = __LINE__ + 1;
    auto head = s::mint_session<s::Recv<int, s::End>>(ctx, LateWire{});
    const watch::detail::endpoint_record& record =
        watch::detail::record_at_(static_cast<unsigned>(head.watch_endpoint()));
    const bool is_this_line = record.site_line.load_acquire() == mint_line;
    const bool is_this_file = record.site_file.load_acquire().match(
        [](::foundation::core::StaticText file) noexcept {
            const ::foundation::core::TextView text = file.view();
            if (text.size() < kThisFile.size()) return false;
            return text.window(text.size() - kThisFile.size(), kThisFile.size())
                .match([](::foundation::core::TextView tail) noexcept { return tail == kThisFile; },
                       []() noexcept { return false; });
        },
        []() noexcept { return false; });
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    if (!is_this_line || !is_this_file) {
        ::fixy::report(::fixy::Sink::Err, "the record of a session does not keep the site of the call of its mint\n");
        return 1;
    }
    return 0;
}

// A wait-for chain holds a fixed list of links, and each padding byte of a
// link costs one store for each link at each return of trace_chain
// (padding_bytes.h).
[[nodiscard]] int chain_link_has_no_padding_byte() {
    crucible::test::expect_no_padding_byte<^^s::watch::detail::chain_link>(kTest);
    return 0;
}

}  // namespace

int main() {
    if (const int rc = walk_once(); rc != 0) return rc;
    if (const int rc = walk_pinned_reference(); rc != 0) return rc;
    if (const int rc = walk_choice(); rc != 0) return rc;
    if (const int rc = walk_offer(); rc != 0) return rc;
    if (const int rc = walk_keyed_choice_in_another_order(); rc != 0) return rc;
    if (const int rc = view_a_position(); rc != 0) return rc;
    if (const int rc = walk_with_session(); rc != 0) return rc;
    if (const int rc = walk_receiving_loop(); rc != 0) return rc;
    if (const int rc = walk_loop_with_permission_set(); rc != 0) return rc;
    if (const int rc = move_token_through_session(); rc != 0) return rc;
    if (const int rc = walk_vendor_pinned_session(); rc != 0) return rc;
    if (const int rc = walk_vendor_pinned_loop(); rc != 0) return rc;
    if (const int rc = walk_test_channel(); rc != 0) return rc;
    if (const int rc = walk_forked_channel(); rc != 0) return rc;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    policy_runs(test);
    liveness_and_cancel_run(test);
    if (const int rc = watch_counts_sessions(); rc != 0) return rc;
    if (const int rc = polling_channel_waits_without_a_false_deadlock(); rc != 0) return rc;
    if (const int rc = channel_claim_names_no_holder(); rc != 0) return rc;
    if (const int rc = record_keeps_the_site_of_the_mint(); rc != 0) return rc;
    if (const int rc = each_transport_shape_waits_through_the_watch(); rc != 0) return rc;
    if (const int rc = chain_link_has_no_padding_byte(); rc != 0) return rc;
    return 0;
}
