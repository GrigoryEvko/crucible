// Runtime harness for L8 crash-stop extensions (task #347, SEPLOG-I5).
// Most coverage is in-header static_asserts; this file exercises Stop
// as a first-class handle state and demonstrates the crash-branch
// dispatch pattern via Offer with a Recv<Crash<Peer>, _> branch.

#include <crucible/safety/Session.h>
#include <crucible/safety/SessionCrash.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>
#include <variant>

namespace {

using namespace crucible::safety::proto;

// ── Fixture tags ────────────────────────────────────────────────

struct Server {};  // the unreliable peer

// ── In-memory wire ──────────────────────────────────────────────

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

// ── Fixture messages ────────────────────────────────────────────

struct Request  { std::string payload; };
struct Response { std::string payload; };

auto send_req  = [](Wire& w, Request&& r) noexcept { w.bytes->push_back("REQ:" + r.payload); };
auto recv_req  = [](Wire& w) noexcept -> Request {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Request{s.substr(4)};
};
auto send_resp = [](Wire& w, Response&& r) noexcept { w.bytes->push_back("RESP:" + r.payload); };
auto recv_resp = [](Wire& w) noexcept -> Response {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Response{s.substr(5)};
};
auto send_crash = [](Wire& w, Crash<Server>&&) noexcept { w.bytes->push_back("CRASH"); };
auto recv_crash = [](Wire& w) noexcept -> Crash<Server> {
    w.bytes->pop_front();
    return {};
};
auto send_idx = [](Wire& w, std::size_t i) noexcept { w.bytes->push_back("IDX:" + std::to_string(i)); };
auto recv_idx = [](Wire& w) noexcept -> std::size_t {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return static_cast<std::size_t>(std::atoi(s.data() + 4));
};

// ── Test 1: Stop as terminal handle ─────────────────────────────
//
// Build a handle in the Stop state and verify it's terminal, closable,
// and equivalent to End for handle-lifecycle purposes.

int run_stop_terminal() {
    std::deque<std::string> wire;
    Wire res{&wire};

    auto handle = make_session_handle<Stop>(std::move(res));

    // Shape check at the protocol level.
    static_assert(std::is_same_v<decltype(handle)::protocol, Stop>);
    static_assert(is_stop_v<decltype(handle)::protocol>);
    static_assert(is_terminal_state_v<decltype(handle)::protocol>);

    // close() yields the resource back (same shape as End).
    auto released = std::move(handle).close();
    (void)released;
    return 0;
}

// ── Test 2: Crash-handled request-response, success path ───────
//
// Client protocol:
//   Send Request, then Offer:
//     branch 0 = Recv Response → End  (success)
//     branch 1 = Recv Crash<Server> → End  (recovery)
//
// Server (dual) selects branch 0 to respond normally.

using CrashHandledClient = Send<Request, Offer<
    Recv<Response,      End>,
    Recv<Crash<Server>, End>>>;

using CrashHandledServer = dual_of_t<CrashHandledClient>;

// Compile-time contract: client's Offer has a crash branch for Server.
static_assert(
    has_crash_branch_for_peer_v<
        typename CrashHandledClient::next,
        Server>);

int run_crash_success_path() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};

    auto [client, server] =
        establish_channel<CrashHandledClient>(std::move(a), std::move(b));

    // Client sends request.
    auto client2 = std::move(client).send(Request{"hello"}, send_req);

    // Server receives request.
    auto [got_req, server2] = std::move(server).recv(recv_req);
    if (got_req.payload != "hello") {
        std::fprintf(stderr, "crash success: request payload mismatch\n");
        return 1;
    }

    // Server picks branch 0 (respond normally), sends Response.
    auto server3 = std::move(server2).template select<0>(send_idx)
                                      .send(Response{"world"}, send_resp);

    // Client branches on the received index and dispatches to the
    // matching handler.  For branch 0 it expects a Response; for
    // branch 1 it would handle the crash.
    int rc = std::move(client2).branch(
        recv_idx,
        [&](auto branch_handle) -> int {
            using BH = decltype(branch_handle);
            if constexpr (std::is_same_v<typename BH::protocol,
                                         Recv<Response, End>>) {
                auto [resp, bh2] = std::move(branch_handle).recv(recv_resp);
                if (resp.payload != "world") {
                    std::fprintf(stderr, "crash success: resp mismatch\n");
                    return 1;
                }
                (void)std::move(bh2).close();
                return 0;
            } else if constexpr (std::is_same_v<typename BH::protocol,
                                                Recv<Crash<Server>, End>>) {
                std::fprintf(stderr, "crash success: took crash branch unexpectedly\n");
                return 1;
            } else {
                std::fprintf(stderr, "crash success: unknown branch shape\n");
                return 1;
            }
        });
    if (rc != 0) return rc;

    (void)std::move(server3).close();
    return 0;
}

