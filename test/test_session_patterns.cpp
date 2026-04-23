// Runtime harness for the session-type pattern library (task #341,
// SEPLOG-H2p).  The bulk of coverage is in-header static_asserts; this
// file exercises a handful of patterns end-to-end through an in-memory
// transport to prove each alias is a usable SessionHandle position,
// not just a type alias that type-checks in isolation.

#include <crucible/safety/SessionPatterns.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;
using namespace crucible::safety::proto::pattern;

// ─── In-memory wire ─────────────────────────────────────────────

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

auto send_str = [](Wire& w, std::string&& s) noexcept {
    w.bytes->push_back(std::move(s));
};
auto recv_str = [](Wire& w) noexcept -> std::string {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return s;
};
auto send_idx = [](Wire& w, std::size_t i) noexcept {
    w.bytes->push_back("IDX:" + std::to_string(i));
};
auto recv_idx = [](Wire& w) noexcept -> std::size_t {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return static_cast<std::size_t>(std::atoi(s.data() + 4));
};

// ─── Fixture messages ───────────────────────────────────────────

struct Req  { std::string payload; };
struct Resp { std::string payload; };

auto send_req  = [](Wire& w, Req&& r) noexcept  { w.bytes->push_back("REQ:"  + r.payload); };
auto recv_req  = [](Wire& w) noexcept -> Req    {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Req{s.substr(4)};
};
auto send_resp = [](Wire& w, Resp&& r) noexcept { w.bytes->push_back("RESP:" + r.payload); };
auto recv_resp = [](Wire& w) noexcept -> Resp   {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Resp{s.substr(5)};
};

// ─── Test 1: RequestResponseOnce ────────────────────────────────

int run_request_response_once() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};

    auto [client, server] =
        establish_channel<RequestResponseOnce_Client<Req, Resp>>(std::move(a), std::move(b));

    auto client2 = std::move(client).send(Req{"hello"}, send_req);
    auto [got_req, server2] = std::move(server).recv(recv_req);
    if (got_req.payload != "hello") {
        std::fprintf(stderr, "request-response: req payload mismatch: %s\n",
                     got_req.payload.c_str());
        return 1;
    }

    auto server3 = std::move(server2).send(Resp{"world"}, send_resp);
    auto [got_resp, client3] = std::move(client2).recv(recv_resp);
    if (got_resp.payload != "world") {
        std::fprintf(stderr, "request-response: resp payload mismatch: %s\n",
                     got_resp.payload.c_str());
        return 1;
    }

    (void)std::move(client3).close();
    (void)std::move(server3).close();
    return 0;
}

// ─── Test 2: FanOut (coordinator sends 3 jobs into the wire) ────

int run_fan_out() {
    std::deque<std::string> wire;
    Wire coord{&wire};

    auto handle = make_session_handle<FanOut<3, Req>>(std::move(coord));
    auto h1 = std::move(handle).send(Req{"job0"}, send_req);
    auto h2 = std::move(h1).send(Req{"job1"}, send_req);
    auto h3 = std::move(h2).send(Req{"job2"}, send_req);
    (void)std::move(h3).close();

    if (wire.size() != 3 || wire[0] != "REQ:job0" ||
        wire[1] != "REQ:job1" || wire[2] != "REQ:job2") {
        std::fprintf(stderr, "fan-out: wire contents wrong (size=%zu)\n", wire.size());
        return 1;
    }
    return 0;
}

// ─── Test 3: ScatterGather (send N tasks, recv N results) ───────

int run_scatter_gather() {
    constexpr std::size_t N = 3;
    std::deque<std::string> wire;
    Wire client_res{&wire};
    Wire server_res{&wire};

    auto [coord, worker] =
        establish_channel<ScatterGather<N, Req, Resp>>(std::move(client_res),
                                                       std::move(server_res));

    // Coordinator sends N tasks.
    auto c1 = std::move(coord).send(Req{"t0"}, send_req);
    auto c2 = std::move(c1).send(Req{"t1"}, send_req);
    auto c3 = std::move(c2).send(Req{"t2"}, send_req);

    // Worker (in this test, one actor playing all workers) receives N,
    // then responds N.
    auto [t0, w1] = std::move(worker).recv(recv_req);
    auto [t1, w2] = std::move(w1).recv(recv_req);
    auto [t2, w3] = std::move(w2).recv(recv_req);
    if (t0.payload != "t0" || t1.payload != "t1" || t2.payload != "t2") {
        std::fprintf(stderr, "scatter-gather: task payloads wrong\n");
        return 1;
    }

    auto w4 = std::move(w3).send(Resp{"r0"}, send_resp);
    auto w5 = std::move(w4).send(Resp{"r1"}, send_resp);
    auto w6 = std::move(w5).send(Resp{"r2"}, send_resp);

    auto [r0, c4] = std::move(c3).recv(recv_resp);
    auto [r1, c5] = std::move(c4).recv(recv_resp);
    auto [r2, c6] = std::move(c5).recv(recv_resp);
    if (r0.payload != "r0" || r1.payload != "r1" || r2.payload != "r2") {
        std::fprintf(stderr, "scatter-gather: result payloads wrong\n");
        return 1;
    }

    (void)std::move(c6).close();
    (void)std::move(w6).close();
    return 0;
}

