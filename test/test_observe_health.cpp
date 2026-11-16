#include <crucible/observe/Health.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include "test_assert.h"

namespace {

namespace observe = ::crucible::observe;
namespace topology = ::crucible::topology;

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_REQUIRE(cond)                                                                 \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
            ++total_failed;                                                                    \
            return;                                                                            \
        }                                                                                      \
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
    snapshot.wear_used_ppm = 789000;
    snapshot.sequence = 42;
    return snapshot;
}

void test_metric_ids_are_partitioned_by_peer_slot() {
    std::uint32_t const peer_3_score = observe::topology_health_metric_id(3, observe::HealthMetricSlot::Score);
    std::uint32_t const peer_4_score = observe::topology_health_metric_id(4, observe::HealthMetricSlot::Score);
    std::uint32_t const peer_3_phi = observe::topology_health_metric_id(3, observe::HealthMetricSlot::PhiMilli);

    CRUCIBLE_REQUIRE(peer_3_score != peer_4_score);
    CRUCIBLE_REQUIRE(peer_3_score != peer_3_phi);
    CRUCIBLE_REQUIRE(peer_3_score
                     == observe::pack_metric_id(observe::MetricFamily::TopologyHealth, 3,
                                                static_cast<std::uint8_t>(observe::HealthMetricSlot::Score)));
}

// Each id gives back its three fields, so no two pairs of a peer and a slot
// share an id.  The walk reads each of the 65536 peer slots with each metric
// slot.  Complexity: O(65536 * slots).
void test_metric_id_gives_back_its_fields() {
    constexpr unsigned kSlotShift = 0;
    constexpr unsigned kIndexShift = kSlotShift + observe::kMetricSlotBits;
    constexpr unsigned kFamilyShift = kIndexShift + observe::kMetricIndexBits;
    for (std::uint32_t peer = 0; peer <= ::fixy::max_of<std::uint16_t>; ++peer) {
        for (std::uint32_t slot_index = 0; slot_index < observe::kTopologyHealthObservationCount; ++slot_index) {
            std::uint32_t const id = observe::topology_health_metric_id(
                static_cast<std::uint16_t>(peer), static_cast<observe::HealthMetricSlot>(slot_index));
            CRUCIBLE_REQUIRE((id >> kFamilyShift) == static_cast<std::uint32_t>(observe::MetricFamily::TopologyHealth));
            CRUCIBLE_REQUIRE(((id >> kIndexShift) & 0xFFFFu) == peer);
            CRUCIBLE_REQUIRE(((id >> kSlotShift) & 0xFFu) == slot_index);
        }
    }
}

// Peer 256 differs from peer 0 only in the ninth bit of its index, and the id
// keeps that bit.
void test_metric_id_keeps_each_bit_of_the_peer() {
    for (std::size_t slot_index = 0; slot_index < observe::kTopologyHealthObservationCount; ++slot_index) {
        auto const slot = static_cast<observe::HealthMetricSlot>(slot_index);
        CRUCIBLE_REQUIRE(observe::topology_health_metric_id(0, slot) != observe::topology_health_metric_id(256, slot));
    }
}

// Two different pairs of a peer and a slot never share an id.  The second
// peer of each sample is the first peer with one bit changed, or a second
// random peer.  The samples come from Philox with a fixed key, so each run
// reads the same samples.
void test_random_peers_never_share_an_id() {
    constexpr std::uint64_t kSampleCount = std::uint64_t{1} << 16;
    constexpr std::uint32_t kSlotCount = observe::kTopologyHealthObservationCount;
    for (std::uint64_t offset = 0; offset < kSampleCount; ++offset) {
        ::fixy::Philox4x32::Counter const block = ::fixy::Philox4x32::generate(offset, 0x4845414C5448ull);
        std::uint32_t const word0 = *block.get(0).expect("a block holds four words");
        std::uint32_t const word1 = *block.get(1).expect("a block holds four words");
        std::uint32_t const word2 = *block.get(2).expect("a block holds four words");
        std::uint32_t const word3 = *block.get(3).expect("a block holds four words");
        auto const peer = static_cast<std::uint16_t>(word0);
        auto const flipped = static_cast<std::uint16_t>(peer ^ (1u << (word1 % 16u)));
        auto const other = static_cast<std::uint16_t>(word1 >> 16u);
        auto const slot = static_cast<observe::HealthMetricSlot>(word2 % kSlotCount);
        auto const other_slot = static_cast<observe::HealthMetricSlot>(word3 % kSlotCount);
        std::uint32_t const id = observe::topology_health_metric_id(peer, slot);
        CRUCIBLE_REQUIRE(id != observe::topology_health_metric_id(flipped, slot));
        bool const is_same_pair = peer == other && slot == other_slot;
        CRUCIBLE_REQUIRE(is_same_pair == (id == observe::topology_health_metric_id(other, other_slot)));
    }
}

