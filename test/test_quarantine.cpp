#include <crucible/warden/Quarantine.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace observe = crucible::observe;
namespace warden = crucible::warden;
namespace saf = crucible::safety;
namespace topology = crucible::topology;

static cog::CogIdentity peer(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x118, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

static warden::QuarantineConfig test_config() {
    warden::QuarantineConfig config{};
    config.suspect_at_or_below = topology::HealthScore{900};
    config.quarantine_at_or_below = topology::HealthScore{500};
    config.recovery_probe_count = warden::PositiveRecoveryProbeCount{std::uint16_t{3}};
    config.permanent_after_ns = warden::PositiveNanoseconds{std::uint64_t{1'000}};
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
    assert(warden::quarantine_state_name(warden::QuarantineState::Quarantined)
           == std::string_view{"Quarantined"});
    assert(warden::quarantine_signal_name(warden::QuarantineSignal::OperatorOverride)
           == std::string_view{"OperatorOverride"});
    std::printf("  test_name_accessors:                     PASSED\n");
}

static void test_health_hysteresis() {
    auto policy = warden::mint_quarantine_policy<effects::ColdInitCtx, 4>(
        effects::ColdInitCtx{}, test_config());
    auto target = peer(1);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(850, topology::HealthState::Suspect, 1), 100));
    assert(policy.state(target) == warden::QuarantineState::Suspect);
    assert(policy.current(target).admitted_load_ppm == 300'000);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(400, topology::HealthState::Quarantined, 2), 200));
    auto snapshot = policy.current(target);
    assert(snapshot.state == warden::QuarantineState::Quarantined);
    assert(snapshot.admitted_load_ppm == 0);
    assert(snapshot.quarantine_since_ns == 200);
    assert(policy.transition_event_count() == 2);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(1000, topology::HealthState::Healthy, 3), 300));
    assert(policy.state(target) == warden::QuarantineState::Quarantined);

    std::printf("  test_health_hysteresis:                  PASSED\n");
}

static void test_asymmetric_failure_policy() {
    auto policy = warden::mint_quarantine_policy<effects::ColdInitCtx, 4>(
        effects::ColdInitCtx{}, test_config());

    auto tx = peer(2);
    assert(policy.on_asymmetric_failure(
        effects::BgDrainCtx{}, tx, topology::FailureClass::TxBroken, 100, 1));
    assert(policy.state(tx) == warden::QuarantineState::Suspect);
    assert(policy.current(tx).signals.test(warden::QuarantineSignal::AsymmetricSuspect));

    auto dead = peer(3);
    assert(policy.on_asymmetric_failure(
        effects::BgDrainCtx{}, dead, topology::FailureClass::BidiFailed, 100, 2));
    assert(policy.state(dead) == warden::QuarantineState::Quarantined);
    assert(policy.current(dead).signals.test(warden::QuarantineSignal::AsymmetricDead));

    std::printf("  test_asymmetric_failure_policy:          PASSED\n");
}

static void test_recovery_canary() {
    auto policy = warden::mint_quarantine_policy<effects::ColdInitCtx, 4>(
        effects::ColdInitCtx{}, test_config());
    auto target = peer(4);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(100, topology::HealthState::Quarantined, 1), 100));
    assert(policy.record_recovery_probe(effects::BgDrainCtx{}, target, probe(true, 2), 200));
    assert(policy.record_recovery_probe(effects::BgDrainCtx{}, target, probe(true, 3), 300));
    assert(policy.state(target) == warden::QuarantineState::Quarantined);
    assert(policy.record_recovery_probe(effects::BgDrainCtx{}, target, probe(true, 4), 400));

    auto snapshot = policy.current(target);
    assert(snapshot.state == warden::QuarantineState::Recovered);
    assert(snapshot.consecutive_recovery_probes == 3);
    assert(snapshot.admitted_load_ppm == 20'000);
    assert(snapshot.signals.test(warden::QuarantineSignal::RecoveryThresholdMet));

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(1000, topology::HealthState::Healthy, 5), 500));
    assert(policy.state(target) == warden::QuarantineState::Healthy);

    std::printf("  test_recovery_canary:                    PASSED\n");
}

