// GAPS-083: indexed SubstrateSessionBridge support for CalendarGrid.
//
// PermissionedCalendarGrid is a producer-row indexed priority calendar
// queue with one drain consumer.  This test verifies the bridge maps
// CalendarProducerId<P> / CalendarConsumerId onto the real handle types
// and preserves the queue's present-vs-empty observation through the
// existing std::optional<T> consumer surface.

#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/sessions/CalendarGridSession.h>

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
namespace cgs = ::crucible::safety::proto::calendar_grid_session;

struct BridgeTag {};
struct HeaderTag {};

struct Job {
    std::uint64_t deadline_ns = 0;
    std::uint64_t payload = 0;
};

struct DeadlineKey {
    static std::uint64_t key(Job const& job) noexcept {
        return job.deadline_ns;
    }
};

using Grid = cc::PermissionedCalendarGrid<
    Job,
    2,
    64,
    8,
    DeadlineKey,
    1ULL,
    BridgeTag>;

static_assert(cgs::CalendarGridSessionSurface<Grid>);
static_assert(cc::IsBridgeableShardDirection<Grid, cc::CalendarProducerId<0>,
                                             cc::Direction::Producer>);
static_assert(cc::IsBridgeableShardDirection<Grid, cc::CalendarConsumerId,
                                             cc::Direction::Consumer>);
static_assert(!cc::IsBridgeableDirection<Grid, cc::Direction::Producer>);
static_assert(!cc::IsBridgeableDirection<Grid, cc::Direction::Consumer>);

static_assert(std::is_same_v<
    cc::handle_for_t<Grid, cc::Direction::Producer,
                     cc::CalendarProducerId<1>>,
    Grid::ProducerHandle<1>>);
static_assert(std::is_same_v<
    cc::handle_for_t<Grid, cc::Direction::Consumer,
                     cc::CalendarConsumerId>,
    Grid::ConsumerHandle>);
static_assert(std::is_same_v<
    cc::default_proto_for_t<Grid, cc::Direction::Producer,
                            cc::CalendarProducerId<0>>,
    cgs::ProducerProto<Job>>);
static_assert(std::is_same_v<
    cc::default_proto_for_t<Grid, cc::Direction::Consumer,
                            cc::CalendarConsumerId>,
    cgs::ConsumerProto<Job>>);

template <typename UserTag, std::size_t M>
auto fresh_calendar_perms() {
    auto whole = safety::mint_permission_root<cc::calendar_tag::Whole<UserTag>>();
    return safety::mint_grid_permissions<cc::calendar_tag::Whole<UserTag>, M, 1>(
        std::move(whole));
}

template <typename Session>
void detach(Session& session) {
    std::move(session).detach(proto::detach_reason::TestInstrumentation{});
}

int test_bridge_64_bucket_half_present() {
    Grid grid;
    auto perms = fresh_calendar_perms<BridgeTag, 2>();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto consumer = grid.consumer(std::move(std::get<0>(perms.consumers)));

    auto ps0 = cc::mint_substrate_session<Grid, cc::CalendarProducerId<0>,
                                          cc::Direction::Producer>(
        ::crucible::effects::HotFgCtx{}, p0);
    auto ps1 = cc::mint_substrate_session<Grid, cc::CalendarProducerId<1>,
                                          cc::Direction::Producer>(
        ::crucible::effects::HotFgCtx{}, p1);
    auto cs = cc::mint_substrate_session<Grid, cc::CalendarConsumerId,
                                         cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, consumer);

    for (std::uint64_t slot = 0; slot < Grid::num_buckets; slot += 2) {
        Job job{.deadline_ns = slot, .payload = 1000 + slot};
        if ((slot / 2) % 2 == 0) {
            auto next = std::move(ps0).send(job, cgs::blocking_push);
            ps0 = std::move(next);
        } else {
            auto next = std::move(ps1).send(job, cgs::blocking_push);
            ps1 = std::move(next);
        }
    }

    std::array<bool, Grid::num_buckets> present{};
    std::uint64_t payload_sum = 0;
    for (std::size_t count = 0; count < Grid::num_buckets / 2; ++count) {
        auto [job, next] = std::move(cs).recv(cgs::blocking_pop);
        assert(job.deadline_ns < Grid::num_buckets);
        present[job.deadline_ns] = true;
        payload_sum += job.payload;
        cs = std::move(next);
    }

    for (std::size_t slot = 0; slot < Grid::num_buckets; ++slot) {
        assert(present[slot] == (slot % 2 == 0));
    }
    assert(payload_sum == 32ULL * 1000ULL + 2ULL * (31ULL * 32ULL / 2ULL));
    assert(!cgs::consumer_session_try_pop<Grid>(cs).has_value());
    assert(grid.empty_approx());

    detach(ps0);
    detach(ps1);
    detach(cs);
    return 0;
}

int test_session_header_factories() {
    using HeaderGrid = cc::PermissionedCalendarGrid<
        Job, 1, 8, 4, DeadlineKey, 1ULL, HeaderTag>;

    HeaderGrid grid;
    auto perms = fresh_calendar_perms<HeaderTag, 1>();
    auto producer = cgs::mint_calendar_grid_producer<HeaderGrid, 0>(
        grid, std::move(std::get<0>(perms.producers)));
    auto consumer = cgs::mint_calendar_grid_consumer<HeaderGrid>(
        grid, std::move(std::get<0>(perms.consumers)));

    auto ps = cgs::mint_producer_session<HeaderGrid, 0>(
        ::crucible::effects::HotFgCtx{}, producer);
    auto cs = cgs::mint_consumer_session<HeaderGrid>(
        ::crucible::effects::HotFgCtx{}, consumer);

    auto next_ps = std::move(ps).send(
        Job{.deadline_ns = 3, .payload = 42}, cgs::blocking_push);
    auto [job, next_cs] = std::move(cs).recv(cgs::blocking_pop);
    assert(job.payload == 42);
    assert(!cgs::consumer_session_try_pop<HeaderGrid>(next_cs).has_value());
    detach(next_ps);
    detach(next_cs);
    assert(grid.empty_approx());
    return 0;
}

}  // namespace

int main() {
    if (int rc = test_bridge_64_bucket_half_present(); rc != 0) return rc;
    if (int rc = test_session_header_factories(); rc != 0) return 10 + rc;
    std::puts("calendar_grid_substrate_session_bridge: OK");
    return 0;
}