void test_snapshot_maps_to_fixed_observation_batch() {
    static_assert(std::is_trivially_copyable_v<observe::TopologyHealthObservationBatch>);

    auto const batch = observe::topology_health_observations(7, sample_snapshot(), observe::ObservationSource::Keeper);

    CRUCIBLE_REQUIRE(batch.at(::fixy::index<0>()).kind == observe::ObservationKind::HealthScore);
    CRUCIBLE_REQUIRE(batch.at(::fixy::index<0>()).source == observe::ObservationSource::Keeper);
    CRUCIBLE_REQUIRE(batch.at(::fixy::index<0>()).value == 777);
    CRUCIBLE_REQUIRE(batch.at(::fixy::index<0>()).sequence == 42);
    CRUCIBLE_REQUIRE(batch.at(::fixy::index<1>()).kind == observe::ObservationKind::PhiMilli);
    CRUCIBLE_REQUIRE(batch.at(::fixy::index<1>()).value == 1234);
    CRUCIBLE_REQUIRE(batch.at(::fixy::index<2>()).kind == observe::ObservationKind::DropRatePpm);
    CRUCIBLE_REQUIRE(batch.at(::fixy::index<2>()).value == 456);
    CRUCIBLE_REQUIRE(batch.at(::fixy::index<3>()).kind == observe::ObservationKind::WearUsedPpm);
    CRUCIBLE_REQUIRE(batch.at(::fixy::index<3>()).value == 789000);
}

void test_publish_updates_dedicated_latest_value_sinks() {
    observe::TopologyHealthObservationSet sinks{};

    observe::publish_topology_health(sinks, 2, sample_snapshot(), observe::ObservationSource::Runtime);

    CRUCIBLE_REQUIRE(observe::latest_observation(sinks.at(::fixy::index<0>())).kind
                     == observe::ObservationKind::HealthScore);
    CRUCIBLE_REQUIRE(observe::latest_observation(sinks.at(::fixy::index<1>())).kind
                     == observe::ObservationKind::PhiMilli);
    CRUCIBLE_REQUIRE(observe::latest_observation(sinks.at(::fixy::index<2>())).kind
                     == observe::ObservationKind::DropRatePpm);
    CRUCIBLE_REQUIRE(observe::latest_observation(sinks.at(::fixy::index<3>())).kind
                     == observe::ObservationKind::WearUsedPpm);
    CRUCIBLE_REQUIRE(observe::latest_observation(sinks.at(::fixy::index<3>())).metric_id
                     == observe::topology_health_metric_id(2, observe::HealthMetricSlot::WearUsedPpm));
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_observe_health]\n");
    run_test("metric_ids_are_partitioned_by_peer_slot", test_metric_ids_are_partitioned_by_peer_slot);
    run_test("metric_id_gives_back_its_fields", test_metric_id_gives_back_its_fields);
    run_test("metric_id_keeps_each_bit_of_the_peer", test_metric_id_keeps_each_bit_of_the_peer);
    run_test("random_peers_never_share_an_id", test_random_peers_never_share_an_id);
    run_test("snapshot_maps_to_fixed_observation_batch", test_snapshot_maps_to_fixed_observation_batch);
    run_test("publish_updates_dedicated_latest_value_sinks", test_publish_updates_dedicated_latest_value_sinks);

    crucible::test::pass("\n{} passed, {} failed\n", total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