static void test_permanent_requires_operator_permission() {
    auto policy = warden::mint_quarantine_policy<effects::ColdInitCtx, 4>(
        effects::ColdInitCtx{}, test_config());
    auto target = peer(5);

    assert(policy.on_health_event(
        effects::BgDrainCtx{}, target,
        health(100, topology::HealthState::Quarantined, 1), 100));
    assert(policy.check_permanent_deadline(
        effects::BgDrainCtx{}, target, 1'200, 2));
    auto blocked = policy.current(target);
    assert(blocked.state == warden::QuarantineState::Quarantined);
    assert(blocked.signals.test(warden::QuarantineSignal::PermanentRequiresOperator));

    auto authority = saf::mint_permission_root<warden::quarantine_tag::OperatorOverride>();
    authority = policy.operator_override(
        effects::ColdInitCtx{}, std::move(authority), target,
        warden::QuarantineState::Permanent, 1'300, 3);
    assert(policy.state(target) == warden::QuarantineState::Permanent);
    assert(policy.current(target).signals.test(warden::QuarantineSignal::OperatorOverride));
    saf::permission_drop(std::move(authority));

    std::printf("  test_permanent_requires_operator_permission: PASSED\n");
}

static void test_event_ring_wrap_chronological_order() {
    // fixy-A5-040 regression: transition_event_at(index) must return
    // chronologically-indexed events.  After wrap, events_[0] holds a
    // post-wrap event and the oldest surviving event lives at slot
    // next_event_.  Same shape as fixy-A5-010 in ConnectionPool.
    //
    // MaxCogs=2, MaxEvents=4 (override default MaxCogs*4=8 for a tight
    // wrap).  Drive 6 alternating Q↔H operator_override transitions
    // (sequences 1..6).  Surviving ring: events #3, #4, #5, #6.
    auto policy = warden::mint_quarantine_policy<effects::ColdInitCtx, 2, 4>(
        effects::ColdInitCtx{}, test_config());
    auto target = peer(99);
    auto authority =
        saf::mint_permission_root<warden::quarantine_tag::OperatorOverride>();

    for (std::uint64_t seq = 1; seq <= 6; ++seq) {
        auto next = (seq % 2 == 1)
            ? warden::QuarantineState::Quarantined
            : warden::QuarantineState::Healthy;
        authority = policy.operator_override(
            effects::ColdInitCtx{}, std::move(authority), target,
            next, 100u * seq, seq);
    }
    saf::permission_drop(std::move(authority));

    assert(policy.transition_event_count() == 4u);

    auto const* oldest = policy.transition_event_at(0);
    auto const* newest = policy.transition_event_at(3);
    assert(oldest != nullptr);
    assert(newest != nullptr);
    // Pre-fix transition_events()[0] returned the slot 0 contents,
    // overwritten by event #5 (odd seq → Quarantined).  The correct
    // chronological-first event is #3 with sequence 3.
    assert(oldest->sequence == 3u);
    assert(newest->sequence == 6u);
    // Sequence 3 was odd → Quarantined.  Sequence 6 was even → Healthy.
    assert(oldest->to == warden::QuarantineState::Quarantined);
    assert(newest->to == warden::QuarantineState::Healthy);

    std::uint64_t prev = 0;
    for (std::size_t k = 0; k < policy.transition_event_count(); ++k) {
        auto const* e = policy.transition_event_at(k);
        assert(e != nullptr);
        assert(e->sequence > prev);
        prev = e->sequence;
    }
    assert(prev == 6u);

    assert(policy.transition_event_at(4) == nullptr);

    std::printf("  test_event_ring_wrap_chronological_order: PASSED\n");
}

int main() {
    static_assert(warden::CtxFitsQuarantineMint<effects::ColdInitCtx>);
    static_assert(!warden::CtxFitsQuarantineMint<effects::BgDrainCtx>);
    static_assert(warden::CtxFitsQuarantineRecord<effects::BgDrainCtx>);
    static_assert(!warden::CtxFitsQuarantineRecord<effects::HotFgCtx>);
    static_assert(warden::CtxFitsQuarantineOverride<effects::ColdInitCtx>);
    static_assert(!warden::CtxFitsQuarantineOverride<effects::BgDrainCtx>);
    static_assert(std::is_trivially_copyable_v<warden::QuarantineSnapshot>);
    static_assert(sizeof(warden::QuarantineEvent) <= 64);

    std::printf("test_quarantine:\n");
    test_name_accessors();
    test_health_hysteresis();
    test_asymmetric_failure_policy();
    test_recovery_canary();
    test_permanent_requires_operator_permission();
    test_event_ring_wrap_chronological_order();
    std::printf("test_quarantine: all PASSED\n");
    return 0;
}
