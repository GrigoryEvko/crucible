// Runtime harness for CheckpointedSession combinator (task #362,
// SEPLOG-L2).  Most coverage is in-header static_asserts; this
// file drives a checkpointed session end-to-end: a verify/commit/
// rollback flow on an in-memory wire, covering both paths.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

// ── Fixture messages ────────────────────────────────────────────

struct Proposal { int draft_id; };
struct Accept   {};
struct Reject   {};
struct RetryRequest { int draft_id; };

// ── Wire + transports ──────────────────────────────────────────

struct Wire { std::deque<std::string>* bytes = nullptr; };

auto send_proposal = [](Wire& w, Proposal&& p) noexcept {
    w.bytes->push_back("PROP:" + std::to_string(p.draft_id));
};
auto recv_proposal = [](Wire& w) noexcept -> Proposal {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return Proposal{std::atoi(s.data() + 5)};
};
auto send_accept = [](Wire& w, Accept&&) noexcept { w.bytes->push_back("ACCEPT"); };
auto recv_accept = [](Wire& w) noexcept -> Accept {
    w.bytes->pop_front();
    return {};
};
auto send_reject = [](Wire& w, Reject&&) noexcept { w.bytes->push_back("REJECT"); };
auto recv_reject = [](Wire& w) noexcept -> Reject {
    w.bytes->pop_front();
    return {};
};
auto send_retry = [](Wire& w, RetryRequest&& r) noexcept {
    w.bytes->push_back("RETRY:" + std::to_string(r.draft_id));
};
auto recv_retry = [](Wire& w) noexcept -> RetryRequest {
    std::string s = std::move(w.bytes->front()); w.bytes->pop_front();
    return RetryRequest{std::atoi(s.data() + 6)};
};

// ── Speculative-decoding-style protocol ───────────────────────
//
// Client proposes a draft; target verifies.  On accept, session
// ends normally (commit path).  On reject, client rolls back to
// a retry protocol that sends a new RetryRequest + awaits the
// next verdict.

using CommitPath   = Send<Proposal, Recv<Accept, End>>;
using RollbackPath = Send<Proposal, Recv<Reject, Send<RetryRequest, Recv<Accept, End>>>>;

using ClientProto = CheckpointedSession<CommitPath, RollbackPath>;
using ServerProto = dual_of_t<ClientProto>;

// Sanity checks.
static_assert(is_checkpointed_session_v<ClientProto>);
static_assert(is_well_formed_v<ClientProto>);
static_assert(is_well_formed_v<ServerProto>);

// Duality round-trips.
static_assert(std::is_same_v<dual_of_t<ServerProto>, ClientProto>);

// Server protocol structure — branches are DUALS of client's branches.
static_assert(std::is_same_v<
    ServerProto,
    CheckpointedSession<
        Recv<Proposal, Send<Accept, End>>,           // dual(CommitPath)
        Recv<Proposal, Send<Reject,
              Recv<RetryRequest, Send<Accept, End>>>>>>);

// ── Commit path: verify accepts; client takes .base() ──────────

int run_commit_path() {
    std::deque<std::string> wire;
    Wire c{&wire};
    Wire s{&wire};

    auto [client, server] =
        establish_channel<ClientProto>(std::move(c), std::move(s));

    // Client decides to COMMIT — take the base path.
    auto client_base = std::move(client).base();
    auto server_base = std::move(server).base();

    // Run the commit protocol end-to-end.
    auto client2 = std::move(client_base).send(Proposal{42}, send_proposal);
    auto [got_prop, server2] = std::move(server_base).recv(recv_proposal);
    if (got_prop.draft_id != 42) {
        std::fprintf(stderr, "commit: draft_id mismatch (%d)\n", got_prop.draft_id);
        return 1;
    }

    auto server3 = std::move(server2).send(Accept{}, send_accept);
    auto [got_accept, client3] = std::move(client2).recv(recv_accept);
    (void)got_accept;

    (void)std::move(client3).close();
    (void)std::move(server3).close();
    return 0;
}

// ── Rollback path: verify rejects; client takes .rollback() ───

int run_rollback_path() {
    std::deque<std::string> wire;
    Wire c{&wire};
    Wire s{&wire};

    auto [client, server] =
        establish_channel<ClientProto>(std::move(c), std::move(s));

    // Client decides to ROLLBACK — take the rollback path.
    // (In practice: application-level logic detected verify-reject
    //  and explicitly takes rollback().  Both peers agreed out-of-band
    //  that this session is the rollback variant.)
    auto client_rb = std::move(client).rollback();
    auto server_rb = std::move(server).rollback();

    // Run the rollback protocol: send proposal, receive reject, send
    // retry with a new draft_id, receive final accept.
    auto client2 = std::move(client_rb).send(Proposal{7}, send_proposal);
    auto [got_prop, server2] = std::move(server_rb).recv(recv_proposal);
    if (got_prop.draft_id != 7) {
        std::fprintf(stderr, "rollback: initial draft_id wrong (%d)\n",
                     got_prop.draft_id);
        return 1;
    }

    // Server rejects.
    auto server3 = std::move(server2).send(Reject{}, send_reject);
    auto [got_reject, client3] = std::move(client2).recv(recv_reject);
    (void)got_reject;

    // Client retries with a new draft.
    auto client4 = std::move(client3).send(RetryRequest{99}, send_retry);
    auto [got_retry, server4] = std::move(server3).recv(recv_retry);
    if (got_retry.draft_id != 99) {
        std::fprintf(stderr, "rollback: retry draft_id wrong (%d)\n",
                     got_retry.draft_id);
        return 1;
    }

    // Server accepts the retry.
    auto server5 = std::move(server4).send(Accept{}, send_accept);
    auto [got_final, client5] = std::move(client4).recv(recv_accept);
    (void)got_final;

    (void)std::move(client5).close();
    (void)std::move(server5).close();
    return 0;
}

// ── Compile-time exercise of the assertion helper ─────────────

consteval bool check_assert_matches() {
    assert_checkpointed_matches<ClientProto, CommitPath, RollbackPath>();
    return true;
}
static_assert(check_assert_matches());

// ── Compose-identity on CheckpointedSession ───────────────────

static_assert(std::is_same_v<
    compose_t<ClientProto, End>,
    ClientProto>);

// Composing with non-End extends BOTH branches.
struct Extension {};
using ExtendedCkpt = compose_t<ClientProto, Send<Extension, End>>;

static_assert(is_checkpointed_session_v<ExtendedCkpt>);
static_assert(std::is_same_v<
    checkpoint_base_t<ExtendedCkpt>,
    compose_t<CommitPath, Send<Extension, End>>>);
static_assert(std::is_same_v<
    checkpoint_rollback_t<ExtendedCkpt>,
    compose_t<RollbackPath, Send<Extension, End>>>);

}  // anonymous namespace

int main() {
    if (int rc = run_commit_path();   rc != 0) return rc;
    if (int rc = run_rollback_path(); rc != 0) return rc;
    std::puts("session_checkpoint: CheckpointedSession .base()/.rollback() both paths OK");
    return 0;
}
