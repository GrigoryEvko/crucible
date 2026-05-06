// ═══════════════════════════════════════════════════════════════════
// test_permissioned_session_handle — end-to-end integration test for
// FOUND-C Phase 3 (PermissionedSessionHandle CRTP wrapper).
//
// Coverage map (mirrors `misc/27_04_csl_permission_session_wiring.md`
// §16.1 Phase 3 test gates):
//
//   Tier 1: End                                     close() round trip
//   Tier 2: Send<int, End>                          plain payload, no PS
//   Tier 3: Send<Transferable<int, X>, End>         PS shrinks on send
//   Tier 4: Recv<Transferable<int, X>,              PS grows on recv,
//             Send<Returned<int, X>, End>>            shrinks on returned send
//   Tier 4b: Delegate/Accept<DelegatedSession<P,S>> inner PSH moves
//   Tier 5: Loop<Recv<Transferable<int, X>,         Loop balance enforcement
//             Send<Returned<int, X>, Continue>>>      (Decision D3)
//   Tier 6: Select<End, Send<int, End>>             branch convergence
//                                                    (Decision D4 structural)
//   Tier 7: sizeof(PSH<P, PS, R>) == sizeof(SessionHandle<P, R>)
//                                                    for various PS sizes
//
// What this test PROVES:
//
//   * The PSH machinery actually works end-to-end with a real
//     Transport (not just compile-time shape).
//   * PermSet evolves correctly via compute_perm_set_after_send_t /
//     compute_perm_set_after_recv_t at every step.
//   * Loop with Continue resolves back to the body with the loop-
//     entry PermSet preserved (Decision D3 — perm_set_equal_v gate
//     fires only on imbalance; balanced loops compile fine).
//   * Select branches that all reach End converge structurally on
//     EmptyPermSet (Decision D4 — no separate convergence
//     metafunction needed).
//   * sizeof(PSH) == sizeof(SessionHandle) — the zero-overhead claim
//     under a real Resource type.
//
// What this test does NOT cover (separate tests / phases):
//
//   * Offer<Bs...>::branch with handler dispatch (Phase 7 neg-compile
//     + the SessionPatterns existing harness already exercise Offer
//     for the bare framework; permission-flow on Offer is exercised
//     by the runtime smoke in PermissionedSession.h).
//   * Negative-compile fixtures (Phase 7, separate target).
//   * Bench / machine-code parity (Phase 8).
//   * session_fork (Phase 4 — separate primitive).
//   * OneShotFlag crash transport (Phase 5 — composes
//     CrashWatchedHandle as inner Resource).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/handles/OneShotFlag.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionMint.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety::proto;
namespace eff = ::crucible::effects;
using ::crucible::safety::Permission;
using ::crucible::safety::mint_permission_root;

// ── Test boilerplate (matches existing test files' shape) ─────────

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                       \
    do {                                                                 \
        if (!(__VA_ARGS__)) [[unlikely]] {                               \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                   \
                         #__VA_ARGS__, __FILE__, __LINE__);              \
            throw TestFailure{};                                         \
        }                                                                \
    } while (0)

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

// ── User tags for permission flow ─────────────────────────────────

struct WorkItem {};   // ownership of one work-item slot
struct AckSlot  {};   // ownership of one ack slot

// ── In-process FakeChannel — value-type SessionResource ───────────
//
// Models a one-element queue: the producer-side transport writes
// `last_int`, the consumer-side reads it.  `counter` increments on
// each Transferable recv so the iterated loop test produces a
// monotonic sequence (1, 2, 3, ...) without depending on outer-scope
// state — the handle owns this FakeChannel by value, so all state
// mutations happen inside the handle's storage and are visible to
// every transport invocation.
//
// Value-type Resource passes the SessionResource concept (the
// concept admits any non-reference type or any lvalue ref to Pinned).
// We deliberately use value-by-move here to mirror the existing
// session-test pattern in test_session_patterns.cpp:
// `mint_channel<...>(std::move(a), std::move(b))`.

struct FakeChannel {
    int last_int = 0;
    int counter  = 0;  // monotonic per recv_transferable_int call
};

