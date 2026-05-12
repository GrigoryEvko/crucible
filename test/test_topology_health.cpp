#include <crucible/topology/Health.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>

namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace safety = crucible::safety;
namespace topology = crucible::topology;

static cog::CogIdentity peer(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x113, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

static topology::HealthPolicy test_policy() {
    topology::HealthPolicy policy{};
    policy.expected_heartbeat_ns =
        topology::PositiveNanoseconds{std::uint64_t{1'000}};
    policy.suspect_phi = topology::PhiMilli{2'000};
    policy.quarantine_phi = topology::PhiMilli{4'000};
    policy.suspect_below = topology::HealthScore{700};
    policy.quarantine_below = topology::HealthScore{350};
    policy.recovered_at_or_above = topology::HealthScore{900};
    policy.corrected_ecc_warn_delta = 4;
    policy.drop_warn_ppm = 1'000;
    policy.drop_critical_ppm = 10'000;
    return policy;
}

static topology::EccCounters ecc(std::uint64_t corrected,
                                 std::uint64_t uncorrected,
                                 std::uint64_t sequence) {
    topology::EccCounters counters{};
    counters.corrected.advance(corrected);
    counters.uncorrected.advance(uncorrected);
    counters.sequence = sequence;
    return counters;
}

static topology::DropCounters drops(std::uint64_t packets,
                                    std::uint64_t dropped,
                                    std::uint64_t sequence) {
    topology::DropCounters counters{};
    counters.rx_packets.advance(packets);
    counters.rx_dropped.advance(dropped);
    counters.sequence = sequence;
    return counters;
}

static void test_name_accessors() {
    assert(topology::health_state_name(topology::HealthState::Quarantined)
           == std::string_view{"Quarantined"});
    assert(topology::health_issue_name(topology::HealthIssue::DropRateCritical)
           == std::string_view{"DropRateCritical"});
    std::printf("  test_name_accessors:              PASSED\n");
}

static void test_healthy_snapshot_is_stale_wrapped() {
    auto scorer = topology::mint_topology_health<effects::ColdInitCtx, 4, 4>(
        effects::ColdInitCtx{}, test_policy());
    auto const p = peer(1);
    assert(scorer.record_heartbeat(effects::BgDrainCtx{}, p, 1'000, 1));
    assert(scorer.record_heartbeat(effects::BgDrainCtx{}, p, 2'000, 2));
    assert(scorer.update_thermal(effects::BgDrainCtx{}, p,
        topology::ThermalSample{
            .temperature_millicelsius = 45'000,
            .clock_degraded_pct = 0,
            .sequence = 3,
        }));
    assert(scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(0, 0, 4)));
    assert(scorer.update_drops(effects::BgDrainCtx{}, p, drops(100'000, 0, 5)));
    assert(scorer.update_wear(effects::BgDrainCtx{}, p,
        topology::WearSample{.used_ppm = 100'000, .sequence = 6}));

    auto snapshot = scorer.compute(p, 2'500, 7);
    assert(snapshot.peek().state == topology::HealthState::Healthy);
    assert(snapshot.peek().score.raw() > 900);
    assert(snapshot.peek().phi.raw() < test_policy().suspect_phi.raw());
    assert(snapshot.peek().issues.none());
    assert(snapshot.is_finite());
    auto current = scorer.current(p, 9);
    assert(current.peek().state == topology::HealthState::Healthy);
    assert(current.staleness().value == 2);
    std::printf("  test_healthy_snapshot_is_stale_wrapped: PASSED\n");
}

static void test_phi_delay_drives_suspect_state() {
    auto scorer = topology::mint_topology_health<effects::ColdInitCtx, 2, 4>(
        effects::ColdInitCtx{}, test_policy());
    auto const p = peer(2);
    assert(scorer.record_heartbeat(effects::BgDrainCtx{}, p, 1'000, 1));
    assert(scorer.record_heartbeat(effects::BgDrainCtx{}, p, 2'000, 2));
    assert(scorer.update_thermal(effects::BgDrainCtx{}, p,
        topology::ThermalSample{.temperature_millicelsius = 40'000, .sequence = 3}));
    assert(scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(0, 0, 4)));
    assert(scorer.update_drops(effects::BgDrainCtx{}, p, drops(100'000, 0, 5)));

    auto snapshot = scorer.compute(p, 7'000, 6);
    assert(snapshot.peek().phi.raw() >= test_policy().suspect_phi.raw());
    assert(snapshot.peek().state == topology::HealthState::Suspect
        || snapshot.peek().state == topology::HealthState::Quarantined);
    assert(snapshot.peek().issues.test(topology::HealthIssue::PhiSuspect)
        || snapshot.peek().issues.test(topology::HealthIssue::PhiQuarantine));
    assert(scorer.transition_event_count() == 1);
    assert(scorer.transition_events()[0].from == topology::HealthState::Healthy);
    std::printf("  test_phi_delay_drives_suspect_state: PASSED\n");
}

static void test_thermal_ecc_and_drops_degrade_score() {
    auto scorer = topology::mint_topology_health<effects::ColdInitCtx, 2, 4>(
        effects::ColdInitCtx{}, test_policy());
    auto const p = peer(3);
    assert(scorer.record_heartbeat(effects::BgDrainCtx{}, p, 1'000, 1));
    assert(scorer.record_heartbeat(effects::BgDrainCtx{}, p, 2'000, 2));
    assert(scorer.update_thermal(effects::BgDrainCtx{}, p,
        topology::ThermalSample{
            .temperature_millicelsius = 82'000,
            .clock_degraded_pct = 12,
            .sequence = 3,
        }));
    assert(scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(0, 0, 4)));
    assert(scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(8, 0, 5)));
    assert(scorer.update_drops(effects::BgDrainCtx{}, p, drops(100'000, 0, 6)));
    assert(scorer.update_drops(effects::BgDrainCtx{}, p, drops(200'000, 1'200, 7)));

    auto snapshot = scorer.compute(p, 2'200, 8);
    assert(snapshot.peek().score.raw() < 900);
    assert(snapshot.peek().drop_rate_ppm >= test_policy().drop_critical_ppm);
    assert(snapshot.peek().issues.test(topology::HealthIssue::ThermalWarn));
    assert(snapshot.peek().issues.test(topology::HealthIssue::ClockDegraded));
    assert(snapshot.peek().issues.test(topology::HealthIssue::CorrectedEccTrend));
    assert(snapshot.peek().issues.test(topology::HealthIssue::DropRateCritical));
    std::printf("  test_thermal_ecc_and_drops_degrade_score: PASSED\n");
}

static void test_counter_regression_is_rejected() {
    auto scorer = topology::mint_topology_health<effects::ColdInitCtx, 2, 4>(
        effects::ColdInitCtx{}, test_policy());
    auto const p = peer(33);

    assert(scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(10, 0, 1)));
    assert(!scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(9, 0, 2)));
    assert(scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(11, 0, 2)));
    assert(scorer.update_drops(effects::BgDrainCtx{}, p, drops(100'000, 10, 3)));
    assert(!scorer.update_drops(effects::BgDrainCtx{}, p, drops(90'000, 10, 4)));
    assert(scorer.update_drops(effects::BgDrainCtx{}, p, drops(110'000, 10, 4)));
    assert(!scorer.update_wear(effects::BgDrainCtx{}, p,
        topology::WearSample{.used_ppm = 1'000'001, .sequence = 5}));
    assert(scorer.update_wear(effects::BgDrainCtx{}, p,
        topology::WearSample{.used_ppm = 900'000, .sequence = 5}));
    std::printf("  test_counter_regression_is_rejected: PASSED\n");
}

static void test_permanent_fault_is_sticky() {
    auto scorer = topology::mint_topology_health<effects::ColdInitCtx, 2, 4>(
        effects::ColdInitCtx{}, test_policy());
    auto const p = peer(4);
    assert(scorer.record_heartbeat(effects::BgDrainCtx{}, p, 1'000, 1));
    assert(scorer.record_heartbeat(effects::BgDrainCtx{}, p, 2'000, 2));
    assert(scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(0, 0, 3)));
    assert(scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(0, 1, 4)));
    assert(scorer.update_thermal(effects::BgDrainCtx{}, p,
        topology::ThermalSample{.temperature_millicelsius = 40'000, .sequence = 5}));
    assert(scorer.update_drops(effects::BgDrainCtx{}, p, drops(100'000, 0, 6)));

    auto failed = scorer.compute(p, 2'500, 7);
    assert(failed.peek().state == topology::HealthState::Permanent);
    assert(failed.peek().issues.test(topology::HealthIssue::UncorrectedEcc));

    assert(scorer.update_ecc(effects::BgDrainCtx{}, p, ecc(0, 1, 8)));
    auto still_failed = scorer.compute(p, 2'600, 9);
    assert(still_failed.peek().state == topology::HealthState::Permanent);
    assert(scorer.transition_event_count() == 1);
    std::printf("  test_permanent_fault_is_sticky:    PASSED\n");
}

int main() {
    static_assert(topology::CtxFitsHealthMint<effects::ColdInitCtx>);
    static_assert(!topology::CtxFitsHealthMint<effects::BgDrainCtx>);
    static_assert(topology::CtxFitsHealthUpdate<effects::BgDrainCtx>);
    static_assert(!topology::CtxFitsHealthUpdate<effects::HotFgCtx>);
    static_assert(safety::diag::is_diagnostic_class_v<topology::Health_Degraded>);

    std::printf("test_topology_health: 6 groups\n");
    test_name_accessors();
    test_healthy_snapshot_is_stale_wrapped();
    test_phi_delay_drives_suspect_state();
    test_thermal_ecc_and_drops_degrade_score();
    test_counter_regression_is_rejected();
    test_permanent_fault_is_sticky();
    std::printf("test_topology_health: all passed\n");
    return 0;
}
