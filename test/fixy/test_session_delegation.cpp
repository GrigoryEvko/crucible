// Delegation: a message that carries a live endpoint of a different
// session.
//
// The test hands an endpoint of an inner session over an outer test
// channel.  The recipient accepts it and walks the inner session to End,
// and End releases the one watch record that the inner session had from
// its mint.  It also checks the payload order, the refusal of a protocol
// that sends an endpoint to a peer of its own session, the delegation
// heads, and that no code builds a DelegatedSession other than the mint.

#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <fixy/session/Projection.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>

#include <atomic>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <type_traits>
#include <utility>

namespace test_session_delegation_types {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;
namespace eff = ::foundation::effects;

struct Ping {
    int value = 0;
};

// The inner session: one Ping and End, over a Wire that records it.
struct Wire {
    int sent = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Inner = s::Send<Ping, s::End>;
using Carried = s::DelegatedSession<Inner, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;

// The outer channel carries one DelegatedSession, in a slot that the test
// fills and empties on one thread.
template <class Payload>
struct SlotOf : ::foundation::Pinned<SlotOf<Payload>> {
    std::optional<Payload> held;
};
// A recipient that waits for an endpoint holds that endpoint only after the
// wait, so the outer channel takes the priority above the priority 0 of the
// endpoint that the slot holds.  The two ends of a channel have one priority.
template <class Payload>
struct SenderEndOf {
    static constexpr s::watch::priority session_priority{1};
    SlotOf<Payload>* slot = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
template <class Payload>
struct RecipientEndOf {
    static constexpr s::watch::priority session_priority{1};
    SlotOf<Payload>* slot = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Slot = SlotOf<Carried>;
using SenderEnd = SenderEndOf<Carried>;
using RecipientEnd = RecipientEndOf<Carried>;
using Outer = s::Send<Carried, s::End>;

// ── The payload order ────────────────────────────────────────────────
//
// The recipient runs code of the protocol that its type names.  Code
// that sends a positive int takes over an endpoint that must send an int,
// so an endpoint at Send<int> stands where one at Send<positive int> is
// expected, and not the reverse.
using SendsAny = s::Send<int, s::End>;
using SendsPositive = s::Send<::fixy::Refined<::fixy::positive, int>, s::End>;
template <typename P>
using At = s::DelegatedSession<P, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;
static_assert(s::is_subtype_sync_v<SendsPositive, SendsAny>);
static_assert(s::is_payload_subsort_v<At<SendsAny>, At<SendsPositive>>,
              "a delegated endpoint is contravariant in its protocol");
static_assert(!s::is_payload_subsort_v<At<SendsPositive>, At<SendsAny>>,
              "a delegated endpoint is not covariant in its protocol");
static_assert(s::is_subtype_sync_v<s::Send<At<SendsAny>, s::End>, s::Send<At<SendsPositive>, s::End>>);
static_assert(!s::is_subtype_sync_v<s::Send<At<SendsPositive>, s::End>, s::Send<At<SendsAny>, s::End>>);
struct OtherWire {};
static_assert(
    !s::is_payload_subsort_v<
        At<SendsAny>, s::DelegatedSession<SendsPositive, OtherWire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>>,
    "a different Resource is a different endpoint");

// ── A send to a peer of the delegated session ────────────────────────
struct Bob {};
struct Carol {};
struct Hand {};
struct Ask {};
using TalksToBob = s::Send<s::PeerMsg<Bob, Ask, int>, s::End>;
using HandsToBob = s::Send<s::PeerMsg<Bob, Hand, At<TalksToBob>>, s::End>;
using HandsToCarol = s::Send<s::PeerMsg<Carol, Hand, At<TalksToBob>>, s::End>;
static_assert(s::delegates_to_own_peer_v<HandsToBob>, "Bob would hold both ends of the inner session");
static_assert(!s::delegates_to_own_peer_v<HandsToCarol>);
static_assert(!s::WellFormedRunnableProtocol<HandsToBob> && s::WellFormedRunnableProtocol<HandsToCarol>);
// The Sender note of an inner Offer names a peer too.
using HearsFromCarol = s::Offer<s::Sender<Carol>, s::Recv<s::PeerMsg<Carol, Ask, int>, s::End>>;
static_assert(s::delegates_to_own_peer_v<s::Send<s::PeerMsg<Carol, Hand, At<HearsFromCarol>>, s::End>>);
// A receive names the sender, not the recipient, so it is no hand-off to
// a peer of the inner session.
static_assert(!s::delegates_to_own_peer_v<s::Recv<s::PeerMsg<Bob, Hand, At<TalksToBob>>, s::End>>);
// A plain protocol delegates nothing.
static_assert(!s::delegates_to_own_peer_v<Outer> && !s::delegates_to_own_peer_v<TalksToBob>);

// ── The delegation heads ─────────────────────────────────────────────
//
// A protocol type states a hand-off with Delegate and Accept.  The dual of
// a head keeps the delegated protocol as it is.  The delegated protocol is
// checked outside every Loop, composition leaves it alone, and no mint
// admits a protocol that holds a head.
using Hands = s::Delegate<Inner, s::End>;
using Takes = s::Accept<Inner, s::End>;
static_assert(std::is_same_v<s::dual_of_t<Hands>, Takes> && std::is_same_v<s::dual_of_t<Takes>, Hands>);
static_assert(
    std::is_same_v<s::dual_of_t<s::Delegate<Inner, s::Send<int, s::End>>>, s::Accept<Inner, s::Recv<int, s::End>>>,
    "the dual dualizes the continuation and keeps the delegated protocol");
static_assert(s::is_dual_v<Hands, Takes> && !s::is_dual_v<Hands, Hands>);
static_assert(s::DelegatesTo<Hands, Inner> && !s::DelegatesTo<Hands, SendsAny>);
static_assert(!s::DelegatesTo<Takes, Inner> && !s::DelegatesTo<Inner, Inner>);
static_assert(s::AcceptsFrom<Takes, Inner> && !s::AcceptsFrom<Hands, Inner> && !s::AcceptsFrom<Takes, SendsAny>);
static_assert(s::DelegatesTo<s::VendorPinned<s::VendorBackend::NV, Hands>, Inner>);
static_assert(s::is_well_formed_v<Hands> && s::is_well_formed_v<Takes>);
static_assert(s::is_well_formed_v<s::Loop<s::Delegate<Inner, s::Continue>>>);
static_assert(s::is_well_formed_v<s::Delegate<Hands, s::End>>, "a head can hand off a protocol that holds a head");
static_assert(!s::is_well_formed_v<s::Delegate<Inner, s::Continue>>, "a Continue with no Loop above it");
static_assert(!s::is_well_formed_v<s::Loop<s::Delegate<s::Send<int, s::Continue>, s::Continue>>>,
              "the delegated protocol cannot name a Loop of the carrier");
static_assert(!s::is_well_formed_v<s::Accept<s::Send<int, s::Continue>, s::End>>);
static_assert(!s::is_well_formed_v<s::Delegate<s::Stop, s::End>>, "a crashed endpoint is no design-time protocol");
static_assert(s::is_empty_choice_v<s::Delegate<s::Select<>, s::End>>
              && s::is_empty_choice_v<s::Accept<Inner, s::Offer<>>>);
static_assert(!s::is_empty_choice_v<Hands> && !s::is_well_formed_v<s::Delegate<s::Select<>, s::End>>);
static_assert(std::is_same_v<s::compose_t<Hands, s::Send<int, s::End>>, s::Delegate<Inner, s::Send<int, s::End>>>,
              "composition replaces the End of the continuation, never the End of the delegated protocol");
static_assert(!s::is_terminal_state_v<Hands> && !s::is_terminal_state_v<Takes>);
// The row of a protocol holds the rows of the protocol that a head hands
// off, as it does for the protocol that a DelegatedSession carries.
using SendsBlocking = s::Send<eff::Computation<eff::Row<eff::Effect::Block>, int>, s::End>;
static_assert(
    std::is_same_v<s::protocol_payload_row_t<s::Delegate<SendsBlocking, s::End>>, eff::Row<eff::Effect::Block>>);
static_assert(
    std::is_same_v<s::protocol_payload_row_t<s::Accept<SendsBlocking, s::End>>, eff::Row<eff::Effect::Block>>);
// A head refines only a head that hands off the same protocol.
static_assert(s::is_subtype_sync_v<Hands, Hands>);
static_assert(!s::is_subtype_sync_v<s::Delegate<SendsPositive, s::End>, s::Delegate<SendsAny, s::End>>);
static_assert(!s::is_subtype_sync_v<s::Delegate<SendsAny, s::End>, s::Delegate<SendsPositive, s::End>>);
// No handle has a step for a head, so no mint admits one.
template <typename P>
concept MintsOverWire = requires(::fixy::TestRunnerCtx const& ctx) { s::mint_session<P>(ctx, Wire{}); };
static_assert(!s::PermissionFlowCloses<Hands, fp::EmptyPermSet> && !s::PermissionFlowCloses<Takes, fp::EmptyPermSet>);
static_assert(MintsOverWire<Inner> && !MintsOverWire<Hands> && !MintsOverWire<s::Send<int, Takes>>);
// The crash-stop theory has no delegation.
static_assert(!s::is_crash_well_formed_v<Hands> && !s::is_crash_well_formed_v<Takes>);

// ── No code builds a DelegatedSession but the mint ───────────────────
static_assert(!std::is_copy_constructible_v<Carried> && std::is_move_constructible_v<Carried>);
static_assert(!std::is_copy_assignable_v<Carried>);
static_assert(!std::is_trivially_copyable_v<Carried> && !std::is_implicit_lifetime_v<Carried>);
static_assert(
    s::DelegatableHandle<decltype(s::mint_session<Inner>(std::declval<::fixy::TestRunnerCtx const&>(), Wire{}))>);
static_assert(!s::DelegatableHandle<int>);
static_assert(s::payload_conveys_delegation_v<Carried>);

// The endpoint of a hand-off keeps the tokens of its tags in its own slots,
// so the hand-off changes no set of the session that carries it.  The
// sender needs no tag of the endpoint, and a sender or a recipient whose set
// names a region of the endpoint would make a second claim on it.
struct HandedRegion {
    using permission_row = eff::Row<>;
};
using CarriesRegion = s::DelegatedSession<Inner, Wire, s::DefaultAbandonmentPolicy, fp::PermSet<HandedRegion>>;
static_assert(!s::PermissionFlowCloses<s::Send<CarriesRegion, s::End>, fp::PermSet<HandedRegion>>,
              "the sender and the endpoint that it sends would each claim the region");
static_assert(s::PermissionFlowCloses<s::Send<CarriesRegion, s::End>, fp::EmptyPermSet>,
              "the endpoint carries its own token, so the sender holds none");
static_assert(s::PermissionFlowCloses<s::Recv<CarriesRegion, s::End>, fp::EmptyPermSet>);
static_assert(!s::PermissionFlowCloses<s::Recv<CarriesRegion, s::End>, fp::PermSet<HandedRegion>>,
              "the recipient and the endpoint that it receives would each claim the region");

[[nodiscard]] static int fail(const char* what) {
    std::fprintf(stderr, "test_session_delegation: %s\n", what);
    return 1;
}

// The sender delegates the inner endpoint over the outer channel.  The
// recipient accepts it, sends the Ping, and closes it.
[[nodiscard]] static int delegate_over_a_channel() {
    const std::uint32_t live_before = s::watch::live_count();
    Slot slot{};
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [sender, recipient] = s::mint_test_channel<Outer>(ctx, SenderEnd{&slot}, RecipientEnd{&slot});

    auto inner = s::mint_session<Inner>(ctx, Wire{});
    Carried carried = s::mint_delegated_session(std::move(inner));
    if (!carried.holds_endpoint()) return fail("the payload does not hold the endpoint");

    auto sender_done = std::move(sender).send(std::move(carried), [](SenderEnd& end, Carried& value) noexcept {
        if (end.slot->held.has_value()) return false;
        end.slot->held.emplace(std::move(value));
        return true;
    });
    auto [received, recipient_done] =
        std::move(recipient).recv([](RecipientEnd& end) noexcept -> ::fixy::Option<Carried> {
            if (!end.slot->held.has_value()) return ::fixy::none;
            ::fixy::Option<Carried> taken = ::fixy::some(std::move(*end.slot->held));
            end.slot->held.reset();
            return taken;
        });
    static_cast<void>(std::move(sender_done).close());
    static_cast<void>(std::move(recipient_done).close());

    auto accepted = std::move(received).accept();
    auto at_end = std::move(accepted).send(Ping{5}, [](Wire& wire, Ping& ping) noexcept {
        wire.sent = ping.value;
        return true;
    });
    const Wire back = std::move(at_end).close();
    if (back.sent != 5) return fail("the accepted handle did not step the delegated Resource");
    if (s::watch::live_count() != live_before) return fail("a session record stayed live after every End");
    return 0;
}

// ── A move and an assignment take the endpoint ───────────────────────
//
// A move empties the source.  An assignment into an empty
// DelegatedSession takes the endpoint of the source and empties it.
// Each accepted handle then steps its own Resource to End.
static_assert(std::is_nothrow_move_constructible_v<Carried> && std::is_nothrow_move_assignable_v<Carried>);

[[nodiscard]] static int move_and_assign_take_the_endpoint() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    Carried first = s::mint_delegated_session(s::mint_session<Inner>(ctx, Wire{1}));
    Carried second = s::mint_delegated_session(s::mint_session<Inner>(ctx, Wire{2}));
    Carried drained = std::move(first);
    if (first.holds_endpoint() || !drained.holds_endpoint()) return fail("a move did not take the endpoint");
    first = std::move(second);
    if (!first.holds_endpoint() || second.holds_endpoint()) return fail("an assignment did not take the endpoint");
    int sent_total = 0;
    for (Carried* parcel : {&drained, &first}) {
        auto at_end = std::move(*parcel).accept().send(Ping{10}, [](Wire& wire, Ping& ping) noexcept {
            wire.sent += ping.value;
            return true;
        });
        sent_total += std::move(at_end).close().sent;
    }
    if (sent_total != 23) return fail("the accepted handles did not step the Resources that the parcels held");
    return 0;
}

// ── The tokens travel with the endpoint ──────────────────────────────
//
// An endpoint whose permission set holds a tag keeps the token of that tag
// in its slots, so it is delegated alone.  The recipient gets the token with
// the handle that claims its region, and it sends the region with it.
struct Region {
    using permission_row = eff::Row<>;
};
using SendsRegion = s::Send<s::Transferable<int, Region>, s::End>;

template <typename H>
concept Delegates = requires(H handle) { s::mint_delegated_session(std::move(handle)); };

[[nodiscard]] static int tokens_travel_with_the_endpoint() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_permissioned_session<SendsRegion>(ctx, Wire{}, fp::mint_permission_root<Region>());
    static_assert(Delegates<decltype(handle)>, "a handle whose set holds a tag carries its token in its slots");