struct PinnedFakeChannel
    : ::crucible::safety::Pinned<PinnedFakeChannel>
{
    int last_int = 0;
};

struct CarrierChannel {
    int delegated_last_int = 0;
    int carrier_int        = 0;
};

// ── Plain-int Transport (send only — recv variant unused in this
//    file; recv-side test arrives via Transferable in Tier 4) ──────

[[gnu::cold]]
void send_int(FakeChannel& ch, int v) noexcept { ch.last_int = v; }

[[gnu::cold]]
void send_pinned_int(PinnedFakeChannel& ch, int v) noexcept {
    ch.last_int = v;
}

// ── Transferable<int, WorkItem> Transports ────────────────────────
//
// The send-side simply writes the int payload into the channel; the
// embedded Permission is consumed when the Transferable destructs at
// end of the send-statement scope.
//
// The recv-side reads the int and MINTS a fresh Permission<WorkItem>
// to wrap as the Transferable's perm field.  In a real production
// channel, the Permission would have been transferred from the
// producer through a CSL handoff (the channel's internal Pool +
// SharedPermissionGuard would carry that flow); the FakeChannel here
// simulates the transferred Permission by minting a fresh one — which
// is sound because mint_permission_root<X> is the canonical mint
// site and the receiver legitimately has authority over X after the
// recv at the protocol-type level.

[[gnu::cold]]
void send_transferable_int(FakeChannel& ch,
                           Transferable<int, WorkItem>&& t) noexcept {
    ch.last_int = t.value;
    // t.perm destructs as the rvalue parameter goes out of scope.
}

[[gnu::cold, nodiscard]]
Transferable<int, WorkItem> recv_transferable_int(FakeChannel& ch) noexcept {
    // Counter-based payload so iterated-loop tests see a monotonic
    // sequence regardless of who wrote ch.last_int between calls.
    // (The handle owns this FakeChannel by value, so the counter
    // accumulates across iterations within the handle's lifetime.)
    return Transferable<int, WorkItem>{++ch.counter,
                                        mint_permission_root<WorkItem>()};
}

// ── Returned<int, WorkItem> Transports — symmetric to Transferable
//
// The semantic distinction between Transferable and Returned is at
// the protocol layer (Transferable is the original handoff;
// Returned is the give-back).  The wire format is identical at the
// FakeChannel level.

[[gnu::cold]]
void send_returned_int(FakeChannel& ch,
                       Returned<int, WorkItem>&& r) noexcept {
    ch.last_int = r.value;
}

void delegate_worker_channel(CarrierChannel& carrier,
                             FakeChannel&& delegated) noexcept {
    carrier.delegated_last_int = delegated.last_int;
}

FakeChannel accept_worker_channel(CarrierChannel& carrier) noexcept {
    return FakeChannel{.last_int = carrier.delegated_last_int};
}

void carrier_send_int(CarrierChannel& carrier, int value) noexcept {
    carrier.carrier_int = value;
}

int carrier_recv_int(CarrierChannel& carrier) noexcept {
    return carrier.carrier_int;
}

// Note: recv_returned_int (Returned<int, WorkItem>(FakeChannel&)) is
// not exercised by the tests below — the recv side of Returned would
// be the symmetric peer's transport, but Tier 4 / Tier 5 only run the
// recipient half (Recv<Transferable> + Send<Returned>) for clarity.
// Adding it speculatively trips -Werror=unused-function; the symmetric
// peer's neg-compile witness in Phase 7 will exercise the recv side.

// ═════════════════════════════════════════════════════════════════
// ── Tier 1: End round trip ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// The simplest possible PSH lifecycle: establish on End with no
// permissions, then close.  Verifies the Resource is moved through
// without corruption and that close() with EmptyPermSet passes the
// permission-surrender static_assert.

void test_end_round_trip() {
    FakeChannel ch{42};
    auto h = mint_permissioned_session<End>(ch);

    // Type-level checks at the establish boundary.
    static_assert(std::is_same_v<typename decltype(h)::protocol, End>);
    static_assert(std::is_same_v<typename decltype(h)::perm_set,
                                 EmptyPermSet>);

    FakeChannel out = std::move(h).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 42);
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 2: plain Send<int, End> → close ─────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// Plain (non-permission-carrying) payload.  PS stays EmptyPermSet
// across the send — verifies that compute_perm_set_after_send_t
// returns PS unchanged for plain payloads.

