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
// acts on that claim is runtime, and the two can disagree silently:
// the ported source had two independent kill switches for one
// decision, a tracker that returned a hardcoded "consumed" and a
// destructor body inside `#ifndef NDEBUG`.  Either one alone flipped
// leaves a build whose constant says "checking" and whose destructor
// does nothing, and no static_assert can see that.  The only way to
// observe a std::abort is to run it somewhere the failure is
// recoverable, which is a child process.  Reading WIFSIGNALED from the
// wait status is also stricter than ctest's WILL_FAIL, which would
// accept any non-zero exit.

#include <fixy/ScopedView.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>

#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

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
struct foundation::permissions::splits_into_pack<channel_tags::Whole, channel_tags::Self, channel_tags::Peer>
    : std::true_type {};
template <>
struct foundation::permissions::splits_into_pack_authoring_witness<channel_tags::Whole, channel_tags::Self,
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
using NoPerms = ::foundation::permissions::EmptyPermSet;
using HoldsRegion = ::foundation::permissions::PermSet<Region>;

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
static_assert(
    std::is_same_v<s::detail::loop_entry_perm_set_t<s::detail::PermLoopFrame<s::Loop<s::End>, HoldsRegion>>,
                   HoldsRegion>);

// The delta of one message comes from fixy/session/Payload.h.  A send of
// a token takes its region from the set, and the matching receive adds
// it.  A sender that does not hold the region cannot send the token, and
// a receiver that holds it already cannot receive a second owner.  End
// refuses an open loan and admits an owned tag.
using Token = ::foundation::permissions::Permission<Region>;
using LendsRegion = ::foundation::permissions::PermSet<s::LentOut<Region>>;
static_assert(
    ::foundation::permissions::perm_set_equal_v<s::detail::perm_set_after_send_t<HoldsRegion, Token>, NoPerms>);
static_assert(
    ::foundation::permissions::perm_set_equal_v<s::detail::perm_set_after_recv_t<NoPerms, Token>, HoldsRegion>);
static_assert(::foundation::permissions::perm_set_equal_v<s::detail::perm_set_after_send_t<NoPerms, Ping>, NoPerms>);
static_assert(s::detail::handle_admits_send_v<HoldsRegion, Token>);
static_assert(!s::detail::handle_admits_send_v<NoPerms, Token>);
static_assert(s::detail::handle_admits_recv_v<NoPerms, Token>);
static_assert(!s::detail::handle_admits_recv_v<HoldsRegion, Token>);
static_assert(s::detail::perm_set_admits_close_v<HoldsRegion>);
static_assert(!s::detail::perm_set_admits_close_v<LendsRegion>);

// ── Where a step lands ───────────────────────────────────────────────

using Once = s::Send<Ping, s::Recv<Pong, s::End>>;
using Looping = s::Loop<s::Send<Ping, s::Continue>>;

// Minting a Loop unrolls one iteration: the handle sits at the body's
// head and carries the Loop as its context, which is what binds the
// Continue inside it.
using LoopHead = decltype(s::mint_session_handle<Looping, ValueWire>(std::declval<ValueWire>()));
static_assert(std::is_same_v<typename LoopHead::protocol, s::Send<Ping, s::Continue>>);
static_assert(std::is_same_v<typename LoopHead::loop_ctx, Looping>);

// A non-Loop head passes through with a void context.
using OnceHead = decltype(s::mint_session_handle<Once, ValueWire>(std::declval<ValueWire>()));
static_assert(std::is_same_v<typename OnceHead::protocol, Once>);
static_assert(std::is_same_v<typename OnceHead::loop_ctx, void>);

