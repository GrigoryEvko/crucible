#include <crucible/handles/OneShotFlag.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/_Pinned.h>
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

constexpr eff::HotFgCtx kSessionCtx{};

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
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

struct WorkItem {};  // ownership of one work-item slot
struct AckSlot {};  // ownership of one ack slot

// A one-element queue: the sending transport writes last_int and the
// receiving one reads it. The handle owns the channel by value, so the
// counter accumulates inside the handle's own storage and every transport
// call sees the same instance. That is what lets the loop test read a
// monotonic sequence without reaching for anything in the enclosing scope.
//
// A value type satisfies the resource concept directly. An lvalue reference
// satisfies it only when the referent is pinned, which is what the second
// channel below is for.

struct FakeChannel {
    int last_int = 0;
    int counter = 0;  // advances once per transferable receive
};

struct PinnedFakeChannel : ::crucible::safety::Pinned<PinnedFakeChannel> {
    int last_int = 0;
};

struct CarrierChannel {
    int delegated_last_int = 0;
    int carrier_int = 0;
};

[[gnu::cold]]
void send_int(FakeChannel& ch, int v) noexcept {
    ch.last_int = v;
}

[[gnu::cold]]
void send_pinned_int(PinnedFakeChannel& ch, int v) noexcept {
    ch.last_int = v;
}

// A real channel would hand the receiver a permission that travelled from the
// producer. This one mints a fresh root permission instead, which is sound
// because the protocol type has already granted the receiver authority over
// the tag by the time the receive returns.

[[gnu::cold]]
void send_transferable_int(FakeChannel& ch, Transferable<int, WorkItem>&& t) noexcept {
    ch.last_int = t.value;
    // The permission inside t is consumed here, as the rvalue parameter goes
    // out of scope.
}

[[gnu::cold, nodiscard]]
Transferable<int, WorkItem> recv_transferable_int(FakeChannel& ch) noexcept {
    return Transferable<int, WorkItem>{++ch.counter, mint_permission_root<WorkItem>()};
}

// A transferable payload is the original handoff and a returned one is the
// give-back. The distinction lives in the protocol; on the wire the two are
// identical, which is why this transport looks like the one above.

[[gnu::cold]]
void send_returned_int(FakeChannel& ch, Returned<int, WorkItem>&& r) noexcept {
    ch.last_int = r.value;
}

void delegate_worker_channel(CarrierChannel& carrier, FakeChannel&& delegated) noexcept {
    carrier.delegated_last_int = delegated.last_int;
}

FakeChannel accept_worker_channel(CarrierChannel& carrier) noexcept {
    return FakeChannel{.last_int = carrier.delegated_last_int};
}

void carrier_send_int(CarrierChannel& carrier, int value) noexcept { carrier.carrier_int = value; }

int carrier_recv_int(CarrierChannel& carrier) noexcept { return carrier.carrier_int; }

// There is no receive-side transport for a returned payload here. That is the
// symmetric peer's half, and the tests below only run the recipient side.
// Declaring one unused would trip -Werror=unused-function.

// The shortest lifecycle there is: mint on End holding nothing, then close.
// Closing is admissible only because the permission set is empty.
void test_end_round_trip() {
    FakeChannel ch{42};
    auto h = mint_permissioned_session<End>(kSessionCtx, std::move(ch));

    static_assert(std::is_same_v<typename decltype(h)::protocol, End>);
    static_assert(std::is_same_v<typename decltype(h)::perm_set, EmptyPermSet>);

    FakeChannel out = std::move(h).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 42);
}

// A payload that carries no permission leaves the permission set unchanged
// across the send.
void test_plain_send_end() {
    FakeChannel ch{};
    auto h = mint_permissioned_session<Send<int, End>>(kSessionCtx, std::move(ch));
    static_assert(std::is_same_v<typename decltype(h)::perm_set, EmptyPermSet>);

    auto h_after = std::move(h).send(7, send_int);
    static_assert(std::is_same_v<typename decltype(h_after)::perm_set, EmptyPermSet>);
    static_assert(std::is_same_v<typename decltype(h_after)::protocol, End>);

    FakeChannel out = std::move(h_after).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 7);
}

