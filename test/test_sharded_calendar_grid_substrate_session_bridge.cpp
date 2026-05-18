// GAPS-084: indexed SubstrateSessionBridge support for ShardedCalendarGrid.
//
// The live substrate exposes one producer and one consumer per shard.
// ShardId<S> selects that shard-local calendar queue; bucket slots are
// observed through the item's priority key and std::optional<T> empty
// result, not through separate per-slot handle types.

#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/sessions/ShardedCalendarGridSession.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

namespace cc = ::crucible::concurrent;
namespace proto = ::crucible::safety::proto;
namespace safety = ::crucible::safety;
namespace scgs = ::crucible::safety::proto::sharded_calendar_grid_session;

struct BridgeTag {};
struct HeaderTag {};

struct Job {
    std::uint64_t shard = 0;
    std::uint64_t deadline_ns = 0;
    std::uint64_t payload = 0;
};

struct DeadlineKey {
    static std::uint64_t key(Job const& job) noexcept {
        return job.deadline_ns;
    }
};

using Grid = cc::PermissionedShardedCalendarGrid<
    Job,
    4,
    64,
    8,
    DeadlineKey,
    1ULL,
    BridgeTag>;

static_assert(scgs::ShardedCalendarGridSessionSurface<Grid>);
static_assert(cc::IsBridgeableShardDirection<Grid, cc::ShardId<0>,
                                             cc::Direction::Producer>);
static_assert(cc::IsBridgeableShardDirection<Grid, cc::ShardId<3>,
                                             cc::Direction::Consumer>);
static_assert(!cc::IsBridgeableDirection<Grid, cc::Direction::Producer>);
static_assert(!cc::IsBridgeableDirection<Grid, cc::Direction::Consumer>);

static_assert(std::is_same_v<
    cc::handle_for_t<Grid, cc::Direction::Producer, cc::ShardId<2>>,
    Grid::ProducerHandle<2>>);
static_assert(std::is_same_v<
    cc::handle_for_t<Grid, cc::Direction::Consumer, cc::ShardId<2>>,
    Grid::ConsumerHandle<2>>);
static_assert(std::is_same_v<
    cc::default_proto_for_t<Grid, cc::Direction::Producer, cc::ShardId<0>>,
    scgs::ProducerProto<Job>>);
static_assert(std::is_same_v<
    cc::default_proto_for_t<Grid, cc::Direction::Consumer, cc::ShardId<0>>,
    scgs::ConsumerProto<Job>>);

template <typename UserTag, std::size_t Shards>
auto fresh_sharded_calendar_perms() {
    auto whole = safety::mint_permission_root<
        cc::sharded_calendar_tag::Whole<UserTag>>();
    return safety::mint_grid_permissions<
        cc::sharded_calendar_tag::Whole<UserTag>, Shards, Shards>(
        std::move(whole));
}

template <typename Session>
void detach(Session& session) {
    std::move(session).detach(proto::detach_reason::TestInstrumentation{});
}

template <std::size_t Shard, typename Session>
void push_even_slots(Session& session) {
    for (std::uint64_t slot = 0; slot < Grid::num_buckets; slot += 2) {
        auto next = std::move(session).send(
            Job{.shard = Shard,
                .deadline_ns = slot,
                .payload = Shard * 1000ULL + slot},
            scgs::blocking_push);
        session = std::move(next);
    }
}

template <std::size_t Shard, typename Session>
void drain_and_check_even_slots(Session& session) {
    std::array<bool, Grid::num_buckets> present{};
    std::uint64_t payload_sum = 0;

    for (std::size_t count = 0; count < Grid::num_buckets / 2; ++count) {
        auto [job, next] = std::move(session).recv(scgs::blocking_pop);
        assert(job.shard == Shard);
        assert(job.deadline_ns < Grid::num_buckets);
        present[job.deadline_ns] = true;
        payload_sum += job.payload;
        session = std::move(next);
    }

    for (std::size_t slot = 0; slot < Grid::num_buckets; ++slot) {
        assert(present[slot] == (slot % 2 == 0));
    }

    constexpr std::uint64_t expected_slot_sum = 2ULL * (31ULL * 32ULL / 2ULL);
    assert(payload_sum == 32ULL * Shard * 1000ULL + expected_slot_sum);
    assert((!scgs::consumer_session_try_pop<Grid, Shard>(session).has_value()));
}

int test_bridge_4_shard_64_bucket_half_present() {
    Grid grid;
    auto perms = fresh_sharded_calendar_perms<BridgeTag, 4>();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));

    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
    auto c3 = grid.template consumer<3>(std::move(std::get<3>(perms.consumers)));

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

    push_even_slots<0>(ps0);
    push_even_slots<1>(ps1);
    push_even_slots<2>(ps2);
    push_even_slots<3>(ps3);

    drain_and_check_even_slots<0>(cs0);
    drain_and_check_even_slots<1>(cs1);
    drain_and_check_even_slots<2>(cs2);
    drain_and_check_even_slots<3>(cs3);

    assert(grid.empty_approx());

    detach(ps0);
    detach(ps1);
    detach(ps2);
    detach(ps3);
    detach(cs0);
    detach(cs1);
    detach(cs2);
    detach(cs3);
    return 0;
}

int test_session_header_factories() {
    using HeaderGrid = cc::PermissionedShardedCalendarGrid<
        Job, 1, 8, 4, DeadlineKey, 1ULL, HeaderTag>;

    HeaderGrid grid;
    auto perms = fresh_sharded_calendar_perms<HeaderTag, 1>();
    auto producer = scgs::mint_sharded_calendar_grid_producer<HeaderGrid, 0>(
        grid, std::move(std::get<0>(perms.producers)));
    auto consumer = scgs::mint_sharded_calendar_grid_consumer<HeaderGrid, 0>(
        grid, std::move(std::get<0>(perms.consumers)));

    auto ps = scgs::mint_producer_session<HeaderGrid, 0>(
        ::crucible::effects::HotFgCtx{}, producer);
    auto cs = scgs::mint_consumer_session<HeaderGrid, 0>(
        ::crucible::effects::HotFgCtx{}, consumer);

    auto next_ps = std::move(ps).send(
        Job{.shard = 0, .deadline_ns = 3, .payload = 42},
        scgs::blocking_push);
    auto [job, next_cs] = std::move(cs).recv(scgs::blocking_pop);
    assert(job.payload == 42);
    assert((!scgs::consumer_session_try_pop<HeaderGrid, 0>(next_cs).has_value()));
    detach(next_ps);
    detach(next_cs);
    assert(grid.empty_approx());
    return 0;
}

}  // namespace

int main() {
    if (int rc = test_bridge_4_shard_64_bucket_half_present(); rc != 0) {
        return rc;
    }
    if (int rc = test_session_header_factories(); rc != 0) return 10 + rc;
    std::puts("sharded_calendar_grid_substrate_session_bridge: OK");
    return 0;
}
