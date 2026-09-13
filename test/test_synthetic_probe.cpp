#include <crucible/observe/SyntheticProbe.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace cog = crucible::cog;
namespace eff = crucible::effects;
namespace observe = crucible::observe;
namespace observe = crucible::observe;
namespace safety = crucible::safety;

static cog::CogIdentity peer(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x141, lo};
    id.level = cog::CogLevel::L3_Chassis;
    id.kind = cog::CogKind::Server;
    return id;
}

static void test_name_accessors() {
    assert(observe::transport_probe_kind_name(observe::TransportProbeKind::Quic) == std::string_view{"Quic"});
    assert(observe::synthetic_probe_failure_name(observe::SyntheticProbeFailureClass::Completion)
           == std::string_view{"Completion"});
    std::printf("  test_name_accessors:            PASSED\n");
}

static void test_register_and_record_success() {
    observe::ProbeConfig config{};
    config.metric_id_base = 700;
    auto runner = observe::mint_synthetic_probes<eff::ColdInitCtx, 4>(eff::ColdInitCtx{}, config);

    safety::Bits<observe::TransportProbeKind> kinds{
        observe::TransportProbeKind::Quic,
        observe::TransportProbeKind::TcpBbr3,
    };
    auto const p = peer(1);
    assert(runner.register_peer(p, kinds));
    assert(!runner.register_peer(p, kinds));
    assert(runner.enabled(p, observe::TransportProbeKind::Quic));

    // The dispatch counter has to be bumped before the outcome is reported.
    // Recording an outcome does not bump it, because doing so would make the
    // lost-probe signal identically zero.
    assert(runner.schedule_probe(eff::BgDrainCtx{}, p, observe::TransportProbeKind::Quic));

    observe::ObservationSnapshot observations;
    assert(runner.record_outcome(eff::BgDrainCtx{}, p,
                                 observe::ProbeOutcome{
                                     .kind = observe::TransportProbeKind::Quic,
                                     .failure = observe::SyntheticProbeFailureClass::None,
                                     .latency_ns = 11'000,
                                     .bytes_transferred = 256,
                                     .sequence = 9,
                                 },
                                 &observations));

    auto const stats = runner.stats(p, observe::TransportProbeKind::Quic);
    assert(stats.scheduled == 1);
    assert(stats.succeeded == 1);
    assert(stats.failed == 0);
    assert(stats.bytes_transferred == 256);
    assert(stats.last_latency_ns == 11'000);
    assert(stats.last_sequence == 9);
    assert(observe::latest_observation(observations).kind == observe::ObservationKind::BitsTransferred);
    std::printf("  test_register_and_record_success: PASSED\n");
}

static void test_failure_accounting() {
    auto runner = observe::mint_synthetic_probes<eff::ColdInitCtx, 2>(eff::ColdInitCtx{});
    auto const p = peer(2);
    safety::Bits<observe::TransportProbeKind> kinds{observe::TransportProbeKind::RdmaRead};
    assert(runner.register_peer(p, kinds));

    // Dispatched separately from the outcome, because a probe that times out
    // still counts as dispatched.
    assert(runner.schedule_probe(eff::BgDrainCtx{}, p, observe::TransportProbeKind::RdmaRead));

    assert(runner.record_outcome(eff::BgDrainCtx{}, p,
                                 observe::ProbeOutcome{
                                     .kind = observe::TransportProbeKind::RdmaRead,
                                     .failure = observe::SyntheticProbeFailureClass::Timeout,
                                     .latency_ns = 1'000'000,
                                     .bytes_transferred = 0,
                                     .sequence = 3,
                                 }));

    auto const stats = runner.stats(p, observe::TransportProbeKind::RdmaRead);
    assert(stats.scheduled == 1);
    assert(stats.succeeded == 0);
    assert(stats.failed == 1);
    assert(stats.timed_out == 1);
    assert(stats.last_failure == observe::SyntheticProbeFailureClass::Timeout);
    std::printf("  test_failure_accounting:        PASSED\n");
}

static void test_rejects_unregistered_or_disabled() {
    auto runner = observe::mint_synthetic_probes<eff::ColdInitCtx, 1>(eff::ColdInitCtx{});
    auto const p = peer(3);
    auto const missing = peer(4);
    safety::Bits<observe::TransportProbeKind> kinds{observe::TransportProbeKind::TcpCubic};
    assert(runner.register_peer(p, kinds));

    assert(!runner.record_outcome(eff::BgDrainCtx{}, missing,
                                  observe::ProbeOutcome{.kind = observe::TransportProbeKind::TcpCubic}));
    assert(
        !runner.record_outcome(eff::BgDrainCtx{}, p, observe::ProbeOutcome{.kind = observe::TransportProbeKind::Quic}));
    assert(runner.stats(missing, observe::TransportProbeKind::TcpCubic).scheduled == 0);
    std::printf("  test_rejects_unregistered_or_disabled: PASSED\n");
}

// The dispatch and outcome counters are independent, and the loss rate is
// their difference. Were a recorded outcome to bump the dispatch counter too,
// that difference would be identically zero and the metric would carry no
// information. Three things follow and are checked below: recording alone
// leaves the dispatch count untouched, dispatching alone opens a non-zero gap,
// and a dispatch paired with an outcome closes it again.
static void test_schedule_record_separation() {
    auto runner = observe::mint_synthetic_probes<eff::ColdInitCtx, 4>(eff::ColdInitCtx{});
    auto const p = peer(5);
    safety::Bits<observe::TransportProbeKind> kinds{observe::TransportProbeKind::Quic};
    assert(runner.register_peer(p, kinds));

    // Recording alone.
    assert(runner.record_outcome(eff::BgDrainCtx{}, p,
                                 observe::ProbeOutcome{
                                     .kind = observe::TransportProbeKind::Quic,
                                     .failure = observe::SyntheticProbeFailureClass::None,
                                     .latency_ns = 1'000,
                                     .bytes_transferred = 64,
                                     .sequence = 1,
                                 }));
    auto stats_a = runner.stats(p, observe::TransportProbeKind::Quic);
    assert(stats_a.scheduled == 0);
    assert(stats_a.succeeded == 1);

    // Dispatching alone, twice, against one recorded outcome.
    assert(runner.schedule_probe(eff::BgDrainCtx{}, p, observe::TransportProbeKind::Quic));
    assert(runner.schedule_probe(eff::BgDrainCtx{}, p, observe::TransportProbeKind::Quic));
    auto stats_b = runner.stats(p, observe::TransportProbeKind::Quic);
    assert(stats_b.scheduled == 2);
    assert(stats_b.succeeded == 1);
    assert(stats_b.failed == 0);
    auto const inflight_b = stats_b.scheduled - stats_b.succeeded - stats_b.failed;
    assert(inflight_b == 1);

    // Every dispatch resolved by exactly one outcome, so the gap closes.
    auto runner2 = observe::mint_synthetic_probes<eff::ColdInitCtx, 4>(eff::ColdInitCtx{});
    auto const q = peer(6);
    assert(runner2.register_peer(q, kinds));
    for (std::uint64_t i = 0; i < 5; ++i) {
        assert(runner2.schedule_probe(eff::BgDrainCtx{}, q, observe::TransportProbeKind::Quic));
        assert(runner2.record_outcome(eff::BgDrainCtx{}, q,
                                      observe::ProbeOutcome{
                                          .kind = observe::TransportProbeKind::Quic,
                                          .failure = (i % 2) == 0 ? observe::SyntheticProbeFailureClass::None
                                                                  : observe::SyntheticProbeFailureClass::Timeout,
                                          .latency_ns = 500,
                                          .bytes_transferred = 32,
                                          .sequence = i,
                                      }));
    }
    auto const stats_c = runner2.stats(q, observe::TransportProbeKind::Quic);
    assert(stats_c.scheduled == 5);
    assert(stats_c.succeeded == 3);
    assert(stats_c.failed == 2);
    assert(stats_c.scheduled == stats_c.succeeded + stats_c.failed);

    // Dispatch refuses an unregistered peer and a disabled kind on the same
    // terms recording does.
    auto const missing = peer(7);
    assert(!runner.schedule_probe(eff::BgDrainCtx{}, missing, observe::TransportProbeKind::Quic));
    assert(!runner.schedule_probe(eff::BgDrainCtx{}, p, observe::TransportProbeKind::TcpBbr3));
    std::printf("  test_schedule_record_separation:  PASSED\n");
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
    std::printf("  test_probe_stats_layout_invariants: PASSED\n");
}

int main() {
    static_assert(observe::CtxFitsSyntheticProbeMint<eff::ColdInitCtx>);
    static_assert(!observe::CtxFitsSyntheticProbeMint<eff::BgDrainCtx>);
    static_assert(observe::CtxFitsSyntheticProbeRecord<eff::BgDrainCtx>);
    static_assert(!observe::CtxFitsSyntheticProbeRecord<eff::HotFgCtx>);

    std::printf("test_synthetic_probe: 6 groups\n");
    test_name_accessors();
    test_register_and_record_success();
    test_failure_accounting();
    test_rejects_unregistered_or_disabled();
    test_schedule_record_separation();
    test_probe_stats_layout_invariants();
    std::printf("test_synthetic_probe: all passed\n");
    return 0;
}
