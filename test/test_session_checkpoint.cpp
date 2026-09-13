// A checkpointed session is driven end to end here, over an in-memory
// wire, once down the commit path and once down the rollback path.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionMint.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

struct Proposal {
    int draft_id;
};
struct Accept {};
struct Reject {};
struct RetryRequest {
    int draft_id;
};

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

auto send_proposal = [](Wire& w, Proposal&& p) noexcept { w.bytes->push_back("PROP:" + std::to_string(p.draft_id)); };
auto recv_proposal = [](Wire& w) noexcept -> Proposal {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
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
auto send_retry = [](Wire& w, RetryRequest&& r) noexcept { w.bytes->push_back("RETRY:" + std::to_string(r.draft_id)); };
auto recv_retry = [](Wire& w) noexcept -> RetryRequest {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return RetryRequest{std::atoi(s.data() + 6)};
};

// The client proposes a draft and the server verifies it.  An accept ends
// the session.  A reject rolls the client back onto a path that sends a
// fresh draft and waits for the next verdict.

using CommitPath = Send<Proposal, Recv<Accept, End>>;
using RollbackPath = Send<Proposal, Recv<Reject, Send<RetryRequest, Recv<Accept, End>>>>;

using ClientProto = CheckpointedSession<CommitPath, RollbackPath>;
using ServerProto = dual_of_t<ClientProto>;

static_assert(is_checkpointed_session_v<ClientProto>);
static_assert(is_well_formed_v<ClientProto>);
static_assert(is_well_formed_v<ServerProto>);

static_assert(std::is_same_v<dual_of_t<ServerProto>, ClientProto>);

static_assert(std::is_same_v<ServerProto,
                             CheckpointedSession<Recv<Proposal, Send<Accept, End>>,  // dual(CommitPath)
                                                 Recv<Proposal, Send<Reject, Recv<RetryRequest, Send<Accept, End>>>>>>);

int run_commit_path() {
    std::deque<std::string> wire;
    Wire c{&wire};
    Wire s{&wire};
    crucible::effects::HotFgCtx ctx{};

    auto [client, server] = mint_channel<ClientProto>(ctx, ctx, std::move(c), std::move(s));

    auto client_base = std::move(client).base();
    auto server_base = std::move(server).base();

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

int run_rollback_path() {
    std::deque<std::string> wire;
    Wire c{&wire};
    Wire s{&wire};
    crucible::effects::HotFgCtx ctx{};

    auto [client, server] = mint_channel<ClientProto>(ctx, ctx, std::move(c), std::move(s));

    // Both peers must already agree that this session is the rollback
    // variant.  Nothing on the wire tells them so.
    auto client_rb = std::move(client).rollback();
    auto server_rb = std::move(server).rollback();

    auto client2 = std::move(client_rb).send(Proposal{7}, send_proposal);
    auto [got_prop, server2] = std::move(server_rb).recv(recv_proposal);
    if (got_prop.draft_id != 7) {
        std::fprintf(stderr, "rollback: initial draft_id wrong (%d)\n", got_prop.draft_id);
        return 1;
    }

    auto server3 = std::move(server2).send(Reject{}, send_reject);
    auto [got_reject, client3] = std::move(client2).recv(recv_reject);
    (void)got_reject;

    auto client4 = std::move(client3).send(RetryRequest{99}, send_retry);
    auto [got_retry, server4] = std::move(server3).recv(recv_retry);
    if (got_retry.draft_id != 99) {
        std::fprintf(stderr, "rollback: retry draft_id wrong (%d)\n", got_retry.draft_id);
        return 1;
    }

    auto server5 = std::move(server4).send(Accept{}, send_accept);
    auto [got_final, client5] = std::move(client4).recv(recv_accept);
    (void)got_final;

    (void)std::move(client5).close();
    (void)std::move(server5).close();
    return 0;
}

consteval bool check_assert_matches() {
    assert_checkpointed_matches<ClientProto, CommitPath, RollbackPath>();
    return true;
}
static_assert(check_assert_matches());

static_assert(std::is_same_v<compose_t<ClientProto, End>, ClientProto>);

// Composing onto a checkpointed session extends both of its branches.
struct Extension {};
using ExtendedCkpt = compose_t<ClientProto, Send<Extension, End>>;

static_assert(is_checkpointed_session_v<ExtendedCkpt>);
static_assert(std::is_same_v<checkpoint_base_t<ExtendedCkpt>, compose_t<CommitPath, Send<Extension, End>>>);
static_assert(std::is_same_v<checkpoint_rollback_t<ExtendedCkpt>, compose_t<RollbackPath, Send<Extension, End>>>);

}  // anonymous namespace

int main() {
    if (int rc = run_commit_path(); rc != 0) return rc;
    if (int rc = run_rollback_path(); rc != 0) return rc;
    std::puts("session_checkpoint: CheckpointedSession .base()/.rollback() both paths OK");
    return 0;
}
