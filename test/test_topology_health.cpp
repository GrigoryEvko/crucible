#include <crucible/topology/Health.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>

namespace cog = crucible::cog;
namespace eff = ::fixy;
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
    policy.expected_heartbeat_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1000});
    policy.suspect_phi = topology::PhiMilli{2000};
    policy.quarantine_phi = topology::PhiMilli{4000};
    policy.suspect_below = topology::HealthScore{700};
    policy.quarantine_below = topology::HealthScore{350};
    policy.recovered_at_or_above = topology::HealthScore{900};
    policy.corrected_ecc_warn_delta = 4;
    policy.drop_warn_ppm = 1000;
    policy.drop_critical_ppm = 10000;
    return policy;
}

static topology::EccCounters ecc(std::uint64_t corrected, std::uint64_t uncorrected, std::uint64_t sequence) {
    topology::EccCounters counters{};
    counters.corrected.advance(corrected);
    counters.uncorrected.advance(uncorrected);
    counters.sequence = sequence;
    return counters;
}

static topology::DropCounters drops(std::uint64_t packets, std::uint64_t dropped, std::uint64_t sequence) {
    topology::DropCounters counters{};
    counters.rx_packets.advance(packets);
    counters.rx_dropped.advance(dropped);
    counters.sequence = sequence;
    return counters;
}

// The log spelling of both enums comes from reflection.
static void test_name_accessors() {
    static_assert(::foundation::reflect::enum_name(topology::HealthState::Quarantined) == "Quarantined");
    static_assert(::foundation::reflect::enum_name(topology::HealthIssue::DropRateCritical) == "DropRateCritical");
    std::printf("  test_name_accessors:              PASSED\n");
}

static void test_healthy_snapshot_is_stale_wrapped() {
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 4, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(1);
    assert(scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 1000, 1));
    assert(scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 2000, 2));
    assert(scorer.update_thermal(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 topology::ThermalSample{
                                     .temperature_millicelsius = 45000,
                                     .clock_degraded_pct = 0,
                                     .sequence = 3,
                                 }));
    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 4)));
    assert(scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 0, 5)));
    assert(scorer.update_wear(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                              topology::WearSample{.used_ppm = 100000, .sequence = 6}));

    auto snapshot = scorer.compute(p, 2500, 7);
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
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(2);
    assert(scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 1000, 1));
    assert(scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 2000, 2));
    assert(scorer.update_thermal(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 topology::ThermalSample{.temperature_millicelsius = 40000, .sequence = 3}));
    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 4)));
    assert(scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 0, 5)));

    auto snapshot = scorer.compute(p, 7000, 6);
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
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(3);
    assert(scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 1000, 1));
    assert(scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 2000, 2));
    assert(scorer.update_thermal(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 topology::ThermalSample{
                                     .temperature_millicelsius = 82000,
                                     .clock_degraded_pct = 12,
                                     .sequence = 3,
                                 }));
    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 4)));
    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(8, 0, 5)));
    assert(scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 0, 6)));
    assert(scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(200000, 1200, 7)));

    auto snapshot = scorer.compute(p, 2200, 8);
    assert(snapshot.peek().score.raw() < 900);
    assert(snapshot.peek().drop_rate_ppm >= test_policy().drop_critical_ppm);
    assert(snapshot.peek().issues.test(topology::HealthIssue::ThermalWarn));
    assert(snapshot.peek().issues.test(topology::HealthIssue::ClockDegraded));
    assert(snapshot.peek().issues.test(topology::HealthIssue::CorrectedEccTrend));
    assert(snapshot.peek().issues.test(topology::HealthIssue::DropRateCritical));
    std::printf("  test_thermal_ecc_and_drops_degrade_score: PASSED\n");
}