// The mint takes the caller's permission token and records its tag in the
// initial set. The send is gated on the set holding that tag, and consumes
// it. Closing at End is then admissible because the set is empty again.
void test_transferable_send_end() {
    FakeChannel ch{};
    auto perm = mint_permission_root<WorkItem>();
    auto h =
        mint_permissioned_session<Send<Transferable<int, WorkItem>, End>>(kSessionCtx, std::move(ch), std::move(perm));

    static_assert(std::is_same_v<typename decltype(h)::perm_set, PermSet<WorkItem>>,
                  "Initial PS should reflect the InitPerms... tag pack");

    Transferable<int, WorkItem> payload{99, mint_permission_root<WorkItem>()};
    auto h_after = std::move(h).send(std::move(payload), send_transferable_int);

    static_assert(std::is_same_v<typename decltype(h_after)::perm_set, EmptyPermSet>,
                  "After Send<Transferable<X>>, PS must remove X");
    static_assert(std::is_same_v<typename decltype(h_after)::protocol, End>);

    FakeChannel out = std::move(h_after).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 99);
}

void test_ctx_bound_permissioned_mint_transferable_send_end() {
    using Proto = Send<Transferable<int, WorkItem>, End>;

    eff::HotFgCtx ctx;
    auto perm = mint_permission_root<WorkItem>();
    auto h = mint_permissioned_session<Proto>(ctx, FakeChannel{}, std::move(perm));

    static_assert(std::is_same_v<typename decltype(h)::protocol, Proto>);
    static_assert(std::is_same_v<typename decltype(h)::perm_set, PermSet<WorkItem>>);

    Transferable<int, WorkItem> payload{101, mint_permission_root<WorkItem>()};
    auto h_after = std::move(h).send(std::move(payload), send_transferable_int);

    static_assert(std::is_same_v<typename decltype(h_after)::perm_set, EmptyPermSet>);

    FakeChannel out = std::move(h_after).close();
    CRUCIBLE_TEST_REQUIRE(out.last_int == 101);
}

void test_mint_permissioned_session_empty_permset() {
    using Proto = Send<int, End>;

    eff::HotFgCtx ctx;
    auto h = mint_permissioned_session<Proto>(ctx, FakeChannel{});

    static_assert(std::is_same_v<typename decltype(h)::protocol, Proto>);
    static_assert(std::is_same_v<typename decltype(h)::perm_set, EmptyPermSet>);

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
    static_assert(std::is_same_v<typename decltype(h)::perm_set, EmptyPermSet>);
    static_assert(std::is_same_v<typename decltype(h)::resource_type, PinnedFakeChannel&>);

    auto h_after = std::move(h).send(88, send_pinned_int);
    PinnedFakeChannel& out = std::move(h_after).close();

    CRUCIBLE_TEST_REQUIRE(&out == &ch);
    CRUCIBLE_TEST_REQUIRE(ch.last_int == 88);
}

