#include <crucible/observe/SyntheticProbe.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace observe = crucible::observe;

static cog::CogIdentity peer(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x141, lo};
    id.level = cog::CogLevel::L3_Chassis;
    id.kind = cog::CogKind::Server;
    return id;
}

static void test_name_accessors() {
    assert(observe::transport_probe_kind_name(observe::TransportProbeKind::FederationMtls)
           == ::fixy::TextView{"FederationMtls"});
    assert(observe::synthetic_probe_failure_name(observe::SyntheticProbeFailureClass::Completion)
           == ::fixy::TextView{"Completion"});
    crucible::test::pass("  test_name_accessors:            PASSED\n");
}

static void test_register_and_record_success() {
    observe::ProbeConfig config{};
    config.metric_id_base = 700;
    auto runner = observe::mint_synthetic_probes<eff::ColdInitCtx, 4>(
        eff::ColdInitCtx{::foundation::effects::testing::init()}, config);

    ::fixy::Bits<observe::TransportProbeKind> kinds{
        observe::TransportProbeKind::FederationMtls,
        observe::TransportProbeKind::TcpBbr3,
    };
    auto const p = peer(1);
    assert(runner.register_peer(p, kinds));
    assert(!runner.register_peer(p, kinds));
    assert(runner.enabled(p, observe::TransportProbeKind::FederationMtls));

    // The dispatch counter has to be bumped before the outcome is reported.
    // Recording an outcome does not bump it, because doing so would make the
    // lost-probe signal identically zero.
    assert(runner.schedule_probe(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 observe::TransportProbeKind::FederationMtls));

    observe::ObservationSnapshot observations;
    assert(runner.record_outcome(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 observe::ProbeOutcome{
                                     .kind = observe::TransportProbeKind::FederationMtls,
                                     .failure = observe::SyntheticProbeFailureClass::None,
                                     .latency_ns = 11000,
                                     .bytes_transferred = 256,
                                     .sequence = 9,
                                 },
                                 observations));

    auto const stats = runner.stats(p, observe::TransportProbeKind::FederationMtls);
    assert(stats.scheduled == 1);
    assert(stats.succeeded == 1);
    assert(stats.failed == 0);
    assert(stats.bytes_transferred == 256);
    assert(stats.last_latency_ns == 11000);
    assert(stats.last_sequence == 9);
    assert(observe::latest_observation(observations).kind == observe::ObservationKind::BitsTransferred);
    crucible::test::pass("  test_register_and_record_success: PASSED\n");
}

