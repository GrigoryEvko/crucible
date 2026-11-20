#include <crucible/warden/Quarantine.h>

#include <fixy/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/EnumName.h>

#include <fixy/Core.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdio>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace cog = crucible::cog;
namespace fe = ::foundation::effects;
namespace observe = crucible::observe;
namespace perm = ::foundation::permissions;
namespace warden = crucible::warden;
namespace topology = crucible::topology;

static ::fixy::ColdInitCtx init_ctx() { return ::fixy::ColdInitCtx{fe::testing::init()}; }

static ::fixy::BgDrainCtx bg_ctx() { return ::fixy::BgDrainCtx{fe::testing::bg()}; }

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
    config.recovery_probe_count = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{3});
    config.permanent_after_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1000});
    config.canary_load_ppm = 20000;
    return config;
}

static topology::HealthSnapshot health(std::uint16_t score, topology::HealthState state, std::uint64_t sequence) {
    topology::HealthSnapshot out{};
    out.score = topology::HealthScore{score};
    out.state = state;
    out.sequence = sequence;
    return out;
}

static observe::ProbeOutcome probe(bool ok, std::uint64_t sequence) {
    observe::ProbeOutcome out{};
    out.failure = ok ? observe::SyntheticProbeFailureClass::None : observe::SyntheticProbeFailureClass::Timeout;
    out.sequence = sequence;
    return out;
}

static void test_names() {
    static_assert(::fixy::enum_name_text(warden::QuarantineState::Quarantined) == "Quarantined");
    static_assert(::fixy::enum_name_text(warden::QuarantineSignal::OperatorOverride) == "OperatorOverride");
    static_assert(::fixy::enum_name_text(static_cast<warden::QuarantineState>(0xFF)) == "<unknown QuarantineState>");
    ::fixy::check::pass(kTest, "  test_names:                              PASSED\n");
}

static void test_health_hysteresis() {
    auto policy = warden::mint_quarantine_policy<4>(init_ctx(), test_config());
    auto target = peer(1);

    CRUCIBLE_TEST_ASSERT(kTest,
                         policy.on_health_event(bg_ctx(), target, health(850, topology::HealthState::Suspect, 1), 100));
    CRUCIBLE_TEST_ASSERT(kTest, policy.state(target) == warden::QuarantineState::Suspect);
    CRUCIBLE_TEST_ASSERT(kTest, policy.current(target).admitted_load_ppm == 300000);

    CRUCIBLE_TEST_ASSERT(
        kTest, policy.on_health_event(bg_ctx(), target, health(400, topology::HealthState::Quarantined, 2), 200));
    auto snapshot = policy.current(target);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.state == warden::QuarantineState::Quarantined);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.admitted_load_ppm == 0);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.quarantine_since_ns == 200);
    CRUCIBLE_TEST_ASSERT(kTest, policy.transition_event_count() == 2);

    CRUCIBLE_TEST_ASSERT(
        kTest, policy.on_health_event(bg_ctx(), target, health(1000, topology::HealthState::Healthy, 3), 300));
    CRUCIBLE_TEST_ASSERT(kTest, policy.state(target) == warden::QuarantineState::Quarantined);

    // A fact whose sequence goes backward is refused and changes nothing.
    CRUCIBLE_TEST_ASSERT(
        kTest, !policy.on_health_event(bg_ctx(), target, health(100, topology::HealthState::Quarantined, 1), 400));
    CRUCIBLE_TEST_ASSERT(kTest, policy.transition_event_count() == 2);

    ::fixy::check::pass(kTest, "  test_health_hysteresis:                  PASSED\n");
}