// Borrow and give back, from the receiving side. The set starts empty, gains
// the tag on the receive, loses it again on the returned send, and is empty
// once more at End.
void test_recv_transferable_send_returned() {
    FakeChannel ch{};
    auto h = mint_permissioned_session<Recv<Transferable<int, WorkItem>, Send<Returned<int, WorkItem>, End>>>(
        kSessionCtx, std::move(ch));

    static_assert(std::is_same_v<typename decltype(h)::perm_set, EmptyPermSet>);

    auto [val, h2] = std::move(h).recv(recv_transferable_int);
    // The receiving transport returns the pre-incremented counter, so the
    // first call yields one.
    CRUCIBLE_TEST_REQUIRE(val.value == 1);

    static_assert(std::is_same_v<typename decltype(h2)::perm_set, PermSet<WorkItem>>,
                  "After Recv<Transferable<X>>, PS must insert X");

    auto h3 = std::move(h2).send(Returned<int, WorkItem>{val.value * 2, std::move(val.perm)}, send_returned_int);

    static_assert(std::is_same_v<typename decltype(h3)::perm_set, EmptyPermSet>,
                  "After Send<Returned<X>>, PS must remove X");
    static_assert(std::is_same_v<typename decltype(h3)::protocol, End>);

    FakeChannel out = std::move(h3).close();
    // The returned send doubled the received value on the way out.
    CRUCIBLE_TEST_REQUIRE(out.last_int == 2);
}

// A carrier session hands a whole worker endpoint over mid-protocol. The
// delegated endpoint carries its own permission set, and the accepting side
// receives it with that set intact.
void test_permissioned_delegate_accept_handoff() {
    using InnerProto = Send<Transferable<int, WorkItem>, End>;
    using InnerPS = PermSet<WorkItem>;
    using Payload = DelegatedSession<InnerProto, InnerPS>;
    using SenderProto = Delegate<Payload, Send<int, End>>;
    using AccepterProto = Accept<Payload, Recv<int, End>>;

    static_assert(std::is_same_v<dual_of_t<SenderProto>, AccepterProto>);

    auto worker_perm = mint_permission_root<WorkItem>();
    auto inner =
        mint_permissioned_session<InnerProto>(kSessionCtx, FakeChannel{.last_int = 314}, std::move(worker_perm));

    CarrierChannel carrier{};
    auto sender = mint_permissioned_session<SenderProto>(kSessionCtx, std::move(carrier));

    auto sender_after_delegate = std::move(sender).delegate(std::move(inner), delegate_worker_channel);

    static_assert(std::is_same_v<typename decltype(sender_after_delegate)::protocol, Send<int, End>>);
    static_assert(std::is_same_v<typename decltype(sender_after_delegate)::perm_set, EmptyPermSet>);

    auto sender_end = std::move(sender_after_delegate).send(777, carrier_send_int);
    CarrierChannel wire_state = std::move(sender_end).close();
    CRUCIBLE_TEST_REQUIRE(wire_state.delegated_last_int == 314);
    CRUCIBLE_TEST_REQUIRE(wire_state.carrier_int == 777);

    auto accepter = mint_permissioned_session<AccepterProto>(kSessionCtx, std::move(wire_state));
    auto [accepted_inner, accepter_after_accept] = std::move(accepter).accept(accept_worker_channel);

    static_assert(std::is_same_v<typename decltype(accepted_inner)::protocol, InnerProto>);
    static_assert(std::is_same_v<typename decltype(accepted_inner)::perm_set, InnerPS>);
    static_assert(std::is_same_v<typename decltype(accepter_after_accept)::protocol, Recv<int, End>>);
    static_assert(std::is_same_v<typename decltype(accepter_after_accept)::perm_set, EmptyPermSet>);

    auto [carrier_value, accepter_end] = std::move(accepter_after_accept).recv(carrier_recv_int);
    CRUCIBLE_TEST_REQUIRE(carrier_value == 777);
    (void)std::move(accepter_end).close();

    Transferable<int, WorkItem> payload{12, mint_permission_root<WorkItem>()};
    auto accepted_inner_end = std::move(accepted_inner).send(std::move(payload), send_transferable_int);
    FakeChannel inner_out = std::move(accepted_inner_end).close();
    CRUCIBLE_TEST_REQUIRE(inner_out.last_int == 12);
}

