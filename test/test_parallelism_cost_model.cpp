// ═══════════════════════════════════════════════════════════════════
// test_parallelism_cost_model — SEPLOG-C2 cache-tier decision table
//
// Validates concurrent::WorkingSetEstimator's recommend() function
// against the rules stated in concurrent/CostModel.h.  The tests are
// STRUCTURAL (asserting the decision tree) rather than numerical
// (asserting exact factors) — the factor ladder output depends on
// the host topology, so tests assert invariants that hold across ALL
// reasonable topologies (x86 Zen/Skylake, ARM Graviton, Apple Silicon).
//
// Covered:
//   1. classify() tier boundaries
//   2. Sequential when L1/L2-resident (never regresses rule)
//   3. Parallel when DRAM-bound with enough amortization
//   4. Compute-bound override fires for small data + heavy per-item
//   5. Amortization gate fires for small total work
//   6. Factor is always in {1, 2, 4, 8, 16}
//   7. decision.is_parallel() iff factor > 1 (Sequential iff factor==1)
//   8. NumaPolicy matches tier expectations
//   9. Determinism: same budget → same decision
//  10. Container-awareness: factor ≤ process_cpu_count
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/CostModel.h>
#include <crucible/concurrent/Topology.h>

#include <cstdio>
#include <cstdlib>

using namespace crucible::concurrent;

// ── Test harness ─────────────────────────────────────────────────

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                          \
    do {                                                                    \
        if (!(__VA_ARGS__)) [[unlikely]] {                                  \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                      \
                         #__VA_ARGS__, __FILE__, __LINE__);                 \
            throw TestFailure{};                                            \
        }                                                                   \
    } while (0)

namespace {

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    std::fflush(stderr);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

// ── Helper: is_power_of_two_on_ladder ─────────────────────────────
//
// Factor must snap to the fixed ladder {1, 2, 4, 8, 16}.
[[nodiscard]] bool on_factor_ladder_(std::size_t f) noexcept {
    return f == 1 || f == 2 || f == 4 || f == 8 || f == 16;
}

// ══════════════════════════════════════════════════════════════════
// 1. classify() tier boundaries
// ══════════════════════════════════════════════════════════════════

void test_classify_boundaries() {
    const auto& topo = Topology::instance();
    const std::size_t l1d = topo.l1d_per_core_bytes();
    const std::size_t l2  = topo.l2_per_core_bytes();
    const std::size_t l3  = topo.l3_total_bytes();

    // Zero bytes is always L1Resident (trivially fits).
    CRUCIBLE_TEST_REQUIRE(WorkingSetEstimator::classify(0) == Tier::L1Resident);

    // Just below L1d boundary → L1Resident.
    CRUCIBLE_TEST_REQUIRE(WorkingSetEstimator::classify(l1d - 1) == Tier::L1Resident);

    // Exactly L1d → L2Resident (L1 is strictly less than).
    CRUCIBLE_TEST_REQUIRE(WorkingSetEstimator::classify(l1d) == Tier::L2Resident);

    // Just below L2 → still L2Resident.
    CRUCIBLE_TEST_REQUIRE(WorkingSetEstimator::classify(l2 - 1) == Tier::L2Resident);

    // Exactly L2 → L3Resident.
    CRUCIBLE_TEST_REQUIRE(WorkingSetEstimator::classify(l2) == Tier::L3Resident);

    // Just below L3 → still L3Resident.
    CRUCIBLE_TEST_REQUIRE(WorkingSetEstimator::classify(l3 - 1) == Tier::L3Resident);

    // Exactly L3 or above → DRAMBound.
    CRUCIBLE_TEST_REQUIRE(WorkingSetEstimator::classify(l3) == Tier::DRAMBound);
    CRUCIBLE_TEST_REQUIRE(WorkingSetEstimator::classify(l3 * 10) == Tier::DRAMBound);
}

// ══════════════════════════════════════════════════════════════════
// 2. Sequential when L1/L2-resident (never regresses)
// ══════════════════════════════════════════════════════════════════

void test_sequential_when_l1_resident() {
    // Tiny working set — a handful of cache lines.  Must be Sequential.
    WorkBudget b{
        .read_bytes          = 512,     // well below any L1d
        .write_bytes         = 512,
        .item_count          = 128,
        .per_item_compute_ns = 0,
    };
    const auto dec = WorkingSetEstimator::recommend(b);
    CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Sequential);
    CRUCIBLE_TEST_REQUIRE(dec.factor == 1);
    CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::L1Resident);
    CRUCIBLE_TEST_REQUIRE(!dec.is_parallel());
}

