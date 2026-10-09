// The cache-tier probe family of the hardware-capability ledger: whether the
// first touch of a probe region comes from the CPU that the probe measures
// on.  test_ledger_probes tests the contract that every probe family shares.

#include <crucible/ledger/probes/CacheTier.h>

#include <fixy/Core.h>
#include <fixy/os/Topology.h>
#include <fixy/os/NumaCounters.h>
#include <fixy/os/NumaPlacement.h>
#include <fixy/os/Sched.h>

#include "test_assert.h"
#include "test_ledger_probes_fixtures.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <thread>
#include <utility>

#include <sched.h>

using namespace crucible;
using ledger_probe_fixtures::probe_ctx;

namespace {

// The numa_foreign counter of the node: the allocations that preferred the
// node and got a page of another node.  No value when the kernel gives no
// counter for the node.
[[nodiscard]] ::fixy::Option<std::uint64_t> foreign_allocations_of(int node) noexcept {
    ::fixy::Option<::fixy::NumaNodeId> concrete = ::fixy::numa::concrete_numa_node(node);
    if (concrete.is_none()) return ::fixy::none;
    auto counters = ::fixy::numa::read_allocation_counters(probe_ctx, std::move(concrete).expect("the node has an id"));
    if (counters.is_err()) return ::fixy::none;
    return ::fixy::Option<std::uint64_t>::some(std::move(counters).expect("the kernel gave the counters").foreign);
}

// True when the two reads each give a count, and the count did not move.
[[nodiscard]] bool counts_agree(::fixy::Option<std::uint64_t> before, ::fixy::Option<std::uint64_t> after) noexcept {
    if (before.is_none() || after.is_none()) return false;
    return std::move(before).expect("the count is present") == std::move(after).expect("the count is present");
}

// A census with no page, and a census of the region read for a node that
// holds none of its pages, each give the placement fault.
void test_a_wrong_census_refuses_the_placement() {
    constexpr std::size_t region_pages = 8;
    const ::fixy::NumaNodeId node_zero = ::fixy::numa::concrete_numa_node(0).expect("node 0 has an id");
    assert(
        ledger::probes::cache_tier_detail::placement_fault_of(::fixy::numa::PageNodeCensus{}, node_zero, region_pages)
        == ledger::LedgerError::ConfidenceBelowBar);
    assert(ledger::probes::cache_tier_detail::placement_fault_of(::fixy::numa::PageNodeCensus{}, node_zero, 0)
           == ledger::LedgerError::ConfidenceBelowBar);
    assert(::fixy::numa::concrete_numa_node(-1).is_none() && ::fixy::numa::concrete_numa_node(254).is_none());
    crucible::test::pass("  test_a_wrong_census_refuses_the_placement: PASSED\n");
}

// The first touch of a probe region comes from the CPU that the probe
// measures on, so each page is on the node of that CPU, and the pin to that
// CPU holds until the probe restores it.  The worker starts on a CPU of the
// second node and asks for a CPU of the first node.  A first touch from the
// CPU of the worker puts each page on the second node.
//
// A first touch places a page only when the node has free pages above its
// low watermark.  Under memory pressure the kernel takes the pages from
// another node, and the node of a page then does not show the CPU of its
// first touch.  The numa_foreign counters of the two nodes show each such
// fallback, so the test reads them before and after the touch, and it
// cannot measure when one of them moved.
void test_first_touch_comes_from_the_measuring_cpu() {
    const auto& topology = ::fixy::concurrent::Topology::instance();
    const std::span<const int> nodes = topology.numa_node_ids();
    if (nodes.size() < 2 || topology.cores_on_node(nodes[0]).empty() || topology.cores_on_node(nodes[1]).empty()) {
        std::printf("  test_first_touch_comes_from_the_measuring_cpu: SKIPPED (fewer than two NUMA nodes with CPUs, "
                    "so each page is on the node of each CPU)\n");
        return;
    }
    const int measuring_node = nodes[0];
    const int measuring_cpu = topology.cores_on_node(measuring_node).front();
    const int worker_cpu = topology.cores_on_node(nodes[1]).front();

    const char* skip_reason = nullptr;
    std::jthread worker{[&skip_reason, measuring_node, measuring_cpu, worker_cpu] {
        const ::fixy::BgLoadCtx background{::foundation::effects::testing::bg()};
        const auto start = ::fixy::sched::apply_affinity_to_cpu(background, worker_cpu);
        if (!start.has_value()) {
            skip_reason = "the cpuset refuses the CPU of the second node";
            return;
        }
        auto region = ledger::ProbeRegion::create(probe_ctx, 2u * 1024u * 1024u, ledger::PagePolicy::BasePages);
        assert(region.has_value());
        const int worker_node = ::fixy::concurrent::Topology::instance()
                                    .numa_node_of_cpu(worker_cpu)
                                    .expect("the topology gives the node of a CPU that it lists for a node");
        const ::fixy::Option<std::uint64_t> measuring_fallbacks = foreign_allocations_of(measuring_node);
        const ::fixy::Option<std::uint64_t> worker_fallbacks = foreign_allocations_of(worker_node);
        ledger::probes::cache_tier_detail::FirstTouch touch =
            ledger::probes::cache_tier_detail::fault_in_from_cpu(probe_ctx, *region, measuring_cpu);
        if (!touch.pin.prior.has_value()) {
            skip_reason = "the cpuset refuses the CPU of the first node";
            return;
        }
        assert(::sched_getcpu() == measuring_cpu);

        // A counter that moved, or that the kernel does not give, leaves the
        // node of a page with no meaning for the CPU of its first touch.
        const bool is_steady = counts_agree(measuring_fallbacks, foreign_allocations_of(measuring_node))
                            && counts_agree(worker_fallbacks, foreign_allocations_of(worker_node));
        if (!is_steady) {
            (void)touch.pin.restore();
            skip_reason = "the kernel took a page of another node for an allocation that preferred a node of the test";
            return;
        }
        assert(touch.placement == ledger::LedgerError::None);

        // The census of the region holds no page on the node of the worker,
        // so a placement on that node is refused.
        const ::fixy::NumaNodeId worker_node_id =
            ::fixy::numa::concrete_numa_node(worker_node).expect("the topology names each node below 254");
        assert(ledger::probes::cache_tier_detail::placement_fault_of(
                   touch.census, worker_node_id, (2u * 1024u * 1024u) / ledger::ProbeRegion::kBasePageBytes)
               == ledger::LedgerError::ConfidenceBelowBar);

        // The restore gives the worker back its one CPU.  The calls stay
        // outside assert, so a build with NDEBUG still makes them.
        [[maybe_unused]] const bool was_restored = touch.pin.restore().has_value();
        assert(was_restored);
        cpu_set_t affinity;
        CPU_ZERO(&affinity);
        [[maybe_unused]] const int affinity_status = ::sched_getaffinity(0, sizeof(affinity), &affinity);
        assert(affinity_status == 0);
        assert(CPU_COUNT(&affinity) == 1 && CPU_ISSET(static_cast<std::size_t>(worker_cpu), &affinity));
    }};
    worker.join();

    if (skip_reason != nullptr) {
        std::printf("  test_first_touch_comes_from_the_measuring_cpu: SKIPPED (%s)\n", skip_reason);
        return;
    }
    std::printf("  test_first_touch_comes_from_the_measuring_cpu: PASSED\n");
}

}  // namespace

