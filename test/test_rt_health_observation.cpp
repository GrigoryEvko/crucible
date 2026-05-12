#include <crucible/rt/HealthObservation.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace {

namespace rt = ::crucible::rt;
namespace topology = ::crucible::topology;

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_REQUIRE(cond)                                            \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n",        \
                         #cond, __FILE__, __LINE__);                      \
            ++total_failed;                                               \
            return;                                                       \
        }                                                                 \
    } while (0)

template <typename Body>
void run_test(char const* name, Body body) {
    std::fprintf(stderr, "  %s ... ", name);
    int const before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        std::fprintf(stderr, "OK\n");
    } else {
        std::fprintf(stderr, "FAILED\n");
    }
}

topology::HealthSnapshot sample_snapshot() noexcept {
    topology::HealthSnapshot snapshot{};
    snapshot.score = topology::HealthScore{777};
    snapshot.phi = topology::PhiMilli{1234};
    snapshot.drop_rate_ppm = 456;
    snapshot.wear_used_ppm = 789'000;
    snapshot.sequence = 42;
    return snapshot;
}

void test_metric_ids_are_partitioned_by_peer_slot() {
    std::uint32_t const peer_3_score = rt::topology_health_metric_id(
        3, rt::HealthMetricSlot::Score);
    std::uint32_t const peer_4_score = rt::topology_health_metric_id(
        4, rt::HealthMetricSlot::Score);
    std::uint32_t const peer_3_phi = rt::topology_health_metric_id(
        3, rt::HealthMetricSlot::PhiMilli);

    CRUCIBLE_REQUIRE(peer_3_score != peer_4_score);
    CRUCIBLE_REQUIRE(peer_3_score != peer_3_phi);
    CRUCIBLE_REQUIRE((peer_3_score & 0xFFFF'0000u)
                     == rt::kTopologyHealthMetricBase);
}

void test_snapshot_maps_to_fixed_observation_batch() {
    static_assert(std::is_trivially_copyable_v<
                  rt::TopologyHealthObservationBatch>);
    static_assert(rt::kTopologyHealthObservationCount == 4);

    auto const batch = rt::topology_health_observations(
        7, sample_snapshot(), rt::ObservationSource::Keeper);

    CRUCIBLE_REQUIRE(batch[0].kind == rt::ObservationKind::HealthScore);
    CRUCIBLE_REQUIRE(batch[0].source == rt::ObservationSource::Keeper);
    CRUCIBLE_REQUIRE(batch[0].value == 777);
    CRUCIBLE_REQUIRE(batch[0].sequence == 42);
    CRUCIBLE_REQUIRE(batch[1].kind == rt::ObservationKind::PhiMilli);
    CRUCIBLE_REQUIRE(batch[1].value == 1234);
    CRUCIBLE_REQUIRE(batch[2].kind == rt::ObservationKind::DropRatePpm);
    CRUCIBLE_REQUIRE(batch[2].value == 456);
    CRUCIBLE_REQUIRE(batch[3].kind == rt::ObservationKind::WearUsedPpm);
    CRUCIBLE_REQUIRE(batch[3].value == 789'000);
}

void test_publish_updates_dedicated_latest_value_sinks() {
    rt::TopologyHealthObservationSet sinks{
        rt::ObservationSnapshot{},
        rt::ObservationSnapshot{},
        rt::ObservationSnapshot{},
        rt::ObservationSnapshot{},
    };

    rt::publish_topology_health(
        sinks, 2, sample_snapshot(), rt::ObservationSource::Runtime);

    CRUCIBLE_REQUIRE(rt::latest_observation(sinks[0]).kind
                     == rt::ObservationKind::HealthScore);
    CRUCIBLE_REQUIRE(rt::latest_observation(sinks[1]).kind
                     == rt::ObservationKind::PhiMilli);
    CRUCIBLE_REQUIRE(rt::latest_observation(sinks[2]).kind
                     == rt::ObservationKind::DropRatePpm);
    CRUCIBLE_REQUIRE(rt::latest_observation(sinks[3]).kind
                     == rt::ObservationKind::WearUsedPpm);
    CRUCIBLE_REQUIRE(rt::latest_observation(sinks[3]).metric_id
        == rt::topology_health_metric_id(2, rt::HealthMetricSlot::WearUsedPpm));
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_rt_health_observation]\n");
    run_test("metric_ids_are_partitioned_by_peer_slot",
             test_metric_ids_are_partitioned_by_peer_slot);
    run_test("snapshot_maps_to_fixed_observation_batch",
             test_snapshot_maps_to_fixed_observation_batch);
    run_test("publish_updates_dedicated_latest_value_sinks",
             test_publish_updates_dedicated_latest_value_sinks);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
