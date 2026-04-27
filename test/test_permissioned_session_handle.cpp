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

#include <crucible/permissions/Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety::proto;
using ::crucible::safety::Permission;
using ::crucible::safety::permission_root_mint;

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
// `establish_channel<...>(std::move(a), std::move(b))`.

struct FakeChannel {
    int last_int = 0;
    int counter  = 0;  // monotonic per recv_transferable_int call
};

// ── Plain-int Transport (send only — recv variant unused in this
//    file; recv-side test arrives via Transferable in Tier 4) ──────

[[gnu::cold]]
void send_int(FakeChannel& ch, int v) noexcept { ch.last_int = v; }

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
// is sound because permission_root_mint<X> is the canonical mint
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
                                        permission_root_mint<WorkItem>()};
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
    auto h = establish_permissioned<End>(ch);

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
    auto h = establish_permissioned<Send<int, End>>(ch);
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
//   * establish_permissioned<P, R, InitPerms...> consumes the
//     Permission<WorkItem> token from the caller and records its
//     tag in the initial PS.
//   * SendablePayload<Transferable<int, WorkItem>, PermSet<WorkItem>>
//     concept gates the send — concept holds because PS contains the
//     transferred tag.
//   * compute_perm_set_after_send_t removes the tag from PS.
//   * close() static_assert allows EmptyPermSet at End.

void test_transferable_send_end() {
    FakeChannel ch{};
    auto perm = permission_root_mint<WorkItem>();
    auto h = establish_permissioned<Send<Transferable<int, WorkItem>, End>>(
        ch, std::move(perm));

    static_assert(std::is_same_v<typename decltype(h)::perm_set,
                                 PermSet<WorkItem>>,
                  "Initial PS should reflect the InitPerms... tag pack");

    Transferable<int, WorkItem> payload{99,
                                         permission_root_mint<WorkItem>()};
    auto h_after = std::move(h).send(std::move(payload),
                                     send_transferable_int);

    static_assert(std::is_same_v<typename decltype(h_after)::perm_set,
                                 EmptyPermSet>,
                  "After Send<Transferable<X>>, PS must remove X");
    static_assert(std::is_same_v<typename decltype(h_after)::protocol, End>);

    FakeChannel out = std::move(h_after).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 99);
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
    auto h = establish_permissioned<
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
    auto h = establish_permissioned<LoopProto>(ch);

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
        auto h = establish_permissioned<Select<End, Send<int, End>>>(ch);
        auto h_end = std::move(h).template select_local<0>();
        FakeChannel out = std::move(h_end).close();
        CRUCIBLE_TEST_REQUIRE(out.last_int == 1);  // unmodified
    }
    {
        FakeChannel ch{};
        auto h = establish_permissioned<Select<End, Send<int, End>>>(ch);
        auto h_send = std::move(h).template select_local<1>();
        auto h_end  = std::move(h_send).send(123, send_int);
        FakeChannel out = std::move(h_end).close();
        CRUCIBLE_TEST_REQUIRE(out.last_int == 123);
    }
}

// ═════════════════════════════════════════════════════════════════
// ── Tier 7: Sizeof equality at file scope (compile-time witness)
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
    run_test("recv_transferable_send_returned", test_recv_transferable_send_returned);
    run_test("loop_balanced_iteration",         test_loop_balanced_iteration);
    run_test("select_local_pick_branch",        test_select_local_pick_branch);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
