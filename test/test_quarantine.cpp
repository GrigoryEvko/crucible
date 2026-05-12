#include <crucible/rt/Quarantine.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace observe = crucible::observe;
namespace rt = crucible::rt;
namespace saf = crucible::safety;
namespace topology = crucible::topology;

static cog::CogIdentity peer(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x118, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

static rt::QuarantineConfig test_config() {
    rt::QuarantineConfig config{};
    config.suspect_at_or_below = topology::HealthScore{900};
    config.quarantine_at_or_below = topology::HealthScore{500};
    config.recovery_probe_count = rt::PositiveRecoveryProbeCount{std::uint16_t{3}};
    config.permanent_after_ns = rt::PositiveNanoseconds{std::uint64_t{1'000}};
    config.canary_load_ppm = 20'000;
    return config;
}

static topology::HealthSnapshot health(std::uint16_t score,
                                       topology::HealthState state,
                                       std::uint64_t sequence) {
    topology::HealthSnapshot out{};
    out.score = topology::HealthScore{score};
    out.state = state;
    out.sequence = sequence;
    return out;
}

static observe::ProbeOutcome probe(bool ok, std::uint64_t sequence) {
    observe::ProbeOutcome out{};
    out.failure = ok
        ? observe::SyntheticProbeFailureClass::None
        : observe::SyntheticProbeFailureClass::Timeout;
    out.sequence = sequence;
    return out;
}

static void test_name_accessors() {
    assert(rt::quarantine_state_name(rt::QuarantineState::Quarantined)
           == std::string_view{"Quarantined"});
    assert(rt::quarantine_signal_name(rt::QuarantineSignal::OperatorOverride)
           == std::string_view{"OperatorOverride"});
    std::printf("  test_name_accessors:                     PASSED\n");
}

static void test_health_hysteresis() {
    auto policy = rt::mint_quarantine_policy<effects::ColdInitCtx, 4>(
        effects::ColdInitCtx{}, test_config());
    auto target = peer(1);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(850, topology::HealthState::Suspect, 1), 100));
    assert(policy.state(target) == rt::QuarantineState::Suspect);
    assert(policy.current(target).admitted_load_ppm == 300'000);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(400, topology::HealthState::Quarantined, 2), 200));
    auto snapshot = policy.current(target);
    assert(snapshot.state == rt::QuarantineState::Quarantined);
    assert(snapshot.admitted_load_ppm == 0);
    assert(snapshot.quarantine_since_ns == 200);
    assert(policy.transition_event_count() == 2);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(1000, topology::HealthState::Healthy, 3), 300));
    assert(policy.state(target) == rt::QuarantineState::Quarantined);

    std::printf("  test_health_hysteresis:                  PASSED\n");
}

static void test_asymmetric_failure_policy() {
    auto policy = rt::mint_quarantine_policy<effects::ColdInitCtx, 4>(
        effects::ColdInitCtx{}, test_config());

    auto tx = peer(2);
    assert(policy.on_asymmetric_failure(
        effects::BgDrainCtx{}, tx, topology::FailureClass::TxBroken, 100, 1));
    assert(policy.state(tx) == rt::QuarantineState::Suspect);
    assert(policy.current(tx).signals.test(rt::QuarantineSignal::AsymmetricSuspect));

    auto dead = peer(3);
    assert(policy.on_asymmetric_failure(
        effects::BgDrainCtx{}, dead, topology::FailureClass::BidiFailed, 100, 2));
    assert(policy.state(dead) == rt::QuarantineState::Quarantined);
    assert(policy.current(dead).signals.test(rt::QuarantineSignal::AsymmetricDead));

    std::printf("  test_asymmetric_failure_policy:          PASSED\n");
}

static void test_recovery_canary() {
    auto policy = rt::mint_quarantine_policy<effects::ColdInitCtx, 4>(
        effects::ColdInitCtx{}, test_config());
    auto target = peer(4);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(100, topology::HealthState::Quarantined, 1), 100));
    assert(policy.record_recovery_probe(effects::BgDrainCtx{}, target, probe(true, 2), 200));
    assert(policy.record_recovery_probe(effects::BgDrainCtx{}, target, probe(true, 3), 300));
    assert(policy.state(target) == rt::QuarantineState::Quarantined);
    assert(policy.record_recovery_probe(effects::BgDrainCtx{}, target, probe(true, 4), 400));

    auto snapshot = policy.current(target);
    assert(snapshot.state == rt::QuarantineState::Recovered);
    assert(snapshot.consecutive_recovery_probes == 3);
    assert(snapshot.admitted_load_ppm == 20'000);
    assert(snapshot.signals.test(rt::QuarantineSignal::RecoveryThresholdMet));

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(1000, topology::HealthState::Healthy, 5), 500));
    assert(policy.state(target) == rt::QuarantineState::Healthy);

    std::printf("  test_recovery_canary:                    PASSED\n");
}

static void test_permanent_requires_operator_permission() {
    auto policy = rt::mint_quarantine_policy<effects::ColdInitCtx, 4>(
        effects::ColdInitCtx{}, test_config());
    auto target = peer(5);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(100, topology::HealthState::Quarantined, 1), 100));
    assert(policy.check_permanent_deadline(
        effects::BgDrainCtx{}, target, 1'200, 2));
    auto blocked = policy.current(target);
    assert(blocked.state == rt::QuarantineState::Quarantined);
    assert(blocked.signals.test(rt::QuarantineSignal::PermanentRequiresOperator));

    auto authority = saf::mint_permission_root<rt::quarantine_tag::OperatorOverride>();
    authority = policy.operator_override(
        effects::ColdInitCtx{}, std::move(authority), target,
        rt::QuarantineState::Permanent, 1'300, 3);
    assert(policy.state(target) == rt::QuarantineState::Permanent);
    assert(policy.current(target).signals.test(rt::QuarantineSignal::OperatorOverride));
    saf::permission_drop(std::move(authority));

    std::printf("  test_permanent_requires_operator_permission: PASSED\n");
}

int main() {
    static_assert(rt::CtxFitsQuarantineMint<effects::ColdInitCtx>);
    static_assert(!rt::CtxFitsQuarantineMint<effects::BgDrainCtx>);
    static_assert(rt::CtxFitsQuarantineRecord<effects::BgDrainCtx>);
    static_assert(!rt::CtxFitsQuarantineRecord<effects::HotFgCtx>);
    static_assert(rt::CtxFitsQuarantineOverride<effects::ColdInitCtx>);
    static_assert(!rt::CtxFitsQuarantineOverride<effects::BgDrainCtx>);
    static_assert(std::is_trivially_copyable_v<rt::QuarantineSnapshot>);
    static_assert(sizeof(rt::QuarantineEvent) <= 64);

    std::printf("test_quarantine:\n");
    test_name_accessors();
    test_health_hysteresis();
    test_asymmetric_failure_policy();
    test_recovery_canary();
    test_permanent_requires_operator_permission();
    std::printf("test_quarantine: all PASSED\n");
    return 0;
}
