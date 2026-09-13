// The header carries most of the coverage in static_asserts.  What this
// file adds is Stop driven as a handle state of its own, and the
// dispatch pattern for an offer that carries a crash branch.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionMint.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>
#include <variant>

namespace {

using namespace crucible::safety::proto;

struct Server {};  // the peer that is allowed to crash

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

struct Request {
    std::string payload;
};
struct Response {
    std::string payload;
};

auto send_req = [](Wire& w, Request&& r) noexcept { w.bytes->push_back("REQ:" + r.payload); };
auto recv_req = [](Wire& w) noexcept -> Request {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return Request{s.substr(4)};
};
auto send_resp = [](Wire& w, Response&& r) noexcept { w.bytes->push_back("RESP:" + r.payload); };
auto recv_resp = [](Wire& w) noexcept -> Response {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return Response{s.substr(5)};
};
auto send_crash = [](Wire& w, Crash<Server>&&) noexcept { w.bytes->push_back("CRASH"); };
auto recv_crash = [](Wire& w) noexcept -> Crash<Server> {
    w.bytes->pop_front();
    return {};
};
auto send_idx = [](Wire& w, std::size_t i) noexcept { w.bytes->push_back("IDX:" + std::to_string(i)); };
auto recv_idx = [](Wire& w) noexcept -> std::size_t {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return static_cast<std::size_t>(std::atoi(s.data() + 4));
};

// Stop is terminal and closable, and behaves like End as far as the
// handle lifecycle is concerned.

int run_stop_terminal() {
    std::deque<std::string> wire;
    Wire res{&wire};

    auto handle = mint_session_handle<Stop>(std::move(res));

    static_assert(std::is_same_v<decltype(handle)::protocol, Stop>);
    static_assert(is_stop_v<decltype(handle)::protocol>);
    static_assert(is_terminal_state_v<decltype(handle)::protocol>);

    // Closing hands the resource back, exactly as it does from End.
    auto released = std::move(handle).close();
    (void)released;
    return 0;
}

// The client sends a request and then offers two branches.  Branch zero
// receives a response and branch one receives a crash, so the server
// selects zero to answer normally and one to report a crash.

using CrashHandledClient = Send<Request, Offer<Recv<Response, End>, Recv<Crash<Server>, End>>>;

using CrashHandledServer = dual_of_t<CrashHandledClient>;

static_assert(has_crash_branch_for_peer_v<typename CrashHandledClient::next, Server>);

int run_crash_success_path() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};
    crucible::effects::HotFgCtx ctx{};

    auto [client, server] = mint_channel<CrashHandledClient>(ctx, ctx, std::move(a), std::move(b));

    auto client2 = std::move(client).send(Request{"hello"}, send_req);

    auto [got_req, server2] = std::move(server).recv(recv_req);
    if (got_req.payload != "hello") {
        std::fprintf(stderr, "crash success: request payload mismatch\n");
        return 1;
    }

    // Branch zero answers normally.
    auto server3 = std::move(server2).template select<0>(send_idx).send(Response{"world"}, send_resp);

    int rc = std::move(client2).branch(recv_idx, [&](auto branch_handle) -> int {
        using BH = decltype(branch_handle);
        if constexpr (std::is_same_v<typename BH::protocol, Recv<Response, End>>) {
            auto [resp, bh2] = std::move(branch_handle).recv(recv_resp);
            if (resp.payload != "world") {
                std::fprintf(stderr, "crash success: resp mismatch\n");
                return 1;
            }
            (void)std::move(bh2).close();
            return 0;
        } else if constexpr (std::is_same_v<typename BH::protocol, Recv<Crash<Server>, End>>) {
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

int run_crash_recovery_path() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};
    crucible::effects::HotFgCtx ctx{};

    auto [client, server] = mint_channel<CrashHandledClient>(ctx, ctx, std::move(a), std::move(b));

    auto client2 = std::move(client).send(Request{"hello"}, send_req);

    auto [got_req, server2] = std::move(server).recv(recv_req);
    if (got_req.payload != "hello") {
        std::fprintf(stderr, "crash recovery: request payload mismatch\n");
        return 1;
    }

    // Branch one reports a crash, which must fire the recovery handler.
    auto server3 = std::move(server2).template select<1>(send_idx).send(Crash<Server>{}, send_crash);

    bool crash_branch_fired = false;
    int rc = std::move(client2).branch(recv_idx, [&](auto branch_handle) -> int {
        using BH = decltype(branch_handle);
        if constexpr (std::is_same_v<typename BH::protocol, Recv<Crash<Server>, End>>) {
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

// Reliability is a compile-time trait.  Driving it from a runtime loop
// of tag types is what shows it selecting behaviour per role.

struct Peer_A {};
struct Peer_B {};
struct Peer_C {};

using R_AB = ReliableSet<Peer_A, Peer_B>;

int run_reliable_set_dispatch() {
    // The selection happens during instantiation, so no branch survives
    // into the generated code.
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

    struct Wrap_A {
        using type = Peer_A;
    };
    struct Wrap_B {
        using type = Peer_B;
    };
    struct Wrap_C {
        using type = Peer_C;
    };

    dispatch(Wrap_A{});  // reliable
    dispatch(Wrap_B{});  // reliable
    dispatch(Wrap_C{});  // unreliable

    if (seen_reliable != 2 || seen_unreliable != 1) {
        std::fprintf(stderr, "reliable-set dispatch: wrong counts (%d, %d)\n", seen_reliable, seen_unreliable);
        return 1;
    }
    return 0;
}

struct FixturePeer {};
using AssertableOffer = Offer<Recv<Request, End>, Recv<Crash<FixturePeer>, End>>;

consteval bool check_assert_crash_branch_helper() {
    assert_has_crash_branch_for<AssertableOffer, FixturePeer>();
    return true;
}
static_assert(check_assert_crash_branch_helper());

}  // anonymous namespace

int main() {
    if (int rc = run_stop_terminal(); rc != 0) return rc;
    if (int rc = run_crash_success_path(); rc != 0) return rc;
    if (int rc = run_crash_recovery_path(); rc != 0) return rc;
    if (int rc = run_reliable_set_dispatch(); rc != 0) return rc;
    std::puts("session_crash: Stop + Crash<> + ReliableSet + crash-branch dispatch OK");
    return 0;
}