void test_sequential_when_l2_resident() {
    const auto& topo = Topology::instance();
    const std::size_t l1d = topo.l1d_per_core_bytes();
    // Choose a size that's L1-evicted but L2-resident.
    // l1d bytes + 1 is the smallest L2Resident value.
    const std::size_t ws_total = (l1d * 2);

    WorkBudget b{
        .read_bytes          = ws_total / 2,
        .write_bytes         = ws_total / 2,
        .item_count          = 1024,
        .per_item_compute_ns = 0,        // memory-bound
    };
    const auto dec = WorkingSetEstimator::recommend(b);
    CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Sequential);
    CRUCIBLE_TEST_REQUIRE(dec.factor == 1);
    CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::L2Resident);
}

// ══════════════════════════════════════════════════════════════════
// 3. Parallel when DRAM-bound with enough amortization
// ══════════════════════════════════════════════════════════════════

void test_parallel_when_dram_bound() {
    const auto& topo = Topology::instance();
    const std::size_t l3 = topo.l3_total_bytes();
    // Working set 2× L3 — definitely DRAM-bound.
    const std::size_t ws = l3 * 2;

    WorkBudget b{
        .read_bytes          = ws / 2,
        .write_bytes         = ws / 2,
        .item_count          = 1'000'000,
        .per_item_compute_ns = 5,       // memory-bound but some work
    };
    const auto dec = WorkingSetEstimator::recommend(b);
    // Only check parallel if the host actually has >= 2 cores to use.
    if (topo.process_cpu_count() >= 2) {
        CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Parallel);
        CRUCIBLE_TEST_REQUIRE(dec.factor >= 2);
        CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::DRAMBound);
    }
    CRUCIBLE_TEST_REQUIRE(on_factor_ladder_(dec.factor));
}

// ══════════════════════════════════════════════════════════════════
// 4. Compute-bound override — small data, heavy per-item compute
// ══════════════════════════════════════════════════════════════════

void test_compute_bound_override() {
    // TINY working set (L1-resident) but heavy per-item compute.
    // The override must pick Parallel because per-item work amortizes
    // spawn regardless of data footprint.
    WorkBudget b{
        .read_bytes          = 512,     // tiny
        .write_bytes         = 512,
        .item_count          = 1'000,
        .per_item_compute_ns = 500,     // heavy: 500 ns per item
    };
    const auto dec = WorkingSetEstimator::recommend(b);
    if (Topology::instance().process_cpu_count() >= 2) {
        CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Parallel);
        CRUCIBLE_TEST_REQUIRE(dec.factor >= 2);
        CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaIgnore);
    }
}

void test_compute_bound_override_below_threshold() {
    // Heavy per-item but TOO FEW items — below kMinItemsForCompute.
    // Must NOT trigger the override; should stay Sequential (L1-resident).
    WorkBudget b{
        .read_bytes          = 512,
        .write_bytes         = 512,
        .item_count          = 16,      // below 64
        .per_item_compute_ns = 500,
    };
    const auto dec = WorkingSetEstimator::recommend(b);
    CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Sequential);
}

// ══════════════════════════════════════════════════════════════════
// 5. Amortization gate — DRAM working set but too little work
// ══════════════════════════════════════════════════════════════════

void test_amortization_gate() {
    const auto& topo = Topology::instance();
    const std::size_t l3 = topo.l3_total_bytes();
    // Large data but per_item_compute_ns = 0 → total compute = 0 ns.
    // Sequential wins because spawn cost (~200 µs) > total work.
    WorkBudget b{
        .read_bytes          = l3 * 2,
        .write_bytes         = 0,
        .item_count          = 100,     // tiny item count
        .per_item_compute_ns = 0,       // no compute at all
    };
    const auto dec = WorkingSetEstimator::recommend(b);
    // With 0 compute, total_compute_ns < spawn threshold → Sequential.
    CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Sequential);
}

// ══════════════════════════════════════════════════════════════════
// 6. Factor is always on the ladder
// ══════════════════════════════════════════════════════════════════

void test_factor_ladder() {
    const auto& topo = Topology::instance();
    // Sweep working set sizes and compute intensities; every
    // decision's factor must be in {1, 2, 4, 8, 16}.
    std::size_t bad_count = 0;
    for (std::size_t ws_kb = 1; ws_kb <= 1024 * 1024; ws_kb *= 4) {
        const std::size_t ws = ws_kb * 1024;
        for (std::size_t ns : {std::size_t{0}, std::size_t{50},
                               std::size_t{200}, std::size_t{1000}}) {
            WorkBudget b{
                .read_bytes          = ws / 2,
                .write_bytes         = ws / 2,
                .item_count          = 10'000,
                .per_item_compute_ns = ns,
            };
            const auto dec = WorkingSetEstimator::recommend(b);
            if (!on_factor_ladder_(dec.factor)) {
                std::fprintf(stderr,
                    "  OFF-LADDER factor=%zu for ws=%zu ns=%zu\n",
                    dec.factor, ws, ns);
                ++bad_count;
            }
        }
    }
    (void)topo;
    CRUCIBLE_TEST_REQUIRE(bad_count == 0);
}

