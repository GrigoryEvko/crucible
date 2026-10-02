// The cache-tier probe family of the hardware-capability ledger: whether the
// first touch of a probe region comes from the CPU that the probe measures
// on.  test_ledger_probes tests the contract that every probe family shares.

#include <crucible/ledger/probes/CacheTier.h>

#include <fixy/os/Sched.h>

#include "test_assert.h"
#include "test_ledger_probes_fixtures.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <span>
#include <thread>
#include <vector>

#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace crucible;
using ledger_probe_fixtures::probe_ctx;

namespace {

// The first touch of a probe region comes from the CPU that the probe
// measures on, so each page is on the node of that CPU, and the pin to that
// CPU holds until the probe restores it.  The worker starts on a CPU of the
// second node and asks for a CPU of the first node.  A first touch from the
// CPU of the worker puts each page on the second node.
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
        bench::Run::PinResult pin = ledger::probes::cache_tier_detail::fault_in_from_cpu(*region, measuring_cpu);
        if (!pin.prior.has_value()) {
            skip_reason = "the cpuset refuses the CPU of the first node";
            return;
        }
        assert(::sched_getcpu() == measuring_cpu);

        // move_pages in query mode: no target nodes, so the status of each
        // page is the node that holds it.
        const std::size_t page_count = region->size() / ledger::ProbeRegion::kBasePageBytes;
        std::vector<void*> pages(page_count, nullptr);
        std::vector<int> page_nodes(page_count, -1);
        auto* first_byte = static_cast<unsigned char*>(region->data());
        for (std::size_t i = 0; i < page_count; ++i) {
            pages[i] = first_byte + (i * ledger::ProbeRegion::kBasePageBytes);
        }
        const long queried = ::syscall(SYS_move_pages, 0, page_count, pages.data(), nullptr, page_nodes.data(), 0);
        if (queried != 0) {
            (void)pin.restore();
            skip_reason = "the kernel refuses move_pages";
            return;
        }
        assert(std::all_of(page_nodes.begin(), page_nodes.end(),
                           [measuring_node](int node) { return node == measuring_node; }));

        // The restore gives the worker back its one CPU.  The calls stay
        // outside assert, so a build with NDEBUG still makes them.
        [[maybe_unused]] const bool was_restored = pin.restore().has_value();
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
    test_first_touch_comes_from_the_measuring_cpu();
    std::printf("test_ledger_probes_cache_tier: 1 group, all passed\n");
    return 0;
}