    auto parcel = s::mint_delegated_session(std::move(handle));
    static_assert(std::is_same_v<typename decltype(parcel)::inner_perm_set, fp::PermSet<Region>>);
    auto received = std::move(parcel).accept();
    static_assert(std::is_same_v<typename decltype(received)::perm_set, fp::PermSet<Region>>);
    auto at_end = std::move(received).send(9, [](Wire& wire, s::Transferable<int, Region>& sent) noexcept {
        wire.sent = sent.peek();
        return true;
    });
    static_assert(std::is_same_v<typename decltype(at_end)::perm_set, fp::EmptyPermSet>);
    const Wire back = std::move(at_end).close();
    if (back.sent != 9) return fail("the recipient did not send with the token that came with the endpoint");
    return 0;
}

// ── A sender with no tag hands off an endpoint that holds one ────────
//
// The outer session starts with no tag.  It sends an endpoint whose slots
// hold the token of Region, and its own set stays empty.  The recipient
// accepts the endpoint, and it holds the region through it: its read of the
// region goes through the token that the endpoint sends.
using HandsRegion =
    s::Send<s::DelegatedSession<SendsRegion, Wire, s::DefaultAbandonmentPolicy, fp::PermSet<Region>>, s::End>;
using ParcelOfRegion = s::DelegatedSession<SendsRegion, Wire, s::DefaultAbandonmentPolicy, fp::PermSet<Region>>;

// The sender puts the endpoint into the slot, and the recipient takes it
// out, as in delegate_over_a_channel.
[[nodiscard]] static bool put_parcel(SenderEndOf<ParcelOfRegion>& end, ParcelOfRegion& parcel) noexcept {
    if (end.slot->held.has_value()) return false;
    end.slot->held.emplace(std::move(parcel));
    return true;
}

// The read waits in its declared form, so it gives the endpoint with no
// Option around it.  The sender fills the slot on the same thread before the
// read, so the read does not wait.
[[nodiscard]] static ParcelOfRegion take_parcel(RecipientEndOf<ParcelOfRegion>& end, s::watch::wait_scope&) noexcept {
    if (!end.slot->held.has_value()) ::fixy::fatal("the sender put no endpoint into the slot");
    ParcelOfRegion taken = std::move(*end.slot->held);
    end.slot->held.reset();
    return taken;
}

[[nodiscard]] static int sender_without_tags_hands_off_a_region() {
    SlotOf<ParcelOfRegion> slot{};
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [sender, recipient] = s::mint_test_channel<HandsRegion>(ctx, SenderEndOf<ParcelOfRegion>{&slot},
                                                                 RecipientEndOf<ParcelOfRegion>{&slot});
    static_assert(std::is_same_v<typename decltype(sender)::perm_set, fp::EmptyPermSet>);
    auto inner = s::mint_permissioned_session<SendsRegion>(ctx, Wire{}, fp::mint_permission_root<Region>());
    auto sender_done = std::move(sender).send(s::mint_delegated_session(std::move(inner)), put_parcel);
    static_assert(std::is_same_v<typename decltype(sender_done)::perm_set, fp::EmptyPermSet>,
                  "the sender holds no token of the endpoint, before the send and after it");
    // The sender closes before the recipient waits, so the waiting thread
    // holds no peer of the endpoint that it waits on.
    static_cast<void>(std::move(sender_done).close());
    auto [received, recipient_done] = std::move(recipient).recv(take_parcel);
    static_assert(std::is_same_v<typename decltype(recipient_done)::perm_set, fp::EmptyPermSet>,
                  "the recipient holds the region through the endpoint, and not in its own set");
    static_cast<void>(std::move(recipient_done).close());
    if (slot.held.has_value()) return fail("the recipient left the endpoint in the slot");

    auto accepted = std::move(received).accept();
    static_assert(std::is_same_v<typename decltype(accepted)::perm_set, fp::PermSet<Region>>,
                  "the endpoint keeps the token of the region in its slots");
    auto at_end = std::move(accepted).send(11, [](Wire& wire, s::Transferable<int, Region>& sent) noexcept {
        wire.sent = sent.peek();
        return true;
    });
    const Wire back = std::move(at_end).close();
    if (back.sent != 11) return fail("the recipient of the endpoint did not use the region that it holds");
    return 0;
}

}  // namespace test_session_delegation_types

int main() {
    if (const int rc = test_session_delegation_types::delegate_over_a_channel(); rc != 0) return rc;
    if (const int rc = test_session_delegation_types::move_and_assign_take_the_endpoint(); rc != 0) return rc;
    if (const int rc = test_session_delegation_types::tokens_travel_with_the_endpoint(); rc != 0) return rc;
    if (const int rc = test_session_delegation_types::sender_without_tags_hands_off_a_region(); rc != 0) return rc;
    return 0;
}