// ══════════════════════════════════════════════════════════════════
// 7. Sequential iff factor == 1
// ══════════════════════════════════════════════════════════════════

void test_kind_matches_factor() {
    // Sweep a mix of budgets and verify the invariant.
    const std::size_t l3 = Topology::instance().l3_total_bytes();
    const WorkBudget budgets[] = {
        {.read_bytes = 128,   .write_bytes = 128,   .item_count = 32,       .per_item_compute_ns = 0},
        {.read_bytes = 4096,  .write_bytes = 4096,  .item_count = 1024,     .per_item_compute_ns = 10},
        {.read_bytes = l3*2,  .write_bytes = 0,     .item_count = 1'000'000, .per_item_compute_ns = 5},
        {.read_bytes = 1024,  .write_bytes = 1024,  .item_count = 10'000,    .per_item_compute_ns = 500},
    };
    for (const auto& b : budgets) {
        const auto dec = WorkingSetEstimator::recommend(b);
        if (dec.kind == ParallelismDecision::Kind::Sequential) {
            CRUCIBLE_TEST_REQUIRE(dec.factor == 1);
        } else {
            CRUCIBLE_TEST_REQUIRE(dec.factor >= 2);
            CRUCIBLE_TEST_REQUIRE(dec.is_parallel());
        }
    }
}

// ══════════════════════════════════════════════════════════════════
// 8. NumaPolicy matches tier expectations
// ══════════════════════════════════════════════════════════════════

void test_numa_policy_l3_resident() {
    // L3-resident + compute → NumaLocal (stay within socket).
    const auto& topo = Topology::instance();
    const std::size_t l2 = topo.l2_per_core_bytes();
    const std::size_t l3 = topo.l3_total_bytes();
    if (l3 <= l2 * 2) return;  // tiny cache hierarchy — skip

    // Pick a working set in the middle of L3 capacity.
    const std::size_t ws = (l2 + l3) / 2;
    WorkBudget b{
        .read_bytes          = ws / 2,
        .write_bytes         = ws / 2,
        .item_count          = 1'000'000,
        .per_item_compute_ns = 20,
    };
    const auto dec = WorkingSetEstimator::recommend(b);
    if (dec.kind == ParallelismDecision::Kind::Parallel &&
        dec.tier == Tier::L3Resident)
    {
        CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaLocal);
        // L3-resident cap: factor ≤ 4.
        CRUCIBLE_TEST_REQUIRE(dec.factor <= 4);
    }
}

void test_numa_policy_dram_bound() {
    // DRAM-bound: NumaSpread if >1 NUMA node, else NumaIgnore.
    const auto& topo = Topology::instance();
    const std::size_t ws = topo.l3_total_bytes() * 2;
    WorkBudget b{
        .read_bytes          = ws,
        .write_bytes         = 0,
        .item_count          = 1'000'000,
        .per_item_compute_ns = 5,
    };
    const auto dec = WorkingSetEstimator::recommend(b);
    if (dec.kind == ParallelismDecision::Kind::Parallel) {
        if (topo.numa_nodes() > 1) {
            CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaSpread);
        } else {
            CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaIgnore);
        }
    }
}

// ══════════════════════════════════════════════════════════════════
// 9. Determinism — same budget always yields same decision
// ══════════════════════════════════════════════════════════════════

void test_determinism() {
    WorkBudget b{
        .read_bytes          = 1'000'000,
        .write_bytes         = 1'000'000,
        .item_count          = 100'000,
        .per_item_compute_ns = 50,
    };
    const auto d1 = WorkingSetEstimator::recommend(b);
    for (int i = 0; i < 100; ++i) {
        const auto d = WorkingSetEstimator::recommend(b);
        CRUCIBLE_TEST_REQUIRE(d.kind   == d1.kind);
        CRUCIBLE_TEST_REQUIRE(d.factor == d1.factor);
        CRUCIBLE_TEST_REQUIRE(d.numa   == d1.numa);
        CRUCIBLE_TEST_REQUIRE(d.tier   == d1.tier);
    }
}

// ══════════════════════════════════════════════════════════════════
// 10. Container-aware factor cap
// ══════════════════════════════════════════════════════════════════

void test_container_cap() {
    const auto& topo = Topology::instance();
    const std::size_t allowed = topo.process_cpu_count();

    // Any decision's factor must not exceed the process's CPU allowance.
    // Test with a DRAM-bound workload (which tries to maximize factor).
    WorkBudget b{
        .read_bytes          = topo.l3_total_bytes() * 100,
        .write_bytes         = 0,
        .item_count          = 100'000'000,
        .per_item_compute_ns = 10,
    };
    const auto dec = WorkingSetEstimator::recommend(b);
    CRUCIBLE_TEST_REQUIRE(dec.factor <= allowed);
    // And factor must be on the ladder.
    CRUCIBLE_TEST_REQUIRE(on_factor_ladder_(dec.factor));
}

