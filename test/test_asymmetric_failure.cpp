#include <crucible/topology/AsymmetricFailure.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace observe = crucible::observe;
namespace topology = crucible::topology;

static cog::CogIdentity peer(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x127, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

static topology::AsymmetricFailurePolicy test_policy() {
    topology::AsymmetricFailurePolicy policy{};
    policy.min_local_samples = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{2});
    policy.min_witnesses = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{2});
    return policy;
}

static observe::ProbeOutcome outcome(bool ok, std::uint64_t seq) {
    observe::ProbeOutcome out{};
    out.kind = observe::TransportProbeKind::TcpCubic;
    out.failure = ok ? observe::SyntheticProbeFailureClass::None : observe::SyntheticProbeFailureClass::Timeout;
    out.sequence = seq;
    return out;
}

template <class Detector>
static void record_pair(Detector& detector, cog::CogIdentity const& p, bool outbound, bool inbound, std::uint64_t seq) {
    assert(detector.record_outbound(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, outbound, seq));
    assert(detector.record_inbound(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p, inbound, seq + 1));
}

// The log spelling of both enums comes from reflection.
static void test_name_accessors() {
    static_assert(::foundation::reflect::enum_name(topology::FailureClass::TxBroken) == "TxBroken");
    static_assert(::foundation::reflect::enum_name(topology::FailureSignal::WitnessReachable) == "WitnessReachable");
    std::printf("  test_name_accessors:                     PASSED\n");
}

static void test_local_bidirectional_classification() {
    auto detector = topology::mint_asymmetric_failure_detector<eff::ColdInitCtx, 8, 4, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());

    auto ok = peer(1);
    record_pair(detector, ok, true, true, 1);
    record_pair(detector, ok, true, true, 3);
    assert(detector.classify(ok) == topology::FailureClass::BidiOk);

    auto tx = peer(2);
    record_pair(detector, tx, false, true, 10);
    record_pair(detector, tx, false, true, 12);
    assert(detector.classify(tx) == topology::FailureClass::TxBroken);

    auto rx = peer(3);
    record_pair(detector, rx, true, false, 20);
    record_pair(detector, rx, true, false, 22);
    assert(detector.classify(rx) == topology::FailureClass::RxBroken);

    auto dead = peer(4);
    record_pair(detector, dead, false, false, 30);
    record_pair(detector, dead, false, false, 32);
    assert(detector.classify(dead) == topology::FailureClass::BidiFailed);

    std::printf("  test_local_bidirectional_classification: PASSED\n");
}

static void test_multi_vantage_overrides_naive_dead_peer() {
    auto detector = topology::mint_asymmetric_failure_detector<eff::ColdInitCtx, 4, 4, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());

    auto target = peer(10);
    record_pair(detector, target, false, false, 1);
    record_pair(detector, target, false, false, 3);
    assert(detector.classify(target) == topology::FailureClass::BidiFailed);

    assert(detector.record_witness(eff::BgDrainCtx{::foundation::effects::testing::bg()}, target, peer(100), true, 5));
    assert(detector.record_witness(eff::BgDrainCtx{::foundation::effects::testing::bg()}, target, peer(101), true, 6));
    auto summary = detector.summary(target);
    assert(summary.witnesses == 2);
    assert(summary.witness_reachable == 2);
    assert(summary.signals.test(topology::FailureSignal::WitnessMajorityReachable));
    assert(summary.with_witnesses == topology::FailureClass::TxBroken);
    assert(detector.classify_with_witnesses(target) == topology::FailureClass::TxBroken);

    std::printf("  test_multi_vantage_overrides_naive_dead_peer: PASSED\n");
}

static void test_witness_majority_unreachable_keeps_dead_class() {
    auto detector = topology::mint_asymmetric_failure_detector<eff::ColdInitCtx, 4, 4, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());

    auto target = peer(11);
    record_pair(detector, target, false, false, 1);
    record_pair(detector, target, false, false, 3);
    assert(detector.record_witness(eff::BgDrainCtx{::foundation::effects::testing::bg()}, target, peer(100), false, 5));
    assert(detector.record_witness(eff::BgDrainCtx{::foundation::effects::testing::bg()}, target, peer(101), false, 6));

    auto summary = detector.summary(target);
    assert(summary.with_witnesses == topology::FailureClass::BidiFailed);
    assert(summary.signals.test(topology::FailureSignal::WitnessMajorityUnreachable));
    assert(topology::health_state_for_failure(summary.with_witnesses, topology::HealthState::Healthy)
           == topology::HealthState::Quarantined);

    std::printf("  test_witness_majority_unreachable_keeps_dead_class: PASSED\n");
}

static void test_synthetic_round_and_transition_events() {
    auto detector = topology::mint_asymmetric_failure_detector<eff::ColdInitCtx, 2, 4, 2>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, test_policy());
    auto target = peer(12);

    assert(detector.record_synthetic_round(eff::BgDrainCtx{::foundation::effects::testing::bg()}, target,
                                           outcome(false, 1), outcome(true, 2)));
    assert(detector.record_synthetic_round(eff::BgDrainCtx{::foundation::effects::testing::bg()}, target,
                                           outcome(false, 3), outcome(true, 4)));

    assert(detector.classify(target) == topology::FailureClass::TxBroken);
    assert(detector.event_count() >= 1);
    auto const events = detector.events();
    std::size_t const last_event = detector.event_count() - 1u;
    assert(events[last_event].to == topology::FailureClass::TxBroken);
    assert(topology::health_state_for_failure(topology::FailureClass::TxBroken, topology::HealthState::Healthy)
           == topology::HealthState::Suspect);

    std::printf("  test_synthetic_round_and_transition_events: PASSED\n");
}

int main() {
    static_assert(topology::CtxFitsAsymmetricFailureMint<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsAsymmetricFailureMint<eff::BgDrainCtx>);
    static_assert(topology::CtxFitsAsymmetricFailureRecord<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsAsymmetricFailureRecord<eff::HotFgCtx>);
    static_assert(std::is_trivially_copyable_v<topology::FailureSummary>);
    static_assert(sizeof(topology::AsymmetricFailureEvent) <= 64);

    std::printf("test_asymmetric_failure:\n");
    test_name_accessors();
    test_local_bidirectional_classification();
    test_multi_vantage_overrides_naive_dead_peer();
    test_witness_majority_unreachable_keeps_dead_class();
    test_synthetic_round_and_transition_events();
    std::printf("test_asymmetric_failure: all PASSED\n");
    return 0;
}