// ── Test 3: Crash-handled request-response, crash path ─────────
//
// Server selects branch 1 to signal crash; client's recovery handler
// fires.

int run_crash_recovery_path() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};

    auto [client, server] =
        establish_channel<CrashHandledClient>(std::move(a), std::move(b));

    auto client2 = std::move(client).send(Request{"hello"}, send_req);

    auto [got_req, server2] = std::move(server).recv(recv_req);
    if (got_req.payload != "hello") {
        std::fprintf(stderr, "crash recovery: request payload mismatch\n");
        return 1;
    }

    // Server picks branch 1 (signal crash), sends Crash<Server>.
    auto server3 = std::move(server2).template select<1>(send_idx)
                                      .send(Crash<Server>{}, send_crash);

    bool crash_branch_fired = false;
    int rc = std::move(client2).branch(
        recv_idx,
        [&](auto branch_handle) -> int {
            using BH = decltype(branch_handle);
            if constexpr (std::is_same_v<typename BH::protocol,
                                         Recv<Crash<Server>, End>>) {
                auto [crash_payload, bh2] = std::move(branch_handle).recv(recv_crash);
                (void)crash_payload;
                crash_branch_fired = true;
                (void)std::move(bh2).close();
                return 0;
            } else {
                return 1;
            }
        });
    if (rc != 0) return rc;
    if (!crash_branch_fired) {
        std::fprintf(stderr, "crash recovery: crash branch did not fire\n");
        return 1;
    }

    (void)std::move(server3).close();
    return 0;
}

// ── Test 4: ReliableSet membership at runtime ──────────────────
//
// While is_reliable_v is a compile-time trait, exercise it with a
// runtime dispatch that SELECTS behaviour based on a role's
// reliability status.

struct Peer_A {};
struct Peer_B {};
struct Peer_C {};

using R_AB = ReliableSet<Peer_A, Peer_B>;

int run_reliable_set_dispatch() {
    // Dispatch on reliability — at compile time, no runtime branch.
    int seen_reliable = 0;
    int seen_unreliable = 0;

    auto dispatch = [&](auto tag) {
        using Tag = typename decltype(tag)::type;
        if constexpr (is_reliable_v<R_AB, Tag>) {
            ++seen_reliable;
        } else {
            ++seen_unreliable;
        }
    };

    struct Wrap_A { using type = Peer_A; };
    struct Wrap_B { using type = Peer_B; };
    struct Wrap_C { using type = Peer_C; };

    dispatch(Wrap_A{});  // reliable
    dispatch(Wrap_B{});  // reliable
    dispatch(Wrap_C{});  // unreliable

    if (seen_reliable != 2 || seen_unreliable != 1) {
        std::fprintf(stderr, "reliable-set dispatch: wrong counts (%d, %d)\n",
                     seen_reliable, seen_unreliable);
        return 1;
    }
    return 0;
}

// ── Compile-time exercise of the crash-branch assertion helper ──

struct FixturePeer {};
using AssertableOffer = Offer<
    Recv<Request,            End>,
    Recv<Crash<FixturePeer>, End>>;

consteval bool check_assert_crash_branch_helper() {
    assert_has_crash_branch_for<AssertableOffer, FixturePeer>();
    return true;
}
static_assert(check_assert_crash_branch_helper());

}  // anonymous namespace

int main() {
    if (int rc = run_stop_terminal();         rc != 0) return rc;
    if (int rc = run_crash_success_path();    rc != 0) return rc;
    if (int rc = run_crash_recovery_path();   rc != 0) return rc;
    if (int rc = run_reliable_set_dispatch(); rc != 0) return rc;
    std::puts("session_crash: Stop + Crash<> + ReliableSet + crash-branch dispatch OK");
    return 0;
}