void test_plain_send_end() {
    FakeChannel ch{};
    auto h = mint_permissioned_session<Send<int, End>>(ch);
    static_assert(std::is_same_v<typename decltype(h)::perm_set,
                                 EmptyPermSet>);

    auto h_after = std::move(h).send(7, send_int);
    static_assert(std::is_same_v<typename decltype(h_after)::perm_set,
                                 EmptyPermSet>);
    static_assert(std::is_same_v<typename decltype(h_after)::protocol, End>);

    FakeChannel out = std::move(h_after).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 7);
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 3: Send<Transferable<int, X>, End> → close ──────────────
// ═════════════════════════════════════════════════════════════════
//
// Sender holds Permission<WorkItem> initially (PS = {WorkItem});
// Send<Transferable<int, WorkItem>> consumes it (PS' = {});
// close() at End passes the permission-surrender static_assert
// because PS == EmptyPermSet at that point.
//
// This exercises:
//   * mint_permissioned_session<P, R, InitPerms...> consumes the
//     Permission<WorkItem> token from the caller and records its
//     tag in the initial PS.
//   * SendablePayload<Transferable<int, WorkItem>, PermSet<WorkItem>>
//     concept gates the send — concept holds because PS contains the
//     transferred tag.
//   * compute_perm_set_after_send_t removes the tag from PS.
//   * close() static_assert allows EmptyPermSet at End.

void test_transferable_send_end() {
    FakeChannel ch{};
    auto perm = mint_permission_root<WorkItem>();
    auto h = mint_permissioned_session<Send<Transferable<int, WorkItem>, End>>(
        ch, std::move(perm));

    static_assert(std::is_same_v<typename decltype(h)::perm_set,
                                 PermSet<WorkItem>>,
                  "Initial PS should reflect the InitPerms... tag pack");

    Transferable<int, WorkItem> payload{99,
                                         mint_permission_root<WorkItem>()};
    auto h_after = std::move(h).send(std::move(payload),
                                     send_transferable_int);

    static_assert(std::is_same_v<typename decltype(h_after)::perm_set,
                                 EmptyPermSet>,
                  "After Send<Transferable<X>>, PS must remove X");
    static_assert(std::is_same_v<typename decltype(h_after)::protocol, End>);

    FakeChannel out = std::move(h_after).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 99);
}

void test_ctx_bound_permissioned_mint_transferable_send_end() {
    using Proto = Send<Transferable<int, WorkItem>, End>;

    eff::HotFgCtx ctx;
    auto perm = mint_permission_root<WorkItem>();
    auto h = mint_permissioned_session<Proto>(
        ctx, FakeChannel{}, std::move(perm));

    static_assert(std::is_same_v<typename decltype(h)::protocol, Proto>);
    static_assert(std::is_same_v<typename decltype(h)::perm_set,
                                 PermSet<WorkItem>>);

    Transferable<int, WorkItem> payload{
        101, mint_permission_root<WorkItem>()};
    auto h_after = std::move(h).send(std::move(payload),
                                     send_transferable_int);

    static_assert(std::is_same_v<typename decltype(h_after)::perm_set,
                                 EmptyPermSet>);

    FakeChannel out = std::move(h_after).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 101);
}

void test_mint_session_empty_permset_shim() {
    using Proto = Send<int, End>;

    eff::HotFgCtx ctx;
    auto h = mint_session<Proto>(ctx, FakeChannel{});

    static_assert(std::is_same_v<typename decltype(h)::protocol, Proto>);
    static_assert(std::is_same_v<typename decltype(h)::perm_set,
                                 EmptyPermSet>);

    auto h_after = std::move(h).send(55, send_int);
    FakeChannel out = std::move(h_after).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 55);
}

