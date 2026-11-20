#include <crucible/topology/Health.h>
#include <foundation/reflect/EnumName.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdio>
#include <string_view>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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

// The source of the positions of the samples of one test.
static ::fixy::SequenceSource& advance_to(::fixy::SequenceSource& sequences, std::uint64_t target) {
    while (sequences.stamp().position() < target)
        static_cast<void>(sequences.advance());
    return sequences;
}

// The log spelling of both enums comes from reflection.
static void test_name_accessors() {
    static_assert(::fixy::enum_name_text(topology::HealthState::Quarantined) == "Quarantined");
    static_assert(::fixy::enum_name_text(topology::HealthIssue::DropRateCritical) == "DropRateCritical");
    ::fixy::check::pass(kTest, "  test_name_accessors:              PASSED\n");
}

static void test_healthy_snapshot_is_stale_wrapped() {
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 4, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(1);
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 1000, 1));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 2000, 2));
    CRUCIBLE_TEST_ASSERT(kTest, scorer.update_thermal(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                                      topology::ThermalSample{
                                                          .temperature_millicelsius = 45000,
                                                          .clock_degraded_pct = 0,
                                                          .sequence = 3,
                                                      }));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 4)));
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 0, 5)));
    CRUCIBLE_TEST_ASSERT(kTest, scorer.update_wear(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                                   topology::WearSample{.used_ppm = 100000, .sequence = 6}));

    ::fixy::SequenceSource sequences =
        ::fixy::mint_sequence_source(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto snapshot = scorer.compute(p, 2500, advance_to(sequences, 7));
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().state == topology::HealthState::Healthy);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().score.raw() > 900);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().phi.raw() < test_policy().suspect_phi.raw());
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().issues.none());
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.is_fresh());
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().sequence == 7);
    auto current = scorer.current(p, advance_to(sequences, 9));
    CRUCIBLE_TEST_ASSERT(kTest, current.peek().state == topology::HealthState::Healthy);
    CRUCIBLE_TEST_ASSERT(kTest, current.staleness().value == 2);
    ::fixy::check::pass(kTest, "  test_healthy_snapshot_is_stale_wrapped: PASSED\n");
}

static void test_phi_delay_drives_suspect_state() {
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(2);
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 1000, 1));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 2000, 2));
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_thermal(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                     topology::ThermalSample{.temperature_millicelsius = 40000, .sequence = 3}));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 4)));
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 0, 5)));

    ::fixy::SequenceSource sequences =
        ::fixy::mint_sequence_source(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto snapshot = scorer.compute(p, 7000, advance_to(sequences, 6));
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().phi.raw() >= test_policy().suspect_phi.raw());
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().state == topology::HealthState::Suspect
                                    || snapshot.peek().state == topology::HealthState::Quarantined);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().issues.test(topology::HealthIssue::PhiSuspect)
                                    || snapshot.peek().issues.test(topology::HealthIssue::PhiQuarantine));
    CRUCIBLE_TEST_ASSERT(kTest, scorer.transition_event_count() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, scorer.transition_events().get(0).expect("the scorer recorded one transition")->from
                                    == topology::HealthState::Healthy);
    ::fixy::check::pass(kTest, "  test_phi_delay_drives_suspect_state: PASSED\n");
}

static void test_thermal_ecc_and_drops_degrade_score() {
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(3);
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 1000, 1));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 2000, 2));
    CRUCIBLE_TEST_ASSERT(kTest, scorer.update_thermal(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                                      topology::ThermalSample{
                                                          .temperature_millicelsius = 82000,
                                                          .clock_degraded_pct = 12,
                                                          .sequence = 3,
                                                      }));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 4)));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(8, 0, 5)));
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 0, 6)));
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(200000, 1200, 7)));

    ::fixy::SequenceSource sequences =
        ::fixy::mint_sequence_source(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto snapshot = scorer.compute(p, 2200, advance_to(sequences, 8));
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().score.raw() < 900);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().drop_rate_ppm >= test_policy().drop_critical_ppm);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().issues.test(topology::HealthIssue::ThermalWarn));
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().issues.test(topology::HealthIssue::ClockDegraded));
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().issues.test(topology::HealthIssue::CorrectedEccTrend));
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().issues.test(topology::HealthIssue::DropRateCritical));
    ::fixy::check::pass(kTest, "  test_thermal_ecc_and_drops_degrade_score: PASSED\n");
}

static void test_counter_regression_is_rejected() {
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(33);

    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(10, 0, 1)));
    CRUCIBLE_TEST_ASSERT(kTest,
                         !scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(9, 0, 2)));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(11, 0, 2)));
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 10, 3)));
    CRUCIBLE_TEST_ASSERT(
        kTest, !scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(90000, 10, 4)));
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(110000, 10, 4)));
    CRUCIBLE_TEST_ASSERT(kTest, !scorer.update_wear(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                                    topology::WearSample{.used_ppm = 1000001, .sequence = 5}));
    CRUCIBLE_TEST_ASSERT(kTest, scorer.update_wear(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                                   topology::WearSample{.used_ppm = 900000, .sequence = 5}));
    ::fixy::check::pass(kTest, "  test_counter_regression_is_rejected: PASSED\n");
}

