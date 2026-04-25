// Runtime harness for L4 global types G + projection (task #339,
// SEPLOG-H2g).  Most coverage is in-header static_asserts; this
// file exercises projection end-to-end: declare a global type once,
// derive every role's local type via project_t, establish channels
// with those local types, and verify messages flow correctly.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionPatterns.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

// ── Role tags ──────────────────────────────────────────────────

struct Coordinator {};
struct Follower    {};

// ── Payload types ──────────────────────────────────────────────

struct Prepare { int tx_id; };
struct Vote    { bool yes;  };
struct Commit  {};
struct Abort   {};

// ── Define 2PC as a GLOBAL TYPE once; derive local types by projection ──
//
// Global view:
//   Coordinator → Follower : Prepare
//   Follower    → Coordinator : Vote
//   Coordinator → Follower : one of { Commit, Abort }
//
// We express the multi-step flow with nested Transmission, closing
// with a Choice at the end.

using G_2PC = Transmission<Coordinator, Follower, Prepare,
              Transmission<Follower, Coordinator, Vote,
              Choice<Coordinator, Follower,
                  BranchG<Commit, End_G>,
                  BranchG<Abort,  End_G>>>>;

// Project onto each role.
using CoordProto    = project_t<G_2PC, Coordinator>;
using FollowerProto = project_t<G_2PC, Follower>;

// Compile-time: the projections MUST equal the hand-written
// TwoPhaseCommit patterns from SessionPatterns.h.
static_assert(std::is_same_v<
    CoordProto,
    pattern::TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>>);

static_assert(std::is_same_v<
    FollowerProto,
    pattern::TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>>);

// Compile-time: the projections are duals of each other.
static_assert(std::is_same_v<dual_of_t<CoordProto>, FollowerProto>);

// Compile-time: the global type is well-formed and its role set is
// exactly {Coordinator, Follower}.
static_assert(is_global_well_formed_v<G_2PC>);
static_assert(roles_of_t<G_2PC>::size == 2);
static_assert(detail::global::contains_role_v<Coordinator, roles_of_t<G_2PC>>);
static_assert(detail::global::contains_role_v<Follower,   roles_of_t<G_2PC>>);

// ── In-memory wire ────────────────────────────────────────────

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

auto send_prepare = [](Wire& w, Prepare&& p) noexcept {
    w.bytes->push_back("PREP:" + std::to_string(p.tx_id));
};
auto recv_prepare = [](Wire& w) noexcept -> Prepare {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Prepare{std::atoi(s.data() + 5)};
};
auto send_vote = [](Wire& w, Vote&& v) noexcept {
    w.bytes->push_back(v.yes ? "VOTE:Y" : "VOTE:N");
};
auto recv_vote = [](Wire& w) noexcept -> Vote {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Vote{s == "VOTE:Y"};
};
auto send_commit = [](Wire& w, Commit&&) noexcept { w.bytes->push_back("COMMIT"); };
auto recv_commit = [](Wire& w) noexcept -> Commit {
    w.bytes->pop_front();
    return {};
};
auto send_idx = [](Wire& w, std::size_t i) noexcept {
    w.bytes->push_back("IDX:" + std::to_string(i));
};
auto recv_idx = [](Wire& w) noexcept -> std::size_t {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return static_cast<std::size_t>(std::atoi(s.data() + 4));
};

// ── Drive the 2PC protocol via projected handles ──────────────

int run_projected_2pc() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};

    // Establish the channel using the PROJECTED types (not hand-written).
    auto [coord, follower] =
        establish_channel<CoordProto>(std::move(a), std::move(b));

    // Step 1: coord sends Prepare.
    auto coord2 = std::move(coord).send(Prepare{99}, send_prepare);

    // Step 2: follower receives Prepare.
    auto [p, follower2] = std::move(follower).recv(recv_prepare);
    if (p.tx_id != 99) {
        std::fprintf(stderr, "projected 2pc: tx_id mismatch %d\n", p.tx_id);
        return 1;
    }

    // Step 3: follower sends Vote=Yes.
    auto follower3 = std::move(follower2).send(Vote{true}, send_vote);

    // Step 4: coord receives Vote.
    auto [v, coord3] = std::move(coord2).recv(recv_vote);
    if (!v.yes) {
        std::fprintf(stderr, "projected 2pc: expected yes vote\n");
        return 1;
    }

    // Step 5: coord selects Commit (branch 0).
    auto coord4 = std::move(coord3).template select<0>(send_idx)
                                    .send(Commit{}, send_commit);

    // Step 6: follower branches.
    int rc = std::move(follower3).branch(
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
                std::fprintf(stderr, "projected 2pc: wrong follower branch\n");
                return 1;
            }
        });
    if (rc != 0) return rc;

    (void)std::move(coord4).close();
    return 0;
}

// ── Multi-party projection sanity check (ternary chain) ────────
//
// Alice → Bob, then Bob → Carol.  Verify each role's projection
// matches the expected structure AND that each role's local type
// duals with its direct peer's counterpart.

struct Alice {};
struct Bob   {};
struct Carol {};

struct Ping {};
struct Pong {};

using G_chain = Transmission<Alice, Bob, Ping,
                 Transmission<Bob, Carol, Pong, End_G>>;

using AliceLocal = project_t<G_chain, Alice>;
using BobLocal   = project_t<G_chain, Bob>;
using CarolLocal = project_t<G_chain, Carol>;

static_assert(std::is_same_v<AliceLocal, Send<Ping, End>>);
static_assert(std::is_same_v<BobLocal,   Recv<Ping, Send<Pong, End>>>);
static_assert(std::is_same_v<CarolLocal, Recv<Pong, End>>);

// Note: In a 3-party chain, dual(AliceLocal) does NOT equal BobLocal
// because Bob also talks to Carol.  Duality is a BINARY property;
// MPST gives us a more general well-formedness via projection +
// merge, not pairwise duality.
static_assert(!std::is_same_v<dual_of_t<AliceLocal>, BobLocal>);
static_assert(!std::is_same_v<dual_of_t<BobLocal>,   CarolLocal>);

// But Alice's local type, composed with Bob's prefix up to the
// relevant shared event, IS consistent: Alice sends Ping, Bob's
// corresponding first event is a recv of Ping.  This is what MPST
// projection gives us structurally — NOT binary duality.

// ── Compile-time exercise of is_stop_g_v for StopG projection ──

using G_after_crash = Transmission<Alice, Bob, Ping, StopG<Alice>>;
static_assert(is_stop_g_v<StopG<Alice>>);
static_assert(is_global_well_formed_v<G_after_crash>);

// Alice's projection ends in Stop (she's the crashed peer).
static_assert(std::is_same_v<
    project_t<G_after_crash, Alice>,
    Send<Ping, Stop>>);

// Bob's projection ends in End (Alice crashed; Bob's protocol is over).
static_assert(std::is_same_v<
    project_t<G_after_crash, Bob>,
    Recv<Ping, End>>);

// ── Runtime smoke ─────────────────────────────────────────────

int run_smoke() {
    if (roles_of_t<G_2PC>::size != 2) return 1;
    if (roles_of_t<G_chain>::size != 3) return 1;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_smoke();          rc != 0) return rc;
    if (int rc = run_projected_2pc();  rc != 0) return rc;
    std::puts("session_global: global type G + projection + roles OK");
    return 0;
}
