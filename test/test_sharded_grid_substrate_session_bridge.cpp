// GAPS-082: indexed SubstrateSessionBridge support for ShardedGrid.
//
// Verifies that ShardId<I> selects the correct statically-indexed
// ProducerHandle<I> / ConsumerHandle<J>, and that the generic substrate
// session factory composes with the ShardedGrid session facade.

#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/sessions/ShardedGridSession.h>

#include <cassert>
#include <cstdio>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

namespace cc = ::crucible::concurrent;
namespace proto = ::crucible::safety::proto;
namespace safety = ::crucible::safety;
namespace sgs = ::crucible::safety::proto::sharded_grid_session;

struct BridgeTag {};
struct HeaderTag {};

using Grid = cc::PermissionedShardedGrid<int, 4, 8, 64, BridgeTag>;

static_assert(sgs::ShardedGridSessionSurface<Grid>);
static_assert(cc::IsBridgeableShardDirection<Grid, cc::ShardId<0>,
                                             cc::Direction::Producer>);
static_assert(cc::IsBridgeableShardDirection<Grid, cc::ShardId<7>,
                                             cc::Direction::Consumer>);
static_assert(!cc::IsBridgeableDirection<Grid, cc::Direction::Producer>);

static_assert(std::is_same_v<
    cc::handle_for_t<Grid, cc::Direction::Producer, cc::ShardId<3>>,
    Grid::ProducerHandle<3>>);
static_assert(std::is_same_v<
    cc::handle_for_t<Grid, cc::Direction::Consumer, cc::ShardId<7>>,
    Grid::ConsumerHandle<7>>);
static_assert(std::is_same_v<
    cc::default_proto_for_t<Grid, cc::Direction::Producer, cc::ShardId<0>>,
    sgs::ProducerProto<int>>);
static_assert(std::is_same_v<
    cc::default_proto_for_t<Grid, cc::Direction::Consumer, cc::ShardId<0>>,
    sgs::ConsumerProto<int>>);

template <typename UserTag, std::size_t M, std::size_t N>
auto fresh_grid_perms() {
    auto whole = safety::mint_permission_root<cc::grid_tag::Whole<UserTag>>();
    return safety::split_grid<cc::grid_tag::Whole<UserTag>, M, N>(
        std::move(whole));
}

template <typename Session>
void push_eight(Session& session, int base) {
    for (int offset = 0; offset < 8; ++offset) {
        auto next = std::move(session).send(base + offset, sgs::blocking_push);
        session = std::move(next);
    }
}

template <typename Session>
int drain_four(Session& session) {
    int sum = 0;
    for (int count = 0; count < 4; ++count) {
        auto [value, next] = std::move(session).recv(sgs::blocking_pop);
        sum += value;
        session = std::move(next);
    }
    return sum;
}

template <typename Session>
void detach(Session& session) {
    std::move(session).detach(proto::detach_reason::TestInstrumentation{});
}

int test_bridge_4x8_round_robin_delivery() {
    Grid grid;
    auto perms = fresh_grid_perms<BridgeTag, 4, 8>();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));

    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
    auto c3 = grid.template consumer<3>(std::move(std::get<3>(perms.consumers)));
    auto c4 = grid.template consumer<4>(std::move(std::get<4>(perms.consumers)));
    auto c5 = grid.template consumer<5>(std::move(std::get<5>(perms.consumers)));
    auto c6 = grid.template consumer<6>(std::move(std::get<6>(perms.consumers)));
    auto c7 = grid.template consumer<7>(std::move(std::get<7>(perms.consumers)));

    auto ps0 = cc::mint_substrate_session<Grid, cc::ShardId<0>,
                                          cc::Direction::Producer>(
        ::crucible::effects::HotFgCtx{}, p0);
    auto ps1 = cc::mint_substrate_session<Grid, cc::ShardId<1>,
                                          cc::Direction::Producer>(
        ::crucible::effects::HotFgCtx{}, p1);
    auto ps2 = cc::mint_substrate_session<Grid, cc::ShardId<2>,
                                          cc::Direction::Producer>(
        ::crucible::effects::HotFgCtx{}, p2);
    auto ps3 = cc::mint_substrate_session<Grid, cc::ShardId<3>,
                                          cc::Direction::Producer>(
        ::crucible::effects::HotFgCtx{}, p3);

    auto cs0 = cc::mint_substrate_session<Grid, cc::ShardId<0>,
                                          cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, c0);
    auto cs1 = cc::mint_substrate_session<Grid, cc::ShardId<1>,
                                          cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, c1);
    auto cs2 = cc::mint_substrate_session<Grid, cc::ShardId<2>,
                                          cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, c2);
    auto cs3 = cc::mint_substrate_session<Grid, cc::ShardId<3>,
                                          cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, c3);
    auto cs4 = cc::mint_substrate_session<Grid, cc::ShardId<4>,
                                          cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, c4);
    auto cs5 = cc::mint_substrate_session<Grid, cc::ShardId<5>,
                                          cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, c5);
    auto cs6 = cc::mint_substrate_session<Grid, cc::ShardId<6>,
                                          cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, c6);
    auto cs7 = cc::mint_substrate_session<Grid, cc::ShardId<7>,
                                          cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, c7);

    push_eight(ps0, 0);
    push_eight(ps1, 100);
    push_eight(ps2, 200);
    push_eight(ps3, 300);

    assert(drain_four(cs0) == 600);
    assert(drain_four(cs1) == 604);
    assert(drain_four(cs2) == 608);
    assert(drain_four(cs3) == 612);
    assert(drain_four(cs4) == 616);
    assert(drain_four(cs5) == 620);
    assert(drain_four(cs6) == 624);
    assert(drain_four(cs7) == 628);
    assert(grid.empty_approx());

    detach(ps0);
    detach(ps1);
    detach(ps2);
    detach(ps3);
    detach(cs0);
    detach(cs1);
    detach(cs2);
    detach(cs3);
    detach(cs4);
    detach(cs5);
    detach(cs6);
    detach(cs7);
    return 0;
}

int test_session_header_factories() {
    using HeaderGrid = cc::PermissionedShardedGrid<int, 1, 1, 16, HeaderTag>;

    HeaderGrid grid;
    auto perms = fresh_grid_perms<HeaderTag, 1, 1>();
    auto producer = sgs::mint_sharded_grid_producer<HeaderGrid, 0>(
        grid, std::move(std::get<0>(perms.producers)));
    auto consumer = sgs::mint_sharded_grid_consumer<HeaderGrid, 0>(
        grid, std::move(std::get<0>(perms.consumers)));

    auto ps = sgs::mint_producer_session<HeaderGrid, 0>(
        ::crucible::effects::HotFgCtx{}, producer);
    auto cs = sgs::mint_consumer_session<HeaderGrid, 0>(
        ::crucible::effects::HotFgCtx{}, consumer);

    auto next_ps = std::move(ps).send(42, sgs::blocking_push);
    auto [value, next_cs] = std::move(cs).recv(sgs::blocking_pop);
    assert(value == 42);
    detach(next_ps);
    detach(next_cs);
    assert(grid.empty_approx());
    return 0;
}

}  // namespace

int main() {
    if (int rc = test_bridge_4x8_round_robin_delivery(); rc != 0) return rc;
    if (int rc = test_session_header_factories(); rc != 0) return 10 + rc;
    std::puts("sharded_grid_substrate_session_bridge: OK");
    return 0;
}