void test_ctx_bound_permissioned_mint_preserves_pinned_ref() {
    using Proto = Send<int, End>;

    eff::HotFgCtx ctx;
    PinnedFakeChannel ch;
    auto h = mint_permissioned_session<Proto>(ctx, ch);

    static_assert(std::is_same_v<typename decltype(h)::protocol, Proto>);
    static_assert(std::is_same_v<typename decltype(h)::perm_set,
                                 EmptyPermSet>);
    static_assert(std::is_same_v<typename decltype(h)::resource_type,
                                 PinnedFakeChannel&>);

    auto h_after = std::move(h).send(88, send_pinned_int);
    PinnedFakeChannel& out = std::move(h_after).close();

    CRUCIBLE_TEST_REQUIRE(&out == &ch);
    CRUCIBLE_TEST_REQUIRE(ch.last_int == 88);
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 4: Recv Transferable → Send Returned → close ────────────
// ═════════════════════════════════════════════════════════════════
//
// The canonical "borrow and return" round trip from one side of a
// permission-flow protocol.  Receiver starts with EmptyPermSet,
// gains Permission<WorkItem> via Recv<Transferable>, surrenders it
// via Send<Returned>, reaches End with EmptyPermSet and closes.

void test_recv_transferable_send_returned() {
    FakeChannel ch{};
    auto h = mint_permissioned_session<
        Recv<Transferable<int, WorkItem>,
             Send<Returned<int, WorkItem>, End>>>(ch);

    static_assert(std::is_same_v<typename decltype(h)::perm_set,
                                 EmptyPermSet>);

    auto [val, h2] = std::move(h).recv(recv_transferable_int);
    // recv_transferable_int returns ++ch.counter — first call yields 1.
    CRUCIBLE_TEST_REQUIRE(val.value == 1);

    static_assert(std::is_same_v<typename decltype(h2)::perm_set,
                                 PermSet<WorkItem>>,
                  "After Recv<Transferable<X>>, PS must insert X");

    auto h3 = std::move(h2).send(
        Returned<int, WorkItem>{val.value * 2, std::move(val.perm)},
        send_returned_int);

    static_assert(std::is_same_v<typename decltype(h3)::perm_set,
                                 EmptyPermSet>,
                  "After Send<Returned<X>>, PS must remove X");
    static_assert(std::is_same_v<typename decltype(h3)::protocol, End>);

    FakeChannel out = std::move(h3).close();
    // send_returned_int wrote val.value * 2 = 2 into out.last_int.
    CRUCIBLE_TEST_REQUIRE(out.last_int == 2);
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 4b: PSH Delegate/Accept transfers inner endpoint PS ─────
// ═════════════════════════════════════════════════════════════════
//
// Carrier delegates a worker endpoint mid-protocol.  The delegated
// endpoint is itself permissioned: InnerProto starts at
// Send<Transferable<int, WorkItem>, End> with InnerPS={WorkItem}.
// Delegate consumes the sender's inner PSH; Accept recreates the
// recipient's inner PSH with the same InnerPS.

void test_permissioned_delegate_accept_handoff() {
    using InnerProto = Send<Transferable<int, WorkItem>, End>;
    using InnerPS    = PermSet<WorkItem>;
    using Payload    = DelegatedSession<InnerProto, InnerPS>;
    using SenderProto = Delegate<Payload, Send<int, End>>;
    using AccepterProto = Accept<Payload, Recv<int, End>>;

    static_assert(std::is_same_v<dual_of_t<SenderProto>, AccepterProto>);

    auto worker_perm = mint_permission_root<WorkItem>();
    auto inner = mint_permissioned_session<InnerProto>(
        FakeChannel{.last_int = 314}, std::move(worker_perm));

    CarrierChannel carrier{};
    auto sender = mint_permissioned_session<SenderProto>(carrier);

    auto sender_after_delegate = std::move(sender).delegate(
        std::move(inner), delegate_worker_channel);

    static_assert(std::is_same_v<typename decltype(sender_after_delegate)::protocol,
                                 Send<int, End>>);
    static_assert(std::is_same_v<typename decltype(sender_after_delegate)::perm_set,
                                 EmptyPermSet>);

    auto sender_end = std::move(sender_after_delegate).send(
        777, carrier_send_int);
    CarrierChannel wire_state = std::move(sender_end).close();
    CRUCIBLE_TEST_REQUIRE(wire_state.delegated_last_int == 314);
    CRUCIBLE_TEST_REQUIRE(wire_state.carrier_int == 777);

    auto accepter = mint_permissioned_session<AccepterProto>(wire_state);
    auto [accepted_inner, accepter_after_accept] =
        std::move(accepter).accept(accept_worker_channel);

    static_assert(std::is_same_v<typename decltype(accepted_inner)::protocol,
                                 InnerProto>);
    static_assert(std::is_same_v<typename decltype(accepted_inner)::perm_set,
                                 InnerPS>);
    static_assert(std::is_same_v<typename decltype(accepter_after_accept)::protocol,
                                 Recv<int, End>>);
    static_assert(std::is_same_v<typename decltype(accepter_after_accept)::perm_set,
                                 EmptyPermSet>);

    auto [carrier_value, accepter_end] =
        std::move(accepter_after_accept).recv(carrier_recv_int);
    CRUCIBLE_TEST_REQUIRE(carrier_value == 777);
    (void)std::move(accepter_end).close();

    Transferable<int, WorkItem> payload{
        12, mint_permission_root<WorkItem>()};
    auto accepted_inner_end = std::move(accepted_inner).send(
        std::move(payload), send_transferable_int);
    FakeChannel inner_out = std::move(accepted_inner_end).close();
    CRUCIBLE_TEST_REQUIRE(inner_out.last_int == 12);
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 5: Loop balance — Decision D3 (Risk R1) ─────────────────
// ═════════════════════════════════════════════════════════════════
//
// Loop body: Recv<Transferable<int, WorkItem>,
//                 Send<Returned<int, WorkItem>, Continue>>
//
// At loop entry:    PS = EmptyPermSet
// After Recv<Trans>: PS = {WorkItem}
// After Send<Ret>:   PS = EmptyPermSet (matches loop entry)
// At Continue:       static_assert(PS == LoopEntryPS) PASSES.
//
// If we wrote a body that drains a permission per iteration without
// surrender (e.g. Recv<Transferable<int, X>, Continue>), the
// static_assert at Continue would FIRE and the build would fail.
// That negative case lives in test/sessions_neg/permission_imbalance
// (Phase 7).
//
// We run three iterations, each one acquiring and surrendering the
// permission, then detach with TestInstrumentation reason because
// the protocol has no exit branch (Loop without exit is an
// infinite-loop pattern — common in production for perpetual
// workers; documented in Session.h's detach_reason::* taxonomy).

void test_loop_balanced_iteration() {
    using BodyProto = Recv<Transferable<int, WorkItem>,
                           Send<Returned<int, WorkItem>, Continue>>;
    using LoopProto = Loop<BodyProto>;

    FakeChannel ch{};
    auto h = mint_permissioned_session<LoopProto>(ch);

    static_assert(std::is_same_v<typename decltype(h)::protocol, BodyProto>);
    static_assert(std::is_same_v<typename decltype(h)::perm_set, EmptyPermSet>);

    int sum = 0;
    constexpr int kIterations = 3;

    for (int i = 0; i < kIterations; ++i) {
        // recv_transferable_int returns ++ch.counter from the handle's
        // OWN copy of FakeChannel (passed by value at establish).  So
        // val.value is i+1 across iterations 0, 1, 2 → sum is 1+2+3.
        (void)i;

        auto [val, h_after_recv] = std::move(h).recv(recv_transferable_int);
        sum += val.value;

        auto h_after_send = std::move(h_after_recv).send(
            Returned<int, WorkItem>{val.value, std::move(val.perm)},
            send_returned_int);

        // h_after_send's type is identical to h's type (Continue
        // resolved back to LoopBody with PS = LoopEntryPS = Empty).
        // Move-assign reuses the slot.
        h = std::move(h_after_send);
    }

    CRUCIBLE_TEST_REQUIRE(sum == 1 + 2 + 3);

    // Detach the perpetual loop — the protocol has no exit branch.
    // TestInstrumentation tag because this is exercise code; in
    // production the right tag would be InfiniteLoopProtocol.
    std::move(h).detach(detach_reason::TestInstrumentation{});
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 6: Select branch convergence (Decision D4 structural) ───
// ═════════════════════════════════════════════════════════════════
//
// Select<End, Send<int, End>>: both branches reach End with the
// same EmptyPermSet because neither branch transfers permissions.
// The structural enforcement (Decision D4) is satisfied because
// every branch's terminal close() carries the same EmptyPermSet
// requirement; convergence is automatic.
//
// We exercise the wire-omitting select_local<I> variant — for an
// in-process FakeChannel this is the right idiom (no peer wire to
// signal).  The bare select<I>(transport) variant would require a
// transport callable; same machinery, just one extra layer.

void test_select_local_pick_branch() {
    {
        FakeChannel ch{1};
        auto h = mint_permissioned_session<Select<End, Send<int, End>>>(ch);
        auto h_end = std::move(h).template select_local<0>();
        FakeChannel out = std::move(h_end).close();
        CRUCIBLE_TEST_REQUIRE(out.last_int == 1);  // unmodified
    }
    {
        FakeChannel ch{};
        auto h = mint_permissioned_session<Select<End, Send<int, End>>>(ch);
        auto h_send = std::move(h).template select_local<1>();
        auto h_end  = std::move(h_send).send(123, send_int);
        FakeChannel out = std::move(h_end).close();
        CRUCIBLE_TEST_REQUIRE(out.last_int == 123);
    }
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 7: Crash transport composition (FOUND-C Phase 5) ────────
// ═════════════════════════════════════════════════════════════════
//
// Worked example of `with_crash_check_or_detach` — the v1 lightweight
// crash transport composition.  Production loop:
//
//   while (more_work) {
//       auto next = with_crash_check_or_detach(std::move(h), flag,
//                       [&](auto h_in) { return std::move(h_in).send(...); });
//       if (!next) break;       // crash detected, h was detached, PS dropped
//       h = std::move(*next);   // continue with the new handle
//   }
//
// Two scenarios exercised:
//   (a) Happy path: flag never fires, full N iterations complete;
//   (b) Crash path: flag fires mid-loop, with_crash_check_or_detach
//       returns nullopt, the handle was detached cleanly with
//       TransportClosedOutOfBand reason (no abort, no leak diagnostic).

void test_crash_transport_happy_path() {
    using BodyProto = Send<int, Continue>;
    using LoopProto = Loop<BodyProto>;

    FakeChannel ch{};
    ::crucible::safety::OneShotFlag flag;  // never set
    auto h = mint_permissioned_session<LoopProto>(ch);

    int sum = 0;
    constexpr int kIterations = 5;
    for (int i = 0; i < kIterations; ++i) {
        auto next = with_crash_check_or_detach(
            std::move(h), flag,
            [v = i + 1](auto h_in) {
                return std::move(h_in).send(v, send_int);
            });
        CRUCIBLE_TEST_REQUIRE(next.has_value());
        sum += i + 1;
        h = std::move(*next);
    }
    CRUCIBLE_TEST_REQUIRE(sum == 1 + 2 + 3 + 4 + 5);

    // Loop has no exit branch — explicit detach.
    std::move(h).detach(detach_reason::TestInstrumentation{});
}

void test_crash_transport_crash_path() {
    using BodyProto = Send<int, Continue>;
    using LoopProto = Loop<BodyProto>;

    FakeChannel ch{};
    ::crucible::safety::OneShotFlag flag;
    auto h = mint_permissioned_session<LoopProto>(ch);

    int sum = 0;
    bool crash_observed = false;

    for (int i = 0; i < 100; ++i) {
        // Fire the crash flag at iteration 3 (simulates peer crash
        // detected by SWIM / CNTP / kernel socket close handler).
        if (i == 3) flag.signal();

        auto next = with_crash_check_or_detach(
            std::move(h), flag,
            [v = i + 1](auto h_in) {
                return std::move(h_in).send(v, send_int);
            });
        if (!next) {
            // with_crash_check_or_detach detached h cleanly with
            // TransportClosedOutOfBand reason — no abandonment
            // diagnostic, no abort, type-level PS was dropped.
            crash_observed = true;
            break;
        }
        sum += i + 1;
        h = std::move(*next);
    }

    CRUCIBLE_TEST_REQUIRE(crash_observed);
    // Three iterations completed (i = 0, 1, 2) before crash at i = 3.
    CRUCIBLE_TEST_REQUIRE(sum == 1 + 2 + 3);
    // Note: h is in a moved-from state after with_crash_check_or_detach
    // detached it.  No further detach call needed.
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 8: session_fork two-role request-reply (FOUND-C Phase 4)
// ═════════════════════════════════════════════════════════════════
//
// End-to-end exercise of session_fork.  Global protocol:
//
//   G = Transmission<ClientRole, ServerRole, int,
//         Transmission<ServerRole, ClientRole, bool, End_G>>
//
// Project<G, ClientRole>::type = Send<int, Recv<bool, End>>
// Project<G, ServerRole>::type = Recv<int, Send<bool, End>>
//
// session_fork:
//   * Mints Permission<Whole>; we manifest splits_into_pack<Whole,
//     ClientPerm, ServerPerm>.
//   * Spawns one jthread per role; each role's body receives a PSH
//     parameterised by its projected protocol + PermSet<RolePerm>.
//   * RAII-joins both threads; rebuilds Whole on return.
//
// SharedChannel: Pinned struct with two atomic-flag-gated slots (one
// per direction).  Each role's transports busy-wait on the
// counterpart's flag.

namespace fork_test {

// Role tags double as permission tags — session_fork's RolePerms... pack
// is BOTH the projection role identity in G AND the per-role Permission
// tag.  Keeping them unified (rather than pairing two parallel packs)
// keeps the API surface narrow and the splits_into_pack manifest small.
struct ClientRole {};
struct ServerRole {};

struct Whole       {};

// In-memory shared channel: client→server (int) + server→client (bool).
// Pinned because session_fork's SharedChannel binding is
// SharedChannel& and SessionResource requires Pinned for lvalue refs.
struct SharedChan : ::crucible::safety::Pinned<SharedChan> {
    std::atomic<int>  c2s_value{0};
    std::atomic<bool> c2s_ready{false};
    std::atomic<bool> s2c_value{false};
    std::atomic<bool> s2c_ready{false};
};

// Client transports.
void client_send(SharedChan& ch, int v) noexcept {
    ch.c2s_value.store(v, std::memory_order_relaxed);
    ch.c2s_ready.store(true, std::memory_order_release);
}
bool client_recv(SharedChan& ch) noexcept {
    while (!ch.s2c_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    return ch.s2c_value.load(std::memory_order_relaxed);
}

// Server transports.
int server_recv(SharedChan& ch) noexcept {
    while (!ch.c2s_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    return ch.c2s_value.load(std::memory_order_relaxed);
}
void server_send(SharedChan& ch, bool v) noexcept {
    ch.s2c_value.store(v, std::memory_order_relaxed);
    ch.s2c_ready.store(true, std::memory_order_release);
}

// Side-effect outputs the bodies write so the test can verify both
// roles ran their full protocols.
inline std::atomic<int>  g_client_received{-1};
inline std::atomic<int>  g_server_received{-1};

}  // namespace fork_test

}  // namespace  (close anonymous namespace before splits_into_pack)

// splits_into_pack manifest — must be at namespace scope, declared
// adjacent to the role tags.  Same TU per CSL discipline rule.
namespace crucible::safety {
template <>
struct splits_into_pack<fork_test::Whole,
                        fork_test::ClientRole,
                        fork_test::ServerRole>
    : std::true_type {};
}  // namespace crucible::safety

namespace {  // re-open anonymous namespace for tests

void test_session_fork_two_role_request_reply() {
    using G = Transmission<fork_test::ClientRole, fork_test::ServerRole, int,
                Transmission<fork_test::ServerRole, fork_test::ClientRole, bool,
                  End_G>>;

    fork_test::SharedChan ch;
    fork_test::g_client_received.store(-1, std::memory_order_relaxed);
    fork_test::g_server_received.store(-1, std::memory_order_relaxed);

    auto whole = ::crucible::safety::mint_permission_root<fork_test::Whole>();

    auto rebuilt = session_fork<G, fork_test::Whole,
                                 fork_test::ClientRole,
                                 fork_test::ServerRole>(
        ch,
        std::move(whole),
        // Client body: Send<int, Recv<bool, End>>.
        [](auto h_client) noexcept {
            constexpr int kRequest = 7;
            auto h2 = std::move(h_client).send(kRequest, fork_test::client_send);
            auto [reply, h3] = std::move(h2).recv(fork_test::client_recv);
            (void)reply;
            // h3 is at End with PS = PermSet<ClientPerm>.  close()
            // requires PermSet == Empty; the role permission stays in
            // PS as "proof of participation" — surrender via detach
            // (TestInstrumentation reason for this test fixture).
            std::move(h3).detach(detach_reason::TestInstrumentation{});
            fork_test::g_client_received.store(reply ? 1 : 0,
                                                std::memory_order_release);
        },
        // Server body: Recv<int, Send<bool, End>>.
        [](auto h_server) noexcept {
            auto [req, h2] = std::move(h_server).recv(fork_test::server_recv);
            const bool reply = (req == 7);
            auto h3 = std::move(h2).send(reply, fork_test::server_send);
            std::move(h3).detach(detach_reason::TestInstrumentation{});
            fork_test::g_server_received.store(req,
                                                std::memory_order_release);
        });

    // session_fork joined both threads; whole permission was rebuilt.
    (void)rebuilt;
    CRUCIBLE_TEST_REQUIRE(fork_test::g_server_received.load() == 7);
    CRUCIBLE_TEST_REQUIRE(fork_test::g_client_received.load() == 1);
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 9: Sizeof equality at file scope (compile-time witness)
// ═════════════════════════════════════════════════════════════════
//
// PermissionedSessionHandle MUST have the same sizeof as the bare
// SessionHandle for the same Proto and Resource — regardless of |PS|.
// EBO collapses the empty PermSet member; the inherited tracker is
// empty in release.  In debug both wrappers carry one tracker byte
// + alignment, so equality holds in every build mode.
//
// This is the load-bearing zero-overhead claim from the wiring plan
// §13 bench harness.  Asserting `==` (rather than `<=`) catches
// accidental loss of [[no_unique_address]] or addition of a non-
// empty member.

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, FakeChannel>)
              == sizeof(::crucible::safety::proto::SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<End,
                                                PermSet<WorkItem>,
                                                FakeChannel>)
              == sizeof(::crucible::safety::proto::SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<End,
                                                PermSet<WorkItem, AckSlot>,
                                                FakeChannel>)
              == sizeof(::crucible::safety::proto::SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<Send<int, End>,
                                                EmptyPermSet,
                                                FakeChannel>)
              == sizeof(::crucible::safety::proto::SessionHandle<Send<int, End>,
                                                                  FakeChannel>));

}  // namespace

int main() {
    std::fprintf(stderr, "[test_permissioned_session_handle]\n");
    run_test("end_round_trip",                  test_end_round_trip);
    run_test("plain_send_end",                  test_plain_send_end);
    run_test("transferable_send_end",           test_transferable_send_end);
    run_test("ctx_bound_permissioned_mint_transferable_send_end",
             test_ctx_bound_permissioned_mint_transferable_send_end);
    run_test("mint_session_empty_permset_shim",
             test_mint_session_empty_permset_shim);
    run_test("ctx_bound_permissioned_mint_preserves_pinned_ref",
             test_ctx_bound_permissioned_mint_preserves_pinned_ref);
    run_test("recv_transferable_send_returned", test_recv_transferable_send_returned);
    run_test("permissioned_delegate_accept_handoff",
             test_permissioned_delegate_accept_handoff);
    run_test("loop_balanced_iteration",         test_loop_balanced_iteration);
    run_test("select_local_pick_branch",        test_select_local_pick_branch);
    run_test("crash_transport_happy_path",      test_crash_transport_happy_path);
    run_test("crash_transport_crash_path",      test_crash_transport_crash_path);
    run_test("session_fork_two_role_request_reply",
             test_session_fork_two_role_request_reply);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