static void test_counter_regression_is_rejected() {
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(33);

    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(10, 0, 1)));
    assert(!scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(9, 0, 2)));
    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(11, 0, 2)));
    assert(scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 10, 3)));
    assert(!scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(90000, 10, 4)));
    assert(scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(110000, 10, 4)));
    assert(!scorer.update_wear(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                               topology::WearSample{.used_ppm = 1000001, .sequence = 5}));
    assert(scorer.update_wear(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                              topology::WearSample{.used_ppm = 900000, .sequence = 5}));
    std::printf("  test_counter_regression_is_rejected: PASSED\n");
}

static void test_permanent_fault_is_sticky() {
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(4);
    assert(scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 1000, 1));
    assert(scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 2000, 2));
    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 3)));
    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 1, 4)));
    assert(scorer.update_thermal(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 topology::ThermalSample{.temperature_millicelsius = 40000, .sequence = 5}));
    assert(scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 0, 6)));

    auto failed = scorer.compute(p, 2500, 7);
    assert(failed.peek().state == topology::HealthState::Permanent);
    assert(failed.peek().issues.test(topology::HealthIssue::UncorrectedEcc));

    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 1, 8)));
    auto still_failed = scorer.compute(p, 2600, 9);
    assert(still_failed.peek().state == topology::HealthState::Permanent);
    assert(scorer.transition_event_count() == 1);
    std::printf("  test_permanent_fault_is_sticky:    PASSED\n");
}

// The score of a peer whose only weighted risk is the trend of its corrected
// ECC count, after a rise of `delta` in that count.  The score is 1000 minus
// the risk.
static std::uint16_t score_after_corrected_delta(std::uint64_t delta) {
    topology::HealthPolicy policy = test_policy();
    policy.weights = topology::HealthWeights{.phi = 0, .thermal = 0, .ecc = 1, .drop = 0, .wear = 0};
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, policy);
    auto const p = peer(5);
    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 1)));
    assert(scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(delta, 0, 2)));
    auto snapshot = scorer.compute(p, 2000, 3);
    assert(snapshot.peek().issues.test(topology::HealthIssue::CorrectedEccTrend));
    return snapshot.peek().score.raw();
}

// The risk of a corrected ECC trend is 400 plus ten for each corrected error,
// clamped at 1000.  Each delta below gives the full risk.  A sum that wraps
// in 32 bits or a product that wraps in 64 bits would give a small risk and a
// high score.
static void test_large_corrected_delta_gives_full_risk() {
    // 400 + 10 * d passes 2^32 at d = 429,496,690, and its low 32 bits are 4.
    assert(score_after_corrected_delta(429496689) == 0);
    assert(score_after_corrected_delta(429496690) == 0);
    // 10 * 2^63 wraps to 0 in 64 bits, so the wrapped risk is 400.
    assert(score_after_corrected_delta(std::uint64_t{1} << 63) == 0);
    // 10 * (2^64 - 1) wraps to 2^64 - 10, and 400 more wraps to 390.
    assert(score_after_corrected_delta(~std::uint64_t{0}) == 0);
    // A delta at the threshold of the policy gives 400 + 40.
    assert(score_after_corrected_delta(4) == 1000 - 440);
    std::printf("  test_large_corrected_delta_gives_full_risk: PASSED\n");
}

int main() {
    static_assert(topology::CtxFitsHealthMint<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsHealthMint<eff::BgDrainCtx>);
    static_assert(topology::CtxFitsHealthUpdate<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsHealthUpdate<eff::HotFgCtx>);
    static_assert(::foundation::diag::is_diagnostic_class_v<topology::Health_Degraded>);

    std::printf("test_topology_health: 7 groups\n");
    test_name_accessors();
    test_healthy_snapshot_is_stale_wrapped();
    test_phi_delay_drives_suspect_state();
    test_thermal_ecc_and_drops_degrade_score();
    test_counter_regression_is_rejected();
    test_permanent_fault_is_sticky();
    test_large_corrected_delta_gives_full_risk();
    std::printf("test_topology_health: all passed\n");
    return 0;
}