static void test_asymmetric_failure_policy() {
    auto policy = warden::mint_quarantine_policy<4>(init_ctx(), test_config());

    auto tx = peer(2);
    CRUCIBLE_TEST_ASSERT(kTest, policy.on_asymmetric_failure(bg_ctx(), tx, topology::FailureClass::TxBroken, 100, 1));
    CRUCIBLE_TEST_ASSERT(kTest, policy.state(tx) == warden::QuarantineState::Suspect);
    CRUCIBLE_TEST_ASSERT(kTest, policy.current(tx).signals.test(warden::QuarantineSignal::AsymmetricSuspect));

    auto dead = peer(3);
    CRUCIBLE_TEST_ASSERT(kTest,
                         policy.on_asymmetric_failure(bg_ctx(), dead, topology::FailureClass::BidiFailed, 100, 2));
    CRUCIBLE_TEST_ASSERT(kTest, policy.state(dead) == warden::QuarantineState::Quarantined);
    CRUCIBLE_TEST_ASSERT(kTest, policy.current(dead).signals.test(warden::QuarantineSignal::AsymmetricDead));

    ::fixy::check::pass(kTest, "  test_asymmetric_failure_policy:          PASSED\n");
}

static void test_recovery_canary() {
    auto policy = warden::mint_quarantine_policy<4>(init_ctx(), test_config());
    auto target = peer(4);

    CRUCIBLE_TEST_ASSERT(
        kTest, policy.on_health_event(bg_ctx(), target, health(100, topology::HealthState::Quarantined, 1), 100));
    CRUCIBLE_TEST_ASSERT(kTest, policy.record_recovery_probe(bg_ctx(), target, probe(true, 2), 200));
    CRUCIBLE_TEST_ASSERT(kTest, policy.record_recovery_probe(bg_ctx(), target, probe(true, 3), 300));
    CRUCIBLE_TEST_ASSERT(kTest, policy.state(target) == warden::QuarantineState::Quarantined);
    CRUCIBLE_TEST_ASSERT(kTest, policy.record_recovery_probe(bg_ctx(), target, probe(true, 4), 400));

    auto snapshot = policy.current(target);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.state == warden::QuarantineState::Recovered);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.consecutive_recovery_probes == 3);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.admitted_load_ppm == 20000);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.signals.test(warden::QuarantineSignal::RecoveryThresholdMet));

    CRUCIBLE_TEST_ASSERT(
        kTest, policy.on_health_event(bg_ctx(), target, health(1000, topology::HealthState::Healthy, 5), 500));
    CRUCIBLE_TEST_ASSERT(kTest, policy.state(target) == warden::QuarantineState::Healthy);

    ::fixy::check::pass(kTest, "  test_recovery_canary:                    PASSED\n");
}

static void test_permanent_requires_operator_permission() {
    auto policy = warden::mint_quarantine_policy<4>(init_ctx(), test_config());
    auto target = peer(5);

    CRUCIBLE_TEST_ASSERT(
        kTest, policy.on_health_event(bg_ctx(), target, health(100, topology::HealthState::Quarantined, 1), 100));
    CRUCIBLE_TEST_ASSERT(kTest, policy.check_permanent_deadline(bg_ctx(), target, 1200, 2));
    auto blocked = policy.current(target);
    CRUCIBLE_TEST_ASSERT(kTest, blocked.state == warden::QuarantineState::Quarantined);
    CRUCIBLE_TEST_ASSERT(kTest, blocked.signals.test(warden::QuarantineSignal::PermanentRequiresOperator));

    auto authority = perm::mint_permission_root<warden::quarantine_tag::OperatorOverride>(init_ctx());
    authority =
        policy.operator_override(init_ctx(), std::move(authority), target, warden::QuarantineState::Permanent, 1300, 3);
    CRUCIBLE_TEST_ASSERT(kTest, policy.state(target) == warden::QuarantineState::Permanent);
    CRUCIBLE_TEST_ASSERT(kTest, policy.current(target).signals.test(warden::QuarantineSignal::OperatorOverride));
    ::foundation::linear::drop(std::move(authority));

    ::fixy::check::pass(kTest, "  test_permanent_requires_operator_permission: PASSED\n");
}

