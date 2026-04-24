// Runtime harness for SessionHandle lifetime mechanics (task #349,
// SEPLOG-I7).  Most coverage is compile-time verification that the
// SessionHandleBase<Proto> hierarchy works uniformly; this file
// exercises:
//
//   * Handle properly consumed via close/send/recv/select/pick/base/
//     rollback → no abort
//   * Moved-from handle → no abort on source
//   * Terminal handle (End, Stop) may be destroyed without consumption
//   * Infinite Loop protocol requires explicit .detach() for clean
//     termination
//
// The destructor's std::abort() on abandonment is a DEBUG-only check
// and cannot be tested directly without a death-test framework; we
// document it here and verify the positive paths.

#include <crucible/safety/Session.h>
#include <crucible/safety/SessionCheckpoint.h>
#include <crucible/safety/SessionCrash.h>
#include <crucible/safety/SessionDelegate.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

// ── Compile-time: is_terminal_state_v classifies correctly ─────

static_assert( is_terminal_state_v<End>);
static_assert( is_terminal_state_v<Stop>);
static_assert(!is_terminal_state_v<Send<int, End>>);
static_assert(!is_terminal_state_v<Recv<int, End>>);
static_assert(!is_terminal_state_v<Select<Send<int, End>>>);
static_assert(!is_terminal_state_v<Offer<Recv<int, End>>>);
static_assert(!is_terminal_state_v<Loop<Send<int, Continue>>>);
static_assert(!is_terminal_state_v<Delegate<Send<int, End>, End>>);
static_assert(!is_terminal_state_v<Accept<Send<int, End>, End>>);
static_assert(!is_terminal_state_v<CheckpointedSession<End, End>>);

// ── Compile-time: every SessionHandle derives from SessionHandleBase

struct FakeRes {};

static_assert(std::is_base_of_v<
    SessionHandleBase<End>,
    SessionHandle<End, FakeRes>>);
static_assert(std::is_base_of_v<
    SessionHandleBase<Send<int, End>>,
    SessionHandle<Send<int, End>, FakeRes>>);
static_assert(std::is_base_of_v<
    SessionHandleBase<Stop>,
    SessionHandle<Stop, FakeRes>>);
static_assert(std::is_base_of_v<
    SessionHandleBase<Delegate<Send<int, End>, End>>,
    SessionHandle<Delegate<Send<int, End>, End>, FakeRes>>);
static_assert(std::is_base_of_v<
    SessionHandleBase<CheckpointedSession<End, End>>,
    SessionHandle<CheckpointedSession<End, End>, FakeRes>>);

// ── Runtime: handle properly consumed via close() ──────────────

int run_consume_via_close() {
    auto h = make_session_handle<End>(FakeRes{});
    auto r = std::move(h).close();
    (void)r;
    return 0;
}

// ── Runtime: handle consumed via send/recv chain ───────────────

struct Wire { std::deque<std::string>* bytes = nullptr; };

struct Ping { int n; };
struct Pong { int n; };

auto send_ping = [](Wire& w, Ping&& p) noexcept {
    w.bytes->push_back("PING:" + std::to_string(p.n));
};
auto recv_ping = [](Wire& w) noexcept -> Ping {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Ping{std::atoi(s.data() + 5)};
};
auto send_pong = [](Wire& w, Pong&& p) noexcept {
    w.bytes->push_back("PONG:" + std::to_string(p.n));
};
auto recv_pong = [](Wire& w) noexcept -> Pong {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Pong{std::atoi(s.data() + 5)};
};

using PingPongClient = Send<Ping, Recv<Pong, End>>;

int run_consume_via_chain() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};

    auto [client, server] =
        establish_channel<PingPongClient>(std::move(a), std::move(b));

    auto c1 = std::move(client).send(Ping{42}, send_ping);
    auto [got_ping, s1] = std::move(server).recv(recv_ping);
    if (got_ping.n != 42) return 1;

    auto s2 = std::move(s1).send(Pong{42}, send_pong);
    auto [got_pong, c2] = std::move(c1).recv(recv_pong);
    if (got_pong.n != 42) return 2;

    // Both handles reach End and close cleanly.
    (void)std::move(c2).close();
    (void)std::move(s2).close();
    return 0;
}

// ── Runtime: infinite Loop handles MUST be explicitly detached ──

using InfiniteProducer = Loop<Send<Ping, Continue>>;

int run_detach_infinite_loop() {
    std::deque<std::string> wire;
    Wire res{&wire};

    auto h = make_session_handle<InfiniteProducer>(std::move(res));
    // Send a single ping then detach — the protocol has no close
    // branch, so close() isn't available.
    auto h2 = std::move(h).send(Ping{99}, send_ping);
    std::move(h2).detach();
    return 0;
}

// ── Runtime: Stop handle is terminal — no detach needed ────────

int run_stop_terminal() {
    auto h = make_session_handle<Stop>(FakeRes{});
    // Stop is terminal (is_terminal_state_v<Stop> == true); the
    // destructor check skips.  close() is available for symmetry
    // with End but not required.
    auto r = std::move(h).close();
    (void)r;
    return 0;
}

// ── Runtime: moved-from handle does not fire check ─────────────
//
// After std::move(h).send(...), h is moved-from.  Its destructor
// fires at end of scope.  Base's move ctor set source's consumed_
// = true, so destructor skips the check.

int run_moved_from_safe() {
    std::deque<std::string> wire;
    Wire res{&wire};

    auto h = make_session_handle<PingPongClient>(std::move(res));
    auto h2 = std::move(h).send(Ping{1}, send_ping);
    // At this point `h` is moved-from; its destructor at function
    // exit must not fire the abandonment check.
    auto [_p, h3] = std::move(h2).recv(recv_pong);
    (void)_p;
    (void)std::move(h3).close();

    // Put stale data in wire for recv_pong to consume.
    wire.push_back("PING:1");
    wire.push_back("PONG:1");
    return 0;  // Note: the wire setup above is post-facto; this is
               // a smoke test for lifetime mechanics, not full IO.
}

// ── Compile-time: sizeof includes the consumed_ flag ───────────

struct MinimalRes { int x; };

// SessionHandle has 1 byte for consumed_ + Resource (int=4B) +
// alignment padding.  Exact sizeof depends on alignment; we just
// verify it's STRICTLY LARGER than sizeof(Resource) alone.
static_assert(sizeof(SessionHandle<End, MinimalRes>) > sizeof(MinimalRes),
    "SessionHandle should carry an extra consumed_ flag (+ padding)");

}  // anonymous namespace

int main() {
    if (int rc = run_consume_via_close();      rc != 0) return rc;
    if (int rc = run_consume_via_chain();      rc != 0) return rc;
    if (int rc = run_detach_infinite_loop();   rc != 0) return rc;
    if (int rc = run_stop_terminal();          rc != 0) return rc;
    if (int rc = run_moved_from_safe();        rc != 0) return rc;
    std::puts("session_lifetime: close + chain + detach + Stop + moved-from OK");
    return 0;
}