namespace crucible::ledger::probes {

namespace cache_tier_detail::self_test {

// A worker owns a thread that captured `this`, so neither a copy nor a
// move may exist.
static_assert(!std::is_copy_constructible_v<SliceWorker>);
static_assert(!std::is_move_constructible_v<SliceWorker>);

// A default measurement is not usable and does not claim a bracket, so a
// caller that forgets to check is_usable() still cannot read a byte count
// out of a probe that never ran.
static_assert(!CacheTierMeasurement{}.is_usable());
static_assert(!CacheTierMeasurement{}.found_a_bracket());
static_assert(!NumaMeasurement{}.is_usable());

// A ceiling below its knee is not a bracket. The sweep assigns the
// ceiling on every winning point after the knee, so this can only come
// from a corrupted structure, and the predicate catches it rather than
// handing a caller an inverted range.
static_assert(!CacheTierMeasurement{
    .fault = LedgerError::None, .parallel_knee_bytes = 1024, .parallel_ceiling_bytes = 512}
                   .found_a_bracket());
static_assert(CacheTierMeasurement{
    .fault = LedgerError::None, .parallel_knee_bytes = 1024, .parallel_ceiling_bytes = 1024}
                  .found_a_bracket());

// The sweep must actually step. A ratio that rounds back to itself would
// loop forever on the smallest size.
static_assert(kSweepStepNumerator > kSweepStepDenominator);
static_assert((kSweepFloorBytes * kSweepStepNumerator) / kSweepStepDenominator > kSweepFloorBytes);

}  // namespace cache_tier_detail::self_test

}  // namespace crucible::ledger::probes

int main() {
    std::printf("test_ledger_probes_cache_tier:\n");
    test_a_wrong_census_refuses_the_placement();
    test_first_touch_comes_from_the_measuring_cpu();
    crucible::test::pass("test_ledger_probes_cache_tier: 2 groups, all passed\n");
    return 0;
}