// The loop body acquires a permission on the receive and surrenders it on the
// send, so the set at the end of the body equals the set at loop entry. That
// equality is what the assertion behind Continue demands. A body that took a
// permission each iteration without giving one back would fail to compile.
//
// The protocol has no exit branch, which is the ordinary shape for a
// perpetual worker, so the handle is detached rather than closed.
void test_loop_balanced_iteration() {
    using BodyProto = Recv<Transferable<int, WorkItem>, Send<Returned<int, WorkItem>, Continue>>;
    using LoopProto = Loop<BodyProto>;

    FakeChannel ch{};
    auto h = mint_permissioned_session<LoopProto>(kSessionCtx, std::move(ch));

    static_assert(std::is_same_v<typename decltype(h)::protocol, BodyProto>);
    static_assert(std::is_same_v<typename decltype(h)::perm_set, EmptyPermSet>);

    int sum = 0;
    constexpr int kIterations = 3;

    for (int i = 0; i < kIterations; ++i) {
        // The counter lives in the handle's own copy of the channel, so the
        // received value is one greater than the loop index and the sum ends
        // up as one plus two plus three.
        (void)i;

        auto [val, h_after_recv] = std::move(h).recv(recv_transferable_int);
        sum += val.value;

        auto h_after_send =
            std::move(h_after_recv).send(Returned<int, WorkItem>{val.value, std::move(val.perm)}, send_returned_int);

        // Continue resolves back to the loop body with the entry permission
        // set, so this handle has exactly the type h has and can be assigned
        // straight back into it.
        h = std::move(h_after_send);
    }

    CRUCIBLE_TEST_REQUIRE(sum == 1 + 2 + 3);

    // The instrumentation reason suits a test fixture. A perpetual worker in
    // production would name the infinite-loop protocol instead.
    std::move(h).detach(detach_reason::TestInstrumentation{});
}

// Neither branch moves a permission, so both reach End with the same empty
// set. Convergence needs no separate check: each branch's close already
// demands an empty set.
//
// The local form of select omits the wire, which is the right idiom for an
// in-process channel with no peer to signal. The wire-carrying form would
// need a transport callable and exercises the same machinery.
void test_select_local_pick_branch() {
    {
        FakeChannel ch{1};
        auto h = mint_permissioned_session<Select<End, Send<int, End>>>(kSessionCtx, std::move(ch));
        auto h_end = std::move(h).template select_local<0>();
        FakeChannel out = std::move(h_end).close();
        CRUCIBLE_TEST_REQUIRE(out.last_int == 1);  // the branch sends nothing
    }
    {
        FakeChannel ch{};
        auto h = mint_permissioned_session<Select<End, Send<int, End>>>(kSessionCtx, std::move(ch));
        auto h_send = std::move(h).template select_local<1>();
        auto h_end = std::move(h_send).send(123, send_int);
        FakeChannel out = std::move(h_end).close();
        CRUCIBLE_TEST_REQUIRE(out.last_int == 123);
    }
}

// When the flag has fired, the wrapper detaches the handle and returns
// nothing, so the caller drops out of its loop. The detach is clean: the
// permission set goes with it, and no abandonment diagnostic fires.
void test_crash_transport_happy_path() {
    using BodyProto = Send<int, Continue>;
    using LoopProto = Loop<BodyProto>;

    FakeChannel ch{};
    ::crucible::safety::OneShotFlag flag;  // never signalled on this path
    auto h = mint_permissioned_session<LoopProto>(kSessionCtx, std::move(ch));

    int sum = 0;
    constexpr int kIterations = 5;
    for (int i = 0; i < kIterations; ++i) {
        auto next = with_crash_check_or_detach(std::move(h), flag,
                                               [v = i + 1](auto h_in) { return std::move(h_in).send(v, send_int); });
        CRUCIBLE_TEST_REQUIRE(next.has_value());
        sum += i + 1;
        h = std::move(*next);
    }
    CRUCIBLE_TEST_REQUIRE(sum == 1 + 2 + 3 + 4 + 5);

    // The loop has no exit branch, so the handle is detached.
    std::move(h).detach(detach_reason::TestInstrumentation{});
}

