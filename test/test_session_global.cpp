// The header carries most of the coverage in static_asserts.  What this
// file adds is the end-to-end path: declare a global type once, derive
// each role's local type from it, open channels with those derived
// types, and watch messages flow.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionPatterns.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

struct Coordinator {};
struct Follower {};

struct Prepare {
    int tx_id;
};
struct Vote {
    bool yes;
};
struct Commit {};
struct Abort {};

// The global view of two-phase commit:
//
//   Coordinator → Follower    : Prepare
//   Follower    → Coordinator : Vote
//   Coordinator → Follower    : one of { Commit, Abort }
//
// Nested transmissions carry the multi-step flow, and a choice closes it.

using G_2PC = Transmission<Coordinator, Follower, Prepare,
                           Transmission<Follower, Coordinator, Vote,
                                        Choice<Coordinator, Follower, BranchG<Commit, End_G>, BranchG<Abort, End_G>>>>;

using CoordProto = project_t<G_2PC, Coordinator>;
using FollowerProto = project_t<G_2PC, Follower>;

// Projection must reproduce the hand-written pattern exactly.
static_assert(std::is_same_v<CoordProto, pattern::TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>>);

static_assert(std::is_same_v<FollowerProto, pattern::TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>>);

static_assert(std::is_same_v<dual_of_t<CoordProto>, FollowerProto>);

static_assert(is_global_well_formed_v<G_2PC>);
static_assert(roles_of_t<G_2PC>::size == 2);
static_assert(detail::global::contains_role_v<Coordinator, roles_of_t<G_2PC>>);
static_assert(detail::global::contains_role_v<Follower, roles_of_t<G_2PC>>);

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

auto send_prepare = [](Wire& w, Prepare&& p) noexcept { w.bytes->push_back("PREP:" + std::to_string(p.tx_id)); };
auto recv_prepare = [](Wire& w) noexcept -> Prepare {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return Prepare{std::atoi(s.data() + 5)};
};
auto send_vote = [](Wire& w, Vote&& v) noexcept { w.bytes->push_back(v.yes ? "VOTE:Y" : "VOTE:N"); };
auto recv_vote = [](Wire& w) noexcept -> Vote {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return Vote{s == "VOTE:Y"};
};
auto send_commit = [](Wire& w, Commit&&) noexcept { w.bytes->push_back("COMMIT"); };
auto recv_commit = [](Wire& w) noexcept -> Commit {
    w.bytes->pop_front();
    return {};
};
auto send_idx = [](Wire& w, std::size_t i) noexcept { w.bytes->push_back("IDX:" + std::to_string(i)); };
auto recv_idx = [](Wire& w) noexcept -> std::size_t {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return static_cast<std::size_t>(std::atoi(s.data() + 4));
};

int run_projected_2pc() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};
    crucible::effects::HotFgCtx ctx{};

    // The channel is opened on the projected types, not on the
    // hand-written ones.
    auto [coord, follower] = mint_channel<CoordProto>(ctx, ctx, std::move(a), std::move(b));

    auto coord2 = std::move(coord).send(Prepare{99}, send_prepare);

    auto [p, follower2] = std::move(follower).recv(recv_prepare);
    if (p.tx_id != 99) {
        std::fprintf(stderr, "projected 2pc: tx_id mismatch %d\n", p.tx_id);
        return 1;
    }

    auto follower3 = std::move(follower2).send(Vote{true}, send_vote);

    auto [v, coord3] = std::move(coord2).recv(recv_vote);
    if (!v.yes) {
        std::fprintf(stderr, "projected 2pc: expected yes vote\n");
        return 1;
    }

    // Branch zero is the Commit arm.
    auto coord4 = std::move(coord3).template select<0>(send_idx).send(Commit{}, send_commit);

    int rc = std::move(follower3).branch(recv_idx, [&](auto branch_handle) -> int {
        using BH = decltype(branch_handle);
        if constexpr (std::is_same_v<typename BH::protocol, Recv<Commit, End>>) {
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

struct Alice {};
struct Bob {};
struct Carol {};

struct Ping {};
struct Pong {};

using G_chain = Transmission<Alice, Bob, Ping, Transmission<Bob, Carol, Pong, End_G>>;

using AliceLocal = project_t<G_chain, Alice>;
using BobLocal = project_t<G_chain, Bob>;
using CarolLocal = project_t<G_chain, Carol>;

static_assert(std::is_same_v<AliceLocal, Send<Ping, End>>);
static_assert(std::is_same_v<BobLocal, Recv<Ping, Send<Pong, End>>>);
static_assert(std::is_same_v<CarolLocal, Recv<Pong, End>>);

// In a three-party chain the dual of Alice's local type is not Bob's,
// because Bob also talks to Carol.  Duality is a property of a pair.
// What holds across three roles is per-event consistency, delivered by
// projection and merge: Alice sends Ping and Bob's matching first event
// receives it.
static_assert(!std::is_same_v<dual_of_t<AliceLocal>, BobLocal>);
static_assert(!std::is_same_v<dual_of_t<BobLocal>, CarolLocal>);

using G_after_crash = Transmission<Alice, Bob, Ping, StopG<Alice>>;
static_assert(is_stop_g_v<StopG<Alice>>);
static_assert(is_global_well_formed_v<G_after_crash>);

// Alice is the crashed peer, so that projection ends in Stop.
static_assert(std::is_same_v<project_t<G_after_crash, Alice>, Send<Ping, Stop>>);

// Bob received Ping from Alice and so interacted with the crashed peer.
// Crash propagation ends that projection in Stop rather than a clean
// End.  Stop is the bottom of the subtype lattice, so the rule tightens
// the projection rather than loosening it.
static_assert(std::is_same_v<project_t<G_after_crash, Bob>, Recv<Ping, Stop>>);

int run_smoke() {
    if (roles_of_t<G_2PC>::size != 2) return 1;
    if (roles_of_t<G_chain>::size != 3) return 1;
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_smoke(); rc != 0) return rc;
    if (int rc = run_projected_2pc(); rc != 0) return rc;
    std::puts("session_global: global type G + projection + roles OK");
    return 0;
}