// A reference to a Pinned resource is admitted; the handle stores the
// reference and the caller keeps the object alive.
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
    auto handle = s::mint_session_handle<Once, ValueWire>(ValueWire{});

    auto after_send = std::move(handle).send(Ping{3}, [](ValueWire& w, Ping&& p) noexcept { w.last_sent = p.value; });
    static_assert(std::is_same_v<typename decltype(after_send)::protocol, s::Recv<Pong, s::End>>);

    auto [pong, at_end] = std::move(after_send).recv([](ValueWire& w) noexcept { return Pong{w.last_sent}; });
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
// asserted.  The frozen tree admitted this Resource shape and then
// could not mint one: the factory took `Resource r` and passed
// `std::move(r)`, which for Resource = Wire& is an rvalue that cannot
// bind back to Wire&, so the branch the concept's longest comment
// exists to explain failed at the first call.  Forwarding instead of
// moving is what makes this function compile, so this function is the
// regression test for that repair.
[[nodiscard]] int walk_pinned_reference() {
    PinnedWire wire{};
    const PinnedWire* const address_before = &wire;

    auto handle = s::mint_session_handle<s::Send<Ping, s::End>, PinnedWire&>(wire);
    auto at_end = std::move(handle).send(Ping{11}, [](PinnedWire& w, Ping&& p) noexcept { w.last_sent = p.value; });
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
    auto handle = s::mint_session_handle<Choice, ValueWire>(ValueWire{});

    // select<I> tells the peer which branch was taken; select_local<I>
    // deliberately does not, and the two names are what keep that
    // difference visible at the call site.
    std::size_t signalled = 99;
    auto chosen =
        std::move(handle).select<1>([&signalled](ValueWire&, std::size_t label) noexcept { signalled = label; });
    if (signalled != 1) {
        std::fprintf(stderr, "select did not signal the branch index\n");
        return 1;
    }
    static_assert(std::is_same_v<typename decltype(chosen)::protocol, s::Send<Stop, s::End>>);

    auto at_end = std::move(chosen).send(Stop{}, [](ValueWire&, Stop&&) noexcept {});
    (void)std::move(at_end).close();
    return 0;
}