void test_crash_transport_crash_path() {
    using BodyProto = Send<int, Continue>;
    using LoopProto = Loop<BodyProto>;

    FakeChannel ch{};
    ::crucible::safety::OneShotFlag flag;
    auto h = mint_permissioned_session<LoopProto>(kSessionCtx, std::move(ch));

    int sum = 0;
    bool crash_observed = false;

    for (int i = 0; i < 100; ++i) {
        // Standing in for a peer crash noticed out of band.
        if (i == 3) flag.signal();

        auto next = with_crash_check_or_detach(std::move(h), flag,
                                               [v = i + 1](auto h_in) { return std::move(h_in).send(v, send_int); });
        if (!next) {
            crash_observed = true;
            break;
        }
        sum += i + 1;
        h = std::move(*next);
    }

    CRUCIBLE_TEST_REQUIRE(crash_observed);
    // Three iterations completed before the flag fired on the fourth.
    CRUCIBLE_TEST_REQUIRE(sum == 1 + 2 + 3);
    // The handle is moved-from now that the wrapper has detached it, so it
    // needs no detach of its own.
}

// The global protocol below projects to a send then a receive for the client,
// and to the mirror of that for the server. The fork mints the whole
// permission, splits it per role, runs one thread per role and rebuilds the
// whole permission when both have joined.

namespace fork_test {

// One tag serves as both the role identity in the global protocol and the
// per-role permission tag. Two parallel packs would widen the interface and
// double the manifest below for no gain.
struct ClientRole {};
struct ServerRole {};

struct Whole {};

// One slot per direction, each gated by its own flag. The fork binds the
// channel by lvalue reference, and the resource concept admits such a
// reference only when the referent is pinned.
struct SharedChan : ::crucible::safety::Pinned<SharedChan> {
    std::atomic<int> c2s_value{0};
    std::atomic<bool> c2s_ready{false};
    std::atomic<bool> s2c_value{false};
    std::atomic<bool> s2c_ready{false};
};

void client_send(SharedChan& ch, int v) noexcept {
    ch.c2s_value.store(v, std::memory_order_relaxed);
    ch.c2s_ready.store(true, std::memory_order_release);
}
bool client_recv(SharedChan& ch) noexcept {
    while (!ch.s2c_ready.load(std::memory_order_acquire)) {
        CRUCIBLE_SPIN_PAUSE;
    }
    return ch.s2c_value.load(std::memory_order_relaxed);
}

int server_recv(SharedChan& ch) noexcept {
    while (!ch.c2s_ready.load(std::memory_order_acquire)) {
        CRUCIBLE_SPIN_PAUSE;
    }
    return ch.c2s_value.load(std::memory_order_relaxed);
}
void server_send(SharedChan& ch, bool v) noexcept {
    ch.s2c_value.store(v, std::memory_order_relaxed);
    ch.s2c_ready.store(true, std::memory_order_release);
}

// The role bodies write here so the test can tell that each ran its protocol
// through to the end.
inline std::atomic<int> g_client_received{-1};
inline std::atomic<int> g_server_received{-1};

}  // namespace fork_test

}  // namespace

// The split manifest has to sit at namespace scope, and the discipline puts
// it in the same translation unit as the tags it names, next to them.
namespace crucible::safety {
template <>
struct splits_into_pack<fork_test::Whole, fork_test::ClientRole, fork_test::ServerRole> : std::true_type {};

template <>
struct splits_into_pack_authoring_witness<fork_test::Whole, fork_test::ClientRole, fork_test::ServerRole>
    : std::true_type {};
}  // namespace crucible::safety