static void test_event_ring_wrap_chronological_order() {
    // Indexing must be chronological, not positional.  A four-slot ring
    // is small enough that six transitions wrap it twice, leaving events
    // three through six alive with the oldest of them no longer in slot
    // zero.  The alternating states make each event distinguishable.
    auto policy = warden::mint_quarantine_policy<2, 4>(init_ctx(), test_config());
    auto target = peer(99);
    auto authority = perm::mint_permission_root<warden::quarantine_tag::OperatorOverride>(init_ctx());

    for (std::uint64_t seq = 1; seq <= 6; ++seq) {
        auto next = (seq % 2 == 1) ? warden::QuarantineState::Quarantined : warden::QuarantineState::Healthy;
        authority = policy.operator_override(init_ctx(), std::move(authority), target, next, 100u * seq, seq);
    }
    ::foundation::linear::drop(std::move(authority));

    CRUCIBLE_TEST_ASSERT(kTest, policy.transition_event_count() == 4u);

    auto const oldest = policy.transition_event_at(0).expect("the ring keeps four events");
    auto const newest = policy.transition_event_at(3).expect("the ring keeps four events");
    // Slot zero holds event five, which overwrote it, so reading the
    // storage positionally answers with the wrong event.
    CRUCIBLE_TEST_ASSERT(kTest, oldest.sequence == 3u);
    CRUCIBLE_TEST_ASSERT(kTest, newest.sequence == 6u);
    // Odd sequences quarantine and even ones return to healthy.
    CRUCIBLE_TEST_ASSERT(kTest, oldest.to == warden::QuarantineState::Quarantined);
    CRUCIBLE_TEST_ASSERT(kTest, newest.to == warden::QuarantineState::Healthy);

    std::uint64_t prev = 0;
    for (std::size_t k = 0; k < policy.transition_event_count(); ++k) {
        auto const event = policy.transition_event_at(k).expect("k is below the count of events");
        CRUCIBLE_TEST_ASSERT(kTest, event.sequence > prev);
        prev = event.sequence;
    }
    CRUCIBLE_TEST_ASSERT(kTest, prev == 6u);

    CRUCIBLE_TEST_ASSERT(kTest, policy.transition_event_at(4).is_none());

    ::fixy::check::pass(kTest, "  test_event_ring_wrap_chronological_order: PASSED\n");
}

int main() {
    static_assert(warden::CtxFitsQuarantineMint<::fixy::ColdInitCtx>);
    static_assert(!warden::CtxFitsQuarantineMint<::fixy::BgDrainCtx>);
    static_assert(warden::CtxFitsQuarantineRecord<::fixy::BgDrainCtx>);
    static_assert(!warden::CtxFitsQuarantineRecord<::fixy::HotFgCtx>);
    static_assert(warden::CtxFitsQuarantineOverride<::fixy::ColdInitCtx>);
    static_assert(!warden::CtxFitsQuarantineOverride<::fixy::BgDrainCtx>);
    static_assert(perm::PermissionRootArgs<warden::quarantine_tag::OperatorOverride, ::fixy::ColdInitCtx>);
    static_assert(!perm::PermissionRootArgs<warden::quarantine_tag::OperatorOverride, ::fixy::BgDrainCtx>);
    static_assert(!perm::PermissionRootArgs<warden::quarantine_tag::OperatorOverride, ::fixy::TestRunnerCtx>);
    static_assert(!std::is_default_constructible_v<warden::QuarantinePolicy<4>>);
    static_assert(std::is_trivially_copyable_v<warden::QuarantineSnapshot>);
    static_assert(sizeof(warden::QuarantineEvent) <= 64);

    ::fixy::report(::fixy::Sink::Out, "test_quarantine:\n");
    test_names();
    test_health_hysteresis();
    test_asymmetric_failure_policy();
    test_recovery_canary();
    test_permanent_requires_operator_permission();
    test_event_ring_wrap_chronological_order();
    ::fixy::check::pass(kTest, "test_quarantine: all PASSED\n");
    return 0;
}