// ─── Test 4: Two-phase commit (coordinator commits) ─────────────

struct Prepare { int tx_id; };
struct Vote    { bool yes; };
struct Commit  {};
struct Abort   {};

auto send_prepare = [](Wire& w, Prepare&& p) noexcept {
    w.bytes->push_back("PREP:" + std::to_string(p.tx_id));
};
auto recv_prepare = [](Wire& w) noexcept -> Prepare {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Prepare{std::atoi(s.data() + 5)};
};
auto send_vote = [](Wire& w, Vote&& v) noexcept {
    w.bytes->push_back(v.yes ? "VOTE:YES" : "VOTE:NO");
};
auto recv_vote = [](Wire& w) noexcept -> Vote {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Vote{s == "VOTE:YES"};
};
auto send_commit = [](Wire& w, Commit&&) noexcept { w.bytes->push_back("COMMIT"); };
auto recv_commit = [](Wire& w) noexcept -> Commit {
    w.bytes->pop_front();
    return {};
};

int run_two_phase_commit() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};

    using Proto = TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>;
    auto [coord, follower] = establish_channel<Proto>(std::move(a), std::move(b));

    // Coordinator → follower: Prepare
    auto coord2 = std::move(coord).send(Prepare{42}, send_prepare);

    // Follower ← Prepare
    auto [p, follower2] = std::move(follower).recv(recv_prepare);
    if (p.tx_id != 42) {
        std::fprintf(stderr, "2pc: prepare tx_id wrong: %d\n", p.tx_id);
        return 1;
    }

    // Follower → coordinator: Vote
    auto follower3 = std::move(follower2).send(Vote{true}, send_vote);

    // Coordinator ← Vote
    auto [v, coord3] = std::move(coord2).recv(recv_vote);
    if (!v.yes) {
        std::fprintf(stderr, "2pc: unexpected abort vote\n");
        return 1;
    }

    // Coordinator picks Commit branch (index 0).
    auto coord4 = std::move(coord3).template select<0>(send_idx)
                                    .send(Commit{}, send_commit);

    // Follower reads the branch choice (0 = Commit) and dispatches.
    auto follower_rc = std::move(follower3).branch(
        recv_idx,
        [&](auto branch_handle) -> int {
            using BH = decltype(branch_handle);
            if constexpr (std::is_same_v<typename BH::protocol,
                                         Recv<Commit, End>>) {
                auto [c, bh2] = std::move(branch_handle).recv(recv_commit);
                (void)c;
                (void)std::move(bh2).close();
                return 0;
            } else {
                std::fprintf(stderr, "2pc: follower got wrong branch\n");
                return 1;
            }
        });
    if (follower_rc != 0) return follower_rc;

    (void)std::move(coord4).close();
    return 0;
}

// ─── Compile-time exercise of the concept surface ───────────────

static_assert(is_well_formed_v<RequestResponseOnce_Client<Req, Resp>>);
static_assert(is_well_formed_v<ScatterGather<4, Req, Resp>>);
static_assert(is_well_formed_v<TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>>);
static_assert(is_well_formed_v<MpmcProducer<Req>>);
static_assert(is_well_formed_v<Transaction_Client<Prepare, Req, Commit, Vote, Abort>>);

}  // anonymous namespace

int main() {
    if (int rc = run_request_response_once(); rc != 0) return rc;
    if (int rc = run_fan_out();               rc != 0) return rc;
    if (int rc = run_scatter_gather();        rc != 0) return rc;
    if (int rc = run_two_phase_commit();      rc != 0) return rc;
    std::puts("session_patterns: RRonce + FanOut + ScatterGather + 2PC OK");
    return 0;
}