namespace {  // re-open anonymous namespace for tests

void test_session_fork_two_role_request_reply() {
    using G = Transmission<fork_test::ClientRole, fork_test::ServerRole, int,
                           Transmission<fork_test::ServerRole, fork_test::ClientRole, bool, End_G>>;

    fork_test::SharedChan ch;
    fork_test::g_client_received.store(-1, std::memory_order_relaxed);
    fork_test::g_server_received.store(-1, std::memory_order_relaxed);

    auto whole = ::crucible::safety::mint_permission_root<fork_test::Whole>();

    auto rebuilt = session_fork<G, fork_test::Whole, fork_test::ClientRole, fork_test::ServerRole>(
        ch, std::move(whole),
        // The client's projected protocol sends, then receives.
        [](auto h_client) noexcept {
            constexpr int kRequest = 7;
            auto h2 = std::move(h_client).send(kRequest, fork_test::client_send);
            auto [reply, h3] = std::move(h2).recv(fork_test::client_recv);
            (void)reply;
            // The role permission stays in the set as proof of
            // participation, so the set is not empty and close is
            // inadmissible. Detach surrenders it instead.
            std::move(h3).detach(detach_reason::TestInstrumentation{});
            fork_test::g_client_received.store(reply ? 1 : 0, std::memory_order_release);
        },
        // The server's projected protocol is the mirror of it.
        [](auto h_server) noexcept {
            auto [req, h2] = std::move(h_server).recv(fork_test::server_recv);
            const bool reply = (req == 7);
            auto h3 = std::move(h2).send(reply, fork_test::server_send);
            std::move(h3).detach(detach_reason::TestInstrumentation{});
            fork_test::g_server_received.store(req, std::memory_order_release);
        });

    // Both threads have joined by now and the whole permission is rebuilt.
    (void)rebuilt;
    CRUCIBLE_TEST_REQUIRE(fork_test::g_server_received.load() == 7);
    CRUCIBLE_TEST_REQUIRE(fork_test::g_client_received.load() == 1);
}

// The permissioned handle occupies exactly what the bare one does, whatever
// the permission set holds, because the set member is empty and collapses
// away. In a debug build both wrappers carry one tracker byte and its
// alignment, so the equality holds in every build mode.
//
// The comparison is equality rather than an upper bound so that losing the
// no-unique-address attribute, or gaining a non-empty member, is caught here.

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, FakeChannel>)
              == sizeof(::crucible::safety::proto::SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<End, PermSet<WorkItem>, FakeChannel>)
              == sizeof(::crucible::safety::proto::SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<End, PermSet<WorkItem, AckSlot>, FakeChannel>)
              == sizeof(::crucible::safety::proto::SessionHandle<End, FakeChannel>));

static_assert(sizeof(PermissionedSessionHandle<Send<int, End>, EmptyPermSet, FakeChannel>)
              == sizeof(::crucible::safety::proto::SessionHandle<Send<int, End>, FakeChannel>));

}  // namespace

int main() {
    std::fprintf(stderr, "[test_permissioned_session_handle]\n");
    run_test("end_round_trip", test_end_round_trip);
    run_test("plain_send_end", test_plain_send_end);
    run_test("transferable_send_end", test_transferable_send_end);
    run_test("ctx_bound_permissioned_mint_transferable_send_end",
             test_ctx_bound_permissioned_mint_transferable_send_end);
    run_test("mint_permissioned_session_empty_permset", test_mint_permissioned_session_empty_permset);
    run_test("ctx_bound_permissioned_mint_preserves_pinned_ref", test_ctx_bound_permissioned_mint_preserves_pinned_ref);
    run_test("recv_transferable_send_returned", test_recv_transferable_send_returned);
    run_test("permissioned_delegate_accept_handoff", test_permissioned_delegate_accept_handoff);
    run_test("loop_balanced_iteration", test_loop_balanced_iteration);
    run_test("select_local_pick_branch", test_select_local_pick_branch);
    run_test("crash_transport_happy_path", test_crash_transport_happy_path);
    run_test("crash_transport_crash_path", test_crash_transport_crash_path);
    run_test("session_fork_two_role_request_reply", test_session_fork_two_role_request_reply);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