// ══════════════════════════════════════════════════════════════════
// 11. recommend_parallelism free-function equivalence
// ══════════════════════════════════════════════════════════════════

void test_free_function_equivalence() {
    WorkBudget b{
        .read_bytes          = 64 * 1024 * 1024,
        .write_bytes         = 64 * 1024 * 1024,
        .item_count          = 1'000'000,
        .per_item_compute_ns = 100,
    };
    const auto a = WorkingSetEstimator::recommend(b);
    const auto c = recommend_parallelism(b);
    CRUCIBLE_TEST_REQUIRE(a.kind   == c.kind);
    CRUCIBLE_TEST_REQUIRE(a.factor == c.factor);
    CRUCIBLE_TEST_REQUIRE(a.numa   == c.numa);
    CRUCIBLE_TEST_REQUIRE(a.tier   == c.tier);
}

// ══════════════════════════════════════════════════════════════════
// 12. budget_for_span helper
// ══════════════════════════════════════════════════════════════════

void test_budget_for_span() {
    const auto b = WorkingSetEstimator::budget_for_span<std::uint64_t>(
        /*count=*/1024, /*ns_per_item=*/50);
    CRUCIBLE_TEST_REQUIRE(b.item_count == 1024);
    CRUCIBLE_TEST_REQUIRE(b.read_bytes == 1024 * sizeof(std::uint64_t));
    CRUCIBLE_TEST_REQUIRE(b.write_bytes == 1024 * sizeof(std::uint64_t));
    CRUCIBLE_TEST_REQUIRE(b.per_item_compute_ns == 50);
}

// ══════════════════════════════════════════════════════════════════
// 13. The no-regression rule, stated as invariant
// ══════════════════════════════════════════════════════════════════
//
// For ANY budget whose working set fits in L2 AND where per-item
// compute is cheap (< 100 ns), the decision MUST be Sequential.
// Violating this invariant means SOME call site will regress.

void test_no_regression_invariant() {
    const auto& topo = Topology::instance();
    const std::size_t l2 = topo.l2_per_core_bytes();

    // Sweep small working sets × various cheap compute levels.
    for (std::size_t ws : {std::size_t{0}, std::size_t{256},
                           std::size_t{4096}, l2 / 2, l2 - 1}) {
        for (std::size_t ns : {std::size_t{0}, std::size_t{10},
                               std::size_t{99}}) {
            for (std::size_t items : {std::size_t{1}, std::size_t{100},
                                       std::size_t{10'000}}) {
                WorkBudget b{
                    .read_bytes          = ws / 2,
                    .write_bytes         = ws / 2,
                    .item_count          = items,
                    .per_item_compute_ns = ns,
                };
                const auto dec = WorkingSetEstimator::recommend(b);
                if (dec.kind != ParallelismDecision::Kind::Sequential) {
                    std::fprintf(stderr,
                        "  REGRESSION: parallel chosen for ws=%zu "
                        "items=%zu ns=%zu → factor=%zu tier=%d\n",
                        ws, items, ns, dec.factor,
                        static_cast<int>(dec.tier));
                    CRUCIBLE_TEST_REQUIRE(false);
                }
            }
        }
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_parallelism_cost_model:\n");

    // Force topology probe and log — good ops visibility at start.
    Topology::instance().log_summary(stderr);
    std::fprintf(stderr, "\n");

    run_test("classify boundaries",             test_classify_boundaries);
    run_test("sequential when L1-resident",     test_sequential_when_l1_resident);
    run_test("sequential when L2-resident",     test_sequential_when_l2_resident);
    run_test("parallel when DRAM-bound",        test_parallel_when_dram_bound);
    run_test("compute-bound override fires",    test_compute_bound_override);
    run_test("compute-bound override threshold",test_compute_bound_override_below_threshold);
    run_test("amortization gate",               test_amortization_gate);
    run_test("factor always on ladder",         test_factor_ladder);
    run_test("Sequential iff factor==1",        test_kind_matches_factor);
    run_test("NumaLocal on L3-resident",        test_numa_policy_l3_resident);
    run_test("NumaSpread/Ignore on DRAM",       test_numa_policy_dram_bound);
    run_test("determinism",                     test_determinism);
    run_test("container-aware factor cap",      test_container_cap);
    run_test("free-function equivalence",       test_free_function_equivalence);
    run_test("budget_for_span helper",          test_budget_for_span);
    run_test("no-regression invariant",         test_no_regression_invariant);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