[[nodiscard]] int walk_offer() {
    using Served = s::Offer<s::Recv<Ping, s::End>, s::Recv<Stop, s::End>>;
    auto handle = s::mint_session_handle<Served, ValueWire>(ValueWire{});

    // The handler runs once, for the branch the peer's label names.
    const int taken = std::move(handle).branch([](ValueWire&) noexcept -> std::size_t { return 0; },
                                               [](auto branch_handle) {
                                                   using B = typename decltype(branch_handle)::protocol;
                                                   auto [msg, at_end] =
                                                       std::move(branch_handle).recv([](ValueWire&) noexcept {
                                                           return typename decltype(branch_handle)::message_type{};
                                                       });
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
    auto handle = s::mint_session_handle<s::Send<Ping, s::End>, ValueWire>(ValueWire{});
    {
        const auto view = ::fixy::mint_view<s::position::AtSend>(handle);
        if (view.carrier().protocol_name() != AtSend::protocol_name()) {
            std::fprintf(stderr, "the view does not look at the handle it was minted from\n");
            return 1;
        }
    }
    auto at_end = std::move(handle).send(Ping{9}, [](ValueWire& w, Ping&& p) noexcept { w.last_sent = p.value; });
    (void)std::move(at_end).close();
    return 0;
}

// ── Runtime: the callback entry point ────────────────────────────────
//
// The library builds the handle and closes the one the body returns.
// Every handle of the session carries the body's brand, which is how the
// entry point knows that the End handle is the one it gave out.
[[nodiscard]] int walk_with_session() {
    const ValueWire back = s::with_session<Once>(ValueWire{}, [](auto head) noexcept {
        using Brand = typename s::detail::session_brand_of<typename decltype(head)::loop_ctx>::type;
        static_assert(!std::is_void_v<Brand>, "a handle of an owned session carries the brand of its body");
        auto after = std::move(head).send(Ping{5}, [](ValueWire& w, Ping&& p) noexcept { w.last_sent = p.value; });
        auto [pong, at_end] = std::move(after).recv([](ValueWire& w) noexcept { return Pong{w.last_sent}; });
        (void)pong;
        return std::move(at_end);
    });
    if (back.last_sent != 5) {
        std::fprintf(stderr, "with_session returned a resource that lost the protocol's effect\n");
        return 1;
    }
    return 0;
}

// ── Runtime: a loop with a permission set ────────────────────────────
//
// A handle that holds a permission walks a loop three times.  Each
// Continue checks that the iteration left the set as the loop found it.
// The handle is built through the internal factory that a token-
// consuming mint calls, because no public mint builds a non-empty set.
[[nodiscard]] int walk_loop_with_permission_set() {
    using Forever = s::Loop<s::Send<Ping, s::Continue>>;
    auto head = s::detail::open_session_<Forever, ValueWire, s::check::Enforced, HoldsRegion>(
        ValueWire{}, std::source_location::current());
    static_assert(std::is_same_v<typename decltype(head)::perm_set, HoldsRegion>);
    static_assert(std::is_same_v<typename decltype(head)::loop_ctx, s::detail::PermLoopFrame<Forever, HoldsRegion>>);
    for (int round = 0; round < 3; ++round) {
        auto next = std::move(head).send(Ping{round}, [](ValueWire& w, Ping&& p) noexcept { w.last_sent = p.value; });
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

// ── Runtime: a token moves through a session ─────────────────────────
//
// The sender holds Region and sends its token, so its End handle holds
// nothing.  The receiver starts with nothing and receives the token, so
// its End handle holds Region.  The slot is the wire between them.

struct TokenWire {
    std::optional<Token>* slot = nullptr;
};

[[nodiscard]] int move_token_through_session() {
    std::optional<Token> slot;
    auto sender = s::detail::open_session_<s::Send<Token, s::End>, TokenWire, s::check::Enforced, HoldsRegion>(
        TokenWire{&slot}, std::source_location::current());
    auto sender_done = std::move(sender).send(::foundation::permissions::mint_permission_root<Region>(),
                                              [](TokenWire& w, Token&& token) noexcept { w.slot->emplace(std::move(token)); });
    static_assert(std::is_same_v<typename decltype(sender_done)::perm_set, NoPerms>);
    (void)std::move(sender_done).close();

    auto receiver = s::mint_session_handle<s::Recv<Token, s::End>>(TokenWire{&slot});
    auto [token, receiver_done] = std::move(receiver).recv([](TokenWire& w) noexcept {
        std::optional<Token>& wire_slot = *w.slot;
        Token taken = std::move(*wire_slot);
        wire_slot.reset();
        return taken;
    });
    static_assert(::foundation::permissions::perm_set_equal_v<typename decltype(receiver_done)::perm_set, HoldsRegion>);
    (void)std::move(receiver_done).close();
    if (slot.has_value()) {
        std::fprintf(stderr, "the receive left the token on the wire\n");
        return 1;
    }
    ::foundation::permissions::permission_drop(std::move(token));
    return 0;
}

// ── Runtime: the two channel mints ───────────────────────────────────
//
// A one-slot mailbox in each direction.  The two endpoints hold a pointer
// to the same pipe, so each side of the protocol reads what the other
// side wrote.  Every wait has a deadline, so a bug here aborts with a
// diagnostic and does not hang the test run.

struct Mailbox {
    std::atomic<int> value{0};
    std::atomic<bool> full{false};
};

struct Pipe : ::foundation::Pinned<Pipe> {
    Mailbox to_peer;
    Mailbox to_self;
};

struct SelfEnd {
    Pipe* pipe = nullptr;
};
struct PeerEnd {
    Pipe* pipe = nullptr;
};

constexpr auto kWaitDeadline = std::chrono::seconds{10};

void put(Mailbox& box, int value) noexcept {
    box.value.store(value, std::memory_order_relaxed);
    box.full.store(true, std::memory_order_release);
}

[[nodiscard]] int take(Mailbox& box) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + kWaitDeadline;
    while (!box.full.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::fprintf(stderr, "watchdog: a mailbox wait passed its deadline; the two sides are out of step\n");
            std::abort();
        }
        std::this_thread::yield();
    }
    const int value = box.value.load(std::memory_order_relaxed);
    box.full.store(false, std::memory_order_release);
    return value;
}

// The two endpoints on one thread, through the test hatch.  The order is
// fixed: the self side sends before the peer side receives.
[[nodiscard]] int walk_test_channel() {
    Pipe pipe{};
    const ::foundation::effects::detail::ctx_witnesses::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [self_head, peer_head] = s::mint_test_channel<Once>(ctx, SelfEnd{&pipe}, PeerEnd{&pipe});

    auto self_waits = std::move(self_head).send(Ping{7}, [](SelfEnd& e, Ping&& p) noexcept { put(e.pipe->to_peer, p.value); });
    auto [ping, peer_sends] = std::move(peer_head).recv([](PeerEnd& e) noexcept { return Ping{take(e.pipe->to_peer)}; });
    auto peer_done = std::move(peer_sends).send(Pong{ping.value + 1},
                                                [](PeerEnd& e, Pong&& p) noexcept { put(e.pipe->to_self, p.value); });
    auto [pong, self_done] = std::move(self_waits).recv([](SelfEnd& e) noexcept { return Pong{take(e.pipe->to_self)}; });
    (void)std::move(peer_done).close();
    (void)std::move(self_done).close();
    if (pong.value != 8) {
        std::fprintf(stderr, "the test channel did not carry the reply\n");
        return 1;
    }
    return 0;
}

// The fork-shaped mint: each side runs on its own thread and never sees
// the other endpoint.
[[nodiscard]] int walk_forked_channel() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    Pipe pipe{};
    std::atomic<int> seen_pong{0};
    const BgCtx ctx{::foundation::effects::testing::bg()};
    auto whole = ::foundation::permissions::mint_permission_root<channel_tags::Whole>();

    auto back = s::mint_forked_channel<Once, channel_tags::Self, channel_tags::Peer>(
        ctx, std::move(whole), SelfEnd{&pipe}, PeerEnd{&pipe},
        [&seen_pong](auto head, ::foundation::permissions::Permission<channel_tags::Self>, BgCtx const&) noexcept {
            auto waits = std::move(head).send(Ping{20}, [](SelfEnd& e, Ping&& p) noexcept { put(e.pipe->to_peer, p.value); });
            auto [pong, done] = std::move(waits).recv([](SelfEnd& e) noexcept { return Pong{take(e.pipe->to_self)}; });
            seen_pong.store(pong.value, std::memory_order_release);
            return std::move(done);
        },
        [](auto head, ::foundation::permissions::Permission<channel_tags::Peer>, BgCtx const&) noexcept {
            auto [ping, sends] = std::move(head).recv([](PeerEnd& e) noexcept { return Ping{take(e.pipe->to_peer)}; });
            return std::move(sends).send(Pong{ping.value + 1},
                                         [](PeerEnd& e, Pong&& p) noexcept { put(e.pipe->to_self, p.value); });
        });
    ::foundation::permissions::permission_drop(std::move(back));

    if (seen_pong.load(std::memory_order_acquire) != 21) {
        std::fprintf(stderr, "the forked channel did not carry the reply\n");
        return 1;
    }
    return 0;
}

// ── Runtime: the policy actually runs ────────────────────────────────
//
// Builds a handle at a non-terminal position and drops it.  Under
// Enforced the destructor prints and aborts; under Off it does nothing.
// The caller runs this in a child process and reads the exit status.

template <typename Policy>
[[noreturn]] void abandon_a_handle() {
    {
        auto handle = s::mint_session_handle<Once, ValueWire, Policy>(ValueWire{});
        (void)handle;
    }
    // Reached only when the policy does not check.
    std::_Exit(0);
}

// Runs `body` in a child and reports whether the child died on a
// signal, which is how std::abort() exits.
template <typename Body>
[[nodiscard]] bool child_aborted(Body body) {
    // SPAWN-PROCESS-OK: the child exists to die.  The abandonment abort is a
    // runtime behaviour of a destructor, and std::abort ends the process
    // that runs it, so the only way to observe it is from a parent.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, see above
    if (pid < 0) {
        std::fprintf(stderr, "fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        body();
        std::_Exit(0);
    }
    int status = 0;
    // SPAWN-PROCESS-OK: the wait belongs to the fork above; reading
    // WIFSIGNALED from the status is what makes the abort observable,
    // and is stricter than ctest's WILL_FAIL, which accepts any exit.
    if (::waitpid(pid, &status, 0) != pid) {  // SPAWN-PROCESS-OK: death test, see above
        std::fprintf(stderr, "waitpid failed\n");
        std::_Exit(2);
    }
    return WIFSIGNALED(status) != 0;
}

[[nodiscard]] int policy_runs() {
    // The abandonment diagnostic is the child's stderr, and a passing
    // run prints it once.  Saying so here keeps a reader from taking
    // the banner for a failure.
    std::fprintf(stderr, "[expected] the abandonment banner below comes from the child process "
                         "that proves check::Enforced aborts\n");

    if (!child_aborted(abandon_a_handle<s::check::Enforced>)) {
        std::fprintf(stderr, "check::Enforced did not abort on an abandoned handle — the policy is named but "
                             "does nothing\n");
        return 1;
    }
    if (child_aborted(abandon_a_handle<s::check::Off>)) {
        std::fprintf(stderr, "check::Off aborted — the unchecked policy is not unchecked\n");
        return 1;
    }

    // Detaching with a reason tag retires the handle without advancing
    // it, and the check must then stay quiet under the checking policy.
    const bool detach_aborted = child_aborted([] {
        auto handle = s::mint_session_handle<Once, ValueWire, s::check::Enforced>(ValueWire{});
        std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    });
    if (detach_aborted) {
        std::fprintf(stderr, "a detached handle still aborted — detach does not mark the handle consumed\n");
        return 1;
    }

    // Moving marks the SOURCE consumed.  Without that, every step
    // through a protocol would leave a moved-from handle behind whose
    // destructor reports an abandonment that did not happen.
    const bool move_aborted = child_aborted([] {
        auto handle = s::mint_session_handle<Once, ValueWire, s::check::Enforced>(ValueWire{});
        auto moved = std::move(handle);
        std::move(moved).detach(s::detach_reason::TestInstrumentation{});
    });
    if (move_aborted) {
        std::fprintf(stderr, "a moved-from handle aborted — the move did not mark the source consumed\n");
        return 1;
    }
    return 0;
}

// ── Runtime: the liveness check and check::Cancel ────────────────────

// A resource that can tell the peer that its side stops.  The counter
// stands in for the message a real transport would send.
std::atomic<int> g_cancellations{0};

struct CancelWire {
    int last_sent = 0;
};

void cancel_session(CancelWire&) noexcept { g_cancellations.fetch_add(1, std::memory_order_relaxed); }

static_assert(s::CancellableResource<CancelWire>);
static_assert(!s::CancellableResource<ValueWire>);

// Runs `body` in a child and returns the child's exit code, or -1 when a
// signal ended the child.
template <typename Body>
[[nodiscard]] int child_exit_code(Body body) {
    // SPAWN-PROCESS-OK: the child exists to run a destructor whose effect
    // the parent must observe.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, see above
    if (pid < 0) {
        std::fprintf(stderr, "fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        body();
        std::_Exit(0);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {  // SPAWN-PROCESS-OK: death test, see above
        std::fprintf(stderr, "waitpid failed\n");
        std::_Exit(2);
    }
    return WIFEXITED(status) != 0 ? WEXITSTATUS(status) : -1;
}

[[nodiscard]] int liveness_and_cancel_run() {
    std::fprintf(stderr, "[expected] the diagnostics below come from child processes that prove the liveness "
                         "check and the move-assignment guard abort\n");

    // A consumed handle is dead.  The next operation on it aborts under
    // the default policy, so a moved-from handle cannot step the protocol
    // a second time.
    const bool use_after_move_aborted = child_aborted([] {
        auto handle = s::mint_session_handle<Once, ValueWire>(ValueWire{});
        auto moved = std::move(handle);
        std::move(moved).detach(s::detach_reason::TestInstrumentation{});
        auto stepped = std::move(handle).send(Ping{1}, [](ValueWire&, Ping&&) noexcept {});
        std::move(stepped).detach(s::detach_reason::TestInstrumentation{});
    });
    if (!use_after_move_aborted) {
        std::fprintf(stderr, "a moved-from handle sent a message — the liveness check does not run\n");
        return 1;
    }

    // An assignment over a live handle drops the protocol that the target
    // held, and the target's policy acts on the drop.
    const bool assign_over_live_aborted = child_aborted([] {
        auto target = s::mint_session_handle<Once, ValueWire>(ValueWire{});
        auto source = s::mint_session_handle<Once, ValueWire>(ValueWire{});
        target = std::move(source);
        std::move(target).detach(s::detach_reason::TestInstrumentation{});
    });
    if (!assign_over_live_aborted) {
        std::fprintf(stderr, "an assignment over a live handle dropped its protocol silently\n");
        return 1;
    }

    // Under check::Cancel a dropped handle sends the cancellation and the
    // program continues.
    const int cancel_on_drop = child_exit_code([] {
        {
            auto handle = s::mint_session_handle<Once, CancelWire, s::check::Cancel>(CancelWire{});
            (void)handle;
        }
        std::_Exit(g_cancellations.load(std::memory_order_relaxed) == 1 ? 0 : 3);
    });
    if (cancel_on_drop != 0) {
        std::fprintf(stderr, "check::Cancel did not send exactly one cancellation on a drop (exit %d)\n", cancel_on_drop);
        return 1;
    }

    // An explicit cancel() sends it once, and the destructor then finds a
    // consumed handle and sends nothing more.
    const int explicit_cancel = child_exit_code([] {
        {
            auto handle = s::mint_session_handle<Once, CancelWire, s::check::Cancel>(CancelWire{});
            std::move(handle).cancel();
        }
        std::_Exit(g_cancellations.load(std::memory_order_relaxed) == 1 ? 0 : 3);
    });
    if (explicit_cancel != 0) {
        std::fprintf(stderr, "cancel() did not send exactly one cancellation (exit %d)\n", explicit_cancel);
        return 1;
    }

    // A handle that finishes its protocol sends no cancellation.
    const int finished = child_exit_code([] {
        auto handle = s::mint_session_handle<Once, CancelWire, s::check::Cancel>(CancelWire{});
        auto after = std::move(handle).send(Ping{2}, [](CancelWire& w, Ping&& p) noexcept { w.last_sent = p.value; });
        auto [pong, at_end] = std::move(after).recv([](CancelWire& w) noexcept { return Pong{w.last_sent}; });
        (void)pong;
        (void)std::move(at_end).close();
        std::_Exit(g_cancellations.load(std::memory_order_relaxed) == 0 ? 0 : 3);
    });
    if (finished != 0) {
        std::fprintf(stderr, "a finished session sent a cancellation (exit %d)\n", finished);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = walk_once(); rc != 0) return rc;
    if (const int rc = walk_pinned_reference(); rc != 0) return rc;
    if (const int rc = walk_choice(); rc != 0) return rc;
    if (const int rc = walk_offer(); rc != 0) return rc;
    if (const int rc = view_a_position(); rc != 0) return rc;
    if (const int rc = walk_with_session(); rc != 0) return rc;
    if (const int rc = walk_loop_with_permission_set(); rc != 0) return rc;
    if (const int rc = move_token_through_session(); rc != 0) return rc;
    if (const int rc = walk_test_channel(); rc != 0) return rc;
    if (const int rc = walk_forked_channel(); rc != 0) return rc;
    if (const int rc = policy_runs(); rc != 0) return rc;
    if (const int rc = liveness_and_cancel_run(); rc != 0) return rc;
    return 0;
}
