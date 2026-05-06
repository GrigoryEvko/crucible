#include <crucible/sessions/FederationProtocol.h>

#include "test_assert.h"

#include <cstdio>
#include <type_traits>
#include <vector>

namespace fp = crucible::safety::proto::federation;
namespace proto = crucible::safety::proto;

namespace {

struct TraceKey {};

struct Endpoint {
    std::vector<int>* events = nullptr;
};

constexpr crucible::KernelCacheKey kKey{
    crucible::ContentHash{0x1111'2222'3333'4444ULL},
    crucible::RowHash{0xAAAA'BBBB'CCCC'DDDDULL},
};

static_assert(proto::is_global_well_formed_v<fp::FederationGlobal<TraceKey>>);
static_assert(std::is_same_v<
    fp::SenderProto<TraceKey>,
    fp::ExpectedSenderProto<TraceKey>>);
static_assert(std::is_same_v<
    fp::ReceiverProto<TraceKey>,
    fp::ExpectedReceiverProto<TraceKey>>);
static_assert(std::is_same_v<
    fp::CoordProto<TraceKey>,
    fp::ExpectedCoordProto<TraceKey>>);
static_assert(fp::role_protocol_matches_v<
    fp::SenderRole, fp::SenderProto<TraceKey>, TraceKey>);
static_assert(fp::role_protocol_matches_v<
    fp::ReceiverRole, fp::ReceiverProto<TraceKey>, TraceKey>);
static_assert(fp::role_protocol_matches_v<
    fp::CoordRole, fp::CoordProto<TraceKey>, TraceKey>);
static_assert(!fp::role_protocol_matches_v<
    fp::CoordRole, fp::SenderProto<TraceKey>, TraceKey>);

int test_sender_receiver_views() {
    std::vector<int> events;
    auto [sender, receiver] =
        fp::mint_channel<TraceKey>(Endpoint{&events}, Endpoint{&events});

    auto send_header = [](Endpoint& ep,
                          fp::HeaderPayload<TraceKey>&&) noexcept {
        ep.events->push_back(1);
    };
    auto recv_ack = [](Endpoint& ep) noexcept -> fp::Ack<TraceKey> {
        ep.events->push_back(2);
        return fp::Ack<TraceKey>{.key = kKey};
    };
    auto recv_pull = [](Endpoint& ep) noexcept -> fp::PullRequest<TraceKey> {
        ep.events->push_back(3);
        return fp::PullRequest<TraceKey>{.key = kKey};
    };
    auto send_body = [](Endpoint& ep,
                        fp::BodyPayload<TraceKey>&&) noexcept {
        ep.events->push_back(4);
    };

    auto s1 = std::move(sender).send(fp::HeaderPayload<TraceKey>{}, send_header);
    auto [ack, s2] = std::move(s1).recv(recv_ack);
    auto [pull, r1] = std::move(receiver).recv(recv_pull);
    auto r2 = std::move(r1).send(fp::BodyPayload<TraceKey>{}, send_body);

    assert(ack.key == kKey);
    assert(pull.key == kKey);
    assert(events == std::vector<int>({1, 2, 3, 4}));

    std::move(s2).detach(proto::detach_reason::InfiniteLoopProtocol{});
    std::move(r2).detach(proto::detach_reason::InfiniteLoopProtocol{});
    return 0;
}

int test_coord_view() {
    std::vector<int> events;
    auto coord = fp::mint_coord<TraceKey>(Endpoint{&events});

    auto recv_header = [](Endpoint& ep) noexcept -> fp::HeaderPayload<TraceKey> {
        ep.events->push_back(10);
        return fp::HeaderPayload<TraceKey>{};
    };
    auto send_ack = [](Endpoint& ep, fp::Ack<TraceKey>&& ack) noexcept {
        assert(ack.key == kKey);
        ep.events->push_back(11);
    };
    auto send_pull = [](Endpoint& ep,
                        fp::PullRequest<TraceKey>&& pull) noexcept {
        assert(pull.key == kKey);
        ep.events->push_back(12);
    };
    auto recv_body = [](Endpoint& ep) noexcept -> fp::BodyPayload<TraceKey> {
        ep.events->push_back(13);
        return fp::BodyPayload<TraceKey>{};
    };

    auto [header, c1] = std::move(coord).recv(recv_header);
    (void)header;
    auto c2 = std::move(c1).send(fp::Ack<TraceKey>{.key = kKey}, send_ack);
    auto c3 = std::move(c2).send(
        fp::PullRequest<TraceKey>{.key = kKey}, send_pull);
    auto [body, c4] = std::move(c3).recv(recv_body);
    (void)body;

    assert(events == std::vector<int>({10, 11, 12, 13}));
    std::move(c4).detach(proto::detach_reason::InfiniteLoopProtocol{});
    return 0;
}

}  // namespace

int main() {
    if (int rc = test_sender_receiver_views(); rc != 0) return rc;
    if (int rc = test_coord_view(); rc != 0) return rc;
    std::puts("federation_session: projected MPST views OK");
    return 0;
}