static void test_failure_accounting() {
    auto runner =
        observe::mint_synthetic_probes<eff::ColdInitCtx, 2>(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto const p = peer(2);
    ::fixy::Bits<observe::TransportProbeKind> kinds{observe::TransportProbeKind::RdmaRead};
    assert(runner.register_peer(p, kinds));

    // Dispatched separately from the outcome, because a probe that times out
    // still counts as dispatched.
    assert(runner.schedule_probe(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 observe::TransportProbeKind::RdmaRead));

    assert(runner.record_outcome(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 observe::ProbeOutcome{
                                     .kind = observe::TransportProbeKind::RdmaRead,
                                     .failure = observe::SyntheticProbeFailureClass::Timeout,
                                     .latency_ns = 1000000,
                                     .bytes_transferred = 0,
                                     .sequence = 3,
                                 }));

    auto const stats = runner.stats(p, observe::TransportProbeKind::RdmaRead);
    assert(stats.scheduled == 1);
    assert(stats.succeeded == 0);
    assert(stats.failed == 1);
    assert(stats.timed_out == 1);
    assert(stats.last_failure == observe::SyntheticProbeFailureClass::Timeout);
    crucible::test::pass("  test_failure_accounting:        PASSED\n");
}

static void test_rejects_unregistered_or_disabled() {
    auto runner =
        observe::mint_synthetic_probes<eff::ColdInitCtx, 1>(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto const p = peer(3);
    auto const missing = peer(4);
    ::fixy::Bits<observe::TransportProbeKind> kinds{observe::TransportProbeKind::TcpCubic};
    assert(runner.register_peer(p, kinds));

    assert(!runner.record_outcome(eff::BgDrainCtx{::foundation::effects::testing::bg()}, missing,
                                  observe::ProbeOutcome{.kind = observe::TransportProbeKind::TcpCubic}));
    assert(!runner.record_outcome(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                  observe::ProbeOutcome{.kind = observe::TransportProbeKind::FederationMtls}));
    assert(runner.stats(missing, observe::TransportProbeKind::TcpCubic).scheduled == 0);
    crucible::test::pass("  test_rejects_unregistered_or_disabled: PASSED\n");
}

// The dispatch and outcome counters are independent, and the loss rate is
// their difference. Were a recorded outcome to bump the dispatch counter too,
// that difference would be identically zero and the metric would carry no
// information. Three things follow and are checked below: recording alone
// leaves the dispatch count untouched, dispatching alone opens a non-zero gap,
// and a dispatch paired with an outcome closes it again.
static void test_schedule_record_separation() {
    auto runner =
        observe::mint_synthetic_probes<eff::ColdInitCtx, 4>(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto const p = peer(5);
    ::fixy::Bits<observe::TransportProbeKind> kinds{observe::TransportProbeKind::FederationMtls};
    assert(runner.register_peer(p, kinds));

    // Recording alone.
    assert(runner.record_outcome(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 observe::ProbeOutcome{
                                     .kind = observe::TransportProbeKind::FederationMtls,
                                     .failure = observe::SyntheticProbeFailureClass::None,
                                     .latency_ns = 1000,
                                     .bytes_transferred = 64,
                                     .sequence = 1,
                                 }));
    auto stats_a = runner.stats(p, observe::TransportProbeKind::FederationMtls);
    assert(stats_a.scheduled == 0);
    assert(stats_a.succeeded == 1);

    // Dispatching alone, twice, against one recorded outcome.
    assert(runner.schedule_probe(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 observe::TransportProbeKind::FederationMtls));
    assert(runner.schedule_probe(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                 observe::TransportProbeKind::FederationMtls));
    auto stats_b = runner.stats(p, observe::TransportProbeKind::FederationMtls);
    assert(stats_b.scheduled == 2);
    assert(stats_b.succeeded == 1);
    assert(stats_b.failed == 0);
    auto const inflight_b = stats_b.scheduled - stats_b.succeeded - stats_b.failed;
    assert(inflight_b == 1);

    // Every dispatch resolved by exactly one outcome, so the gap closes.
    auto runner2 =
        observe::mint_synthetic_probes<eff::ColdInitCtx, 4>(eff::ColdInitCtx{::foundation::effects::testing::init()});
    auto const q = peer(6);
    assert(runner2.register_peer(q, kinds));
    for (std::uint64_t i = 0; i < 5; ++i) {
        assert(runner2.schedule_probe(eff::BgDrainCtx{::foundation::effects::testing::bg()}, q,
                                      observe::TransportProbeKind::FederationMtls));
        assert(runner2.record_outcome(eff::BgDrainCtx{::foundation::effects::testing::bg()}, q,
                                      observe::ProbeOutcome{
                                          .kind = observe::TransportProbeKind::FederationMtls,
                                          .failure = (i % 2) == 0 ? observe::SyntheticProbeFailureClass::None
                                                                  : observe::SyntheticProbeFailureClass::Timeout,
                                          .latency_ns = 500,
                                          .bytes_transferred = 32,
                                          .sequence = i,
                                      }));
    }
    auto const stats_c = runner2.stats(q, observe::TransportProbeKind::FederationMtls);
    assert(stats_c.scheduled == 5);
    assert(stats_c.succeeded == 3);
    assert(stats_c.failed == 2);
    assert(stats_c.scheduled == stats_c.succeeded + stats_c.failed);

    // Dispatch refuses an unregistered peer and a disabled kind on the same
    // terms recording does.
    auto const missing = peer(7);
    assert(!runner.schedule_probe(eff::BgDrainCtx{::foundation::effects::testing::bg()}, missing,
                                  observe::TransportProbeKind::FederationMtls));
    assert(!runner.schedule_probe(eff::BgDrainCtx{::foundation::effects::testing::bg()}, p,
                                  observe::TransportProbeKind::TcpBbr3));
    crucible::test::pass("  test_schedule_record_separation:  PASSED\n");
}

// The stats grid is indexed by peer and by transport kind, and both indices
// are written concurrently. The counters are narrower than a cache line on
// their own, so without the alignment two adjacent kinds share a line and
// contend on every increment. The loop below checks that every cell in the
// grid starts on its own line.
static void test_probe_stats_layout_invariants() {
    using Stats = observe::detail::AtomicProbeStats;
    static_assert(alignof(Stats) >= 64, "AtomicProbeStats must be cache-line-aligned");
    static_assert(sizeof(Stats) >= 64, "AtomicProbeStats occupies a full cache line");

    std::array<std::array<Stats, 4>, 4> grid{};
    constexpr std::uintptr_t LINE = 64;
    for (std::size_t p = 0; p < grid.size(); ++p) {
        for (std::size_t k = 0; k < grid[p].size(); ++k) {
            auto const addr = std::bit_cast<std::uintptr_t>(&grid[p][k]);
            assert((addr % LINE) == 0);
            if (p > 0 || k > 0) {
                std::size_t const prev_p = (k == 0) ? p - 1 : p;
                std::size_t const prev_k = (k == 0) ? grid[p].size() - 1 : k - 1;
                auto const prev_addr = std::bit_cast<std::uintptr_t>(&grid[prev_p][prev_k]);
                assert(addr - prev_addr >= LINE);
            }
        }
    }
    crucible::test::pass("  test_probe_stats_layout_invariants: PASSED\n");
}

// Two different triples of a peer, a kind and a lane never share an id of
// one base, also when the base makes the id wrap past 2^32.  The second peer
// of each sample is the first peer with one bit changed, or a second random
// peer below the bound.  The samples come from Philox with a fixed key, so
// each run reads the same samples.
static void test_metric_ids_are_injective() {
    constexpr std::uint64_t kSampleCount = std::uint64_t{1} << 16;
    constexpr std::size_t kKindCount = observe::all_transport_probe_kinds.size();
    constexpr std::uint32_t kPeerBits = 27;
    static_assert((std::size_t{1} << kPeerBits) <= observe::detail::kMaxProbeMetricPeers);
    for (std::uint64_t offset = 0; offset < kSampleCount; ++offset) {
        ::fixy::Philox4x32::Counter const block = ::fixy::Philox4x32::generate(offset, 0x50524F4245ull);
        std::uint32_t const word0 = *block.get(0).expect("a block holds four words");
        std::uint32_t const word1 = *block.get(1).expect("a block holds four words");
        std::uint32_t const word2 = *block.get(2).expect("a block holds four words");
        std::uint32_t const word3 = *block.get(3).expect("a block holds four words");
        std::size_t const peer_index = word0 >> (32u - kPeerBits);
        std::size_t const flipped = peer_index ^ (std::size_t{1} << (word1 % kPeerBits));
        std::size_t const other = word1 >> (32u - kPeerBits);
        auto const kind = *observe::all_transport_probe_kinds.get(word2 % kKindCount).expect("the kind is a position");
        auto const other_kind =
            *observe::all_transport_probe_kinds.get((word2 >> 8u) % kKindCount).expect("the kind is a position");
        auto const lane = static_cast<observe::detail::ProbeMetricLane>(word2 >> 31u);
        auto const other_lane = static_cast<observe::detail::ProbeMetricLane>((word2 >> 30u) & 1u);
        std::uint32_t const base = word3;
        std::uint32_t const id = observe::detail::metric_id_for(base, peer_index, kind, lane);
        assert(id != observe::detail::metric_id_for(base, flipped, kind, lane));
        bool const is_same_triple = peer_index == other && kind == other_kind && lane == other_lane;
        assert(is_same_triple == (id == observe::detail::metric_id_for(base, other, other_kind, other_lane)));
    }
    crucible::test::pass("  test_metric_ids_are_injective: PASSED\n");
}

int main() {
    static_assert(observe::CtxFitsSyntheticProbeMint<eff::ColdInitCtx>);
    static_assert(!observe::CtxFitsSyntheticProbeMint<eff::BgDrainCtx>);
    static_assert(observe::CtxFitsSyntheticProbeRecord<eff::BgDrainCtx>);
    static_assert(!observe::CtxFitsSyntheticProbeRecord<eff::HotFgCtx>);

    ::fixy::report(::fixy::Sink::Out, "test_synthetic_probe: 7 groups\n");
    test_name_accessors();
    test_register_and_record_success();
    test_failure_accounting();
    test_rejects_unregistered_or_disabled();
    test_schedule_record_separation();
    test_probe_stats_layout_invariants();
    test_metric_ids_are_injective();
    crucible::test::pass("test_synthetic_probe: all passed\n");
    return 0;
}