static void test_permanent_fault_is_sticky() {
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto const p = peer(4);
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 1000, 1));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.record_heartbeat(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, 2000, 2));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 3)));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 1, 4)));
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_thermal(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                     topology::ThermalSample{.temperature_millicelsius = 40000, .sequence = 5}));
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_drops(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, drops(100000, 0, 6)));

    ::fixy::SequenceSource sequences =
        ::fixy::mint_sequence_source(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto failed = scorer.compute(p, 2500, advance_to(sequences, 7));
    CRUCIBLE_TEST_ASSERT(kTest, failed.peek().state == topology::HealthState::Permanent);
    CRUCIBLE_TEST_ASSERT(kTest, failed.peek().issues.test(topology::HealthIssue::UncorrectedEcc));

    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 1, 8)));
    auto still_failed = scorer.compute(p, 2600, advance_to(sequences, 9));
    CRUCIBLE_TEST_ASSERT(kTest, still_failed.peek().state == topology::HealthState::Permanent);
    CRUCIBLE_TEST_ASSERT(kTest, scorer.transition_event_count() == 1);
    ::fixy::check::pass(kTest, "  test_permanent_fault_is_sticky:    PASSED\n");
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
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(0, 0, 1)));
    CRUCIBLE_TEST_ASSERT(kTest,
                         scorer.update_ecc(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, ecc(delta, 0, 2)));
    ::fixy::SequenceSource sequences =
        ::fixy::mint_sequence_source(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto snapshot = scorer.compute(p, 2000, advance_to(sequences, 3));
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.peek().issues.test(topology::HealthIssue::CorrectedEccTrend));
    return snapshot.peek().score.raw();
}

// The risk of a corrected ECC trend is 400 plus ten for each corrected error,
// clamped at 1000.  Each delta below gives the full risk.  A sum that wraps
// in 32 bits or a product that wraps in 64 bits would give a small risk and a
// high score.
static void test_large_corrected_delta_gives_full_risk() {
    // 400 + 10 * d passes 2^32 at d = 429,496,690, and its low 32 bits are 4.
    CRUCIBLE_TEST_ASSERT(kTest, score_after_corrected_delta(429496689) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, score_after_corrected_delta(429496690) == 0);
    // 10 * 2^63 wraps to 0 in 64 bits, so the wrapped risk is 400.
    CRUCIBLE_TEST_ASSERT(kTest, score_after_corrected_delta(std::uint64_t{1} << 63) == 0);
    // 10 * (2^64 - 1) wraps to 2^64 - 10, and 400 more wraps to 390.
    CRUCIBLE_TEST_ASSERT(kTest, score_after_corrected_delta(~std::uint64_t{0}) == 0);
    // A delta at the threshold of the policy gives 400 + 40.
    CRUCIBLE_TEST_ASSERT(kTest, score_after_corrected_delta(4) == 1000 - 440);
    ::fixy::check::pass(kTest, "  test_large_corrected_delta_gives_full_risk: PASSED\n");
}

// Only the source grades a snapshot.  A peer with no sample, and a sample of a
// position that the source has not reached, give the infinite grade, which
// claims nothing about the age of the snapshot.
static void test_grade_comes_from_the_source() {
    auto scorer = topology::mint_topology_health<eff::ColdInitCtx, 2, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    ::fixy::SequenceSource sequences =
        ::fixy::mint_sequence_source(eff::ColdInitCtx{::foundation::effects::testing::init()});
    advance_to(sequences, 3);

    auto const unknown = scorer.current(peer(40), sequences);
    CRUCIBLE_TEST_ASSERT(kTest, unknown.peek().issues.test(topology::HealthIssue::MissingSample));
    CRUCIBLE_TEST_ASSERT(kTest, unknown.is_infinite());
    CRUCIBLE_TEST_ASSERT(kTest, unknown.peek().sequence == 3);

    auto const p = peer(41);
    CRUCIBLE_TEST_ASSERT(
        kTest, scorer.update_thermal(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                     topology::ThermalSample{.temperature_millicelsius = 40000, .sequence = 5}));
    CRUCIBLE_TEST_ASSERT(kTest, scorer.current(p, sequences).is_infinite());
    CRUCIBLE_TEST_ASSERT(kTest, scorer.current(p, advance_to(sequences, 6)).staleness().value == 1);
    ::fixy::check::pass(kTest, "  test_grade_comes_from_the_source:  PASSED\n");
}

int main() {
    static_assert(topology::CtxFitsHealthMint<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsHealthMint<eff::BgDrainCtx>);
    static_assert(topology::CtxFitsHealthUpdate<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsHealthUpdate<eff::HotFgCtx>);
    static_assert(::foundation::diag::is_diagnostic_class_v<topology::Health_Degraded>);

    ::fixy::report(::fixy::Sink::Out, "test_topology_health: 8 groups\n");
    test_name_accessors();
    test_healthy_snapshot_is_stale_wrapped();
    test_phi_delay_drives_suspect_state();
    test_thermal_ecc_and_drops_degrade_score();
    test_counter_regression_is_rejected();
    test_permanent_fault_is_sticky();
    test_large_corrected_delta_gives_full_risk();
    test_grade_comes_from_the_source();
    ::fixy::check::pass(kTest, "test_topology_health: all passed\n");
    return 0;
}
