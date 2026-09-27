// Every assertion here is structural rather than numerical.  The chosen
// factor depends on the cache sizes and core count of the host, so the
// tests state invariants that hold on any supported machine instead of
// pinning the values one machine happens to produce.
//
// Old spelling: test/test_parallelism_rule.cpp.  The boundary cells follow
// deviation 3 of fixy/concurrent/ParallelismRule.h: a set of exactly one
// cache's size is inside that cache.  A failed check records the failure
// and returns from its case, because nothing in this tree throws.

#include <fixy/concurrent/ParallelismRule.h>
#include <fixy/concurrent/Topology.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace fixy::concurrent;

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            ++total_failed;                                                               \
            return;                                                                       \
        }                                                                                 \
    } while (0)

namespace {

int total_failed = 0;

[[nodiscard]] constexpr bool on_factor_ladder_(std::size_t factor) noexcept {
    return factor == 1 || factor == 2 || factor == 4 || factor == 8 || factor == 16;
}

void test_classify_boundaries() {
    const auto& topo = Topology::instance();
    const std::size_t l1d = topo.l1d_per_core_bytes();
    const std::size_t l2 = topo.l2_per_core_bytes();
    const std::size_t l3 = topo.l3_total_bytes();

    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(0) == Tier::L1Resident);
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l1d) == Tier::L1Resident);

    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l1d + 1) == Tier::L2Resident);
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l2) == Tier::L2Resident);

    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l2 + 1) == Tier::L3Resident);
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l3) == Tier::L3Resident);

    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l3 + 1) == Tier::DRAMBound);
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l3 * 10) == Tier::DRAMBound);
}

// The one question Spawn and Pipeline ask answers yes up to and including
// L2, and no one byte past it.
void test_core_resident_boundary() {
    const std::size_t l2 = Topology::instance().l2_per_core_bytes();
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::is_core_resident(0));
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::is_core_resident(l2));
    CRUCIBLE_TEST_REQUIRE(!ParallelismRule::is_core_resident(l2 + 1));
}

void test_sequential_when_l1_resident() {
    WorkBudget budget{
        .read_bytes = 512,  // well below any L1d
        .write_bytes = 512,
        .item_count = 128,
    };
    const auto dec = ParallelismRule::recommend(budget);
    CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Sequential);
    CRUCIBLE_TEST_REQUIRE(dec.factor == 1);
    CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::L1Resident);
    CRUCIBLE_TEST_REQUIRE(!dec.is_parallel());
}

void test_sequential_when_l2_resident() {
    const std::size_t l1d = Topology::instance().l1d_per_core_bytes();
    // Twice L1 is past L1 and still inside L2 on any supported host.
    const std::size_t ws_total = (l1d * 2);

    WorkBudget budget{
        .read_bytes = ws_total / 2,
        .write_bytes = ws_total / 2,
        .item_count = 1024,
    };
    const auto dec = ParallelismRule::recommend(budget);
    CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Sequential);
    CRUCIBLE_TEST_REQUIRE(dec.factor == 1);
    CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::L2Resident);
}

void test_parallel_when_dram_bound() {
    const auto& topo = Topology::instance();
    // Twice L3 leaves no doubt about the tier on any supported host.
    const std::size_t ws = topo.l3_total_bytes() * 2;

    WorkBudget budget{
        .read_bytes = ws / 2,
        .write_bytes = ws / 2,
        .item_count = 1000000,
    };
    const auto dec = ParallelismRule::recommend(budget);
    if (topo.process_cpu_count() >= 2) {
        CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Parallel);
        CRUCIBLE_TEST_REQUIRE(dec.factor >= 2);
        CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::DRAMBound);
    }
    CRUCIBLE_TEST_REQUIRE(on_factor_ladder_(dec.factor));
}

void test_factor_ladder() {
    std::size_t bad_count = 0;
    for (std::size_t ws_kb = 1; ws_kb <= 1024 * 1024; ws_kb *= 4) {
        const std::size_t ws = ws_kb * 1024;
        WorkBudget budget{
            .read_bytes = ws / 2,
            .write_bytes = ws / 2,
            .item_count = 10000,
        };
        const auto dec = ParallelismRule::recommend(budget);
        if (!on_factor_ladder_(dec.factor)) {
            std::fprintf(stderr, "  OFF-LADDER factor=%zu for ws=%zu\n", dec.factor, ws);
            ++bad_count;
        }
    }
    CRUCIBLE_TEST_REQUIRE(bad_count == 0);
}

void test_kind_matches_factor() {
    const std::size_t l3 = Topology::instance().l3_total_bytes();
    const WorkBudget budgets[] = {
        {.read_bytes = 128, .write_bytes = 128, .item_count = 32},
        {.read_bytes = 4096, .write_bytes = 4096, .item_count = 1024},
        {.read_bytes = l3 * 2, .write_bytes = 0, .item_count = 1000000},
        {.read_bytes = 1024, .write_bytes = 1024, .item_count = 10000},
    };
    for (const auto& budget : budgets) {
        const auto dec = ParallelismRule::recommend(budget);
        if (dec.kind == ParallelismDecision::Kind::Sequential) {
            CRUCIBLE_TEST_REQUIRE(dec.factor == 1);
        } else {
            CRUCIBLE_TEST_REQUIRE(dec.factor >= 2);
            CRUCIBLE_TEST_REQUIRE(dec.is_parallel());
        }
    }
}

void test_numa_policy_l3_resident() {
    // L3 is shared per socket, so a set that fits in it should stay on
    // one socket.
    const auto& topo = Topology::instance();
    const std::size_t l2 = topo.l2_per_core_bytes();
    const std::size_t l3 = topo.l3_total_bytes();
    if (l3 <= l2 * 2) return;  // no room between the tiers on this host

    // Halfway between the two tiers lands inside L3 wherever they sit.
    const std::size_t ws = (l2 + l3) / 2;
    WorkBudget budget{
        .read_bytes = ws / 2,
        .write_bytes = ws / 2,
        .item_count = 1000000,
    };
    const auto dec = ParallelismRule::recommend(budget);
    if (dec.kind == ParallelismDecision::Kind::Parallel && dec.tier == Tier::L3Resident) {
        CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaLocal);
        CRUCIBLE_TEST_REQUIRE(dec.factor <= 4);
    }
}

void test_numa_policy_dram_bound() {
    const auto& topo = Topology::instance();
    const std::size_t ws = topo.l3_total_bytes() * 2;
    WorkBudget budget{
        .read_bytes = ws,
        .write_bytes = 0,
        .item_count = 1000000,
    };
    const auto dec = ParallelismRule::recommend(budget);
    if (dec.kind == ParallelismDecision::Kind::Parallel) {
        if (topo.numa_nodes() > 1) {
            CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaSpread);
        } else {
            CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaIgnore);
        }
    }
}

void test_determinism() {
    WorkBudget budget{
        .read_bytes = 1000000,
        .write_bytes = 1000000,
        .item_count = 100000,
    };
    const auto first = ParallelismRule::recommend(budget);
    for (int i = 0; i < 100; ++i) {
        const auto again = ParallelismRule::recommend(budget);
        CRUCIBLE_TEST_REQUIRE(again.kind == first.kind);
        CRUCIBLE_TEST_REQUIRE(again.factor == first.factor);
        CRUCIBLE_TEST_REQUIRE(again.numa == first.numa);
        CRUCIBLE_TEST_REQUIRE(again.tier == first.tier);
    }
}

void test_container_cap() {
    const auto& topo = Topology::instance();
    const std::size_t allowed = topo.process_cpu_count();

    // The budget is made far larger than L3 so the factor is driven to its
    // maximum.  Only then does the cap get exercised at all.
    WorkBudget budget{
        .read_bytes = topo.l3_total_bytes() * 100,
        .write_bytes = 0,
        .item_count = 100000000,
    };
    const auto dec = ParallelismRule::recommend(budget);
    CRUCIBLE_TEST_REQUIRE(dec.factor <= allowed);
    CRUCIBLE_TEST_REQUIRE(on_factor_ladder_(dec.factor));
}

void test_budget_for_span() {
    const auto budget = ParallelismRule::budget_for_span<std::uint64_t>(/*count=*/1024);
    CRUCIBLE_TEST_REQUIRE(budget.item_count == 1024);
    CRUCIBLE_TEST_REQUIRE(budget.read_bytes == 1024 * sizeof(std::uint64_t));
    CRUCIBLE_TEST_REQUIRE(budget.write_bytes == 1024 * sizeof(std::uint64_t));
}

// A budget whose read and write counts would wrap when added saturates
// instead, so the rule sees a DRAM-bound set and not a tiny one.
void test_saturated_budget_is_not_small() {
    const WorkBudget budget{
        .read_bytes = static_cast<std::size_t>(-1),
        .write_bytes = 2,
        .item_count = 1,
    };
    const auto dec = ParallelismRule::recommend(budget);
    CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::DRAMBound);
}

// Any budget that fits in L2 must stay sequential.  Splitting such a
// budget across cores costs more in cache traffic than the extra cores
// return, so a parallel answer here is a regression at some call site.
void test_no_regression_invariant() {
    const std::size_t l2 = Topology::instance().l2_per_core_bytes();

    for (std::size_t ws : {std::size_t{0}, std::size_t{256}, std::size_t{4096}, l2 / 2, l2}) {
        for (std::size_t items : {std::size_t{1}, std::size_t{100}, std::size_t{10000}}) {
            WorkBudget budget{
                .read_bytes = ws / 2,
                .write_bytes = ws - ws / 2,
                .item_count = items,
            };
            const auto dec = ParallelismRule::recommend(budget);
            if (dec.kind != ParallelismDecision::Kind::Sequential) {
                std::fprintf(stderr,
                             "  REGRESSION: parallel chosen for ws=%zu "
                             "items=%zu → factor=%zu tier=%d\n",
                             ws, items, dec.factor, static_cast<int>(dec.tier));
                CRUCIBLE_TEST_REQUIRE(false);
            }
        }
    }
}

void run_test(const char* name, void (*body)()) {
    const int failed_before = total_failed;
    body();
    std::fprintf(stderr, "  %s: %s\n", name, total_failed == failed_before ? "PASSED" : "FAILED");
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_parallelism_rule:\n");

    // The summary comes first because every result below depends on the
    // host topology and cannot be read without it.
    Topology::instance().log_summary(stderr);
    std::fprintf(stderr, "\n");

    run_test("classify boundaries", test_classify_boundaries);
    run_test("core-resident boundary", test_core_resident_boundary);
    run_test("sequential when L1-resident", test_sequential_when_l1_resident);
    run_test("sequential when L2-resident", test_sequential_when_l2_resident);
    run_test("parallel when DRAM-bound", test_parallel_when_dram_bound);
    run_test("factor always on ladder", test_factor_ladder);
    run_test("Sequential iff factor==1", test_kind_matches_factor);
    run_test("NumaLocal on L3-resident", test_numa_policy_l3_resident);
    run_test("NumaSpread/Ignore on DRAM", test_numa_policy_dram_bound);
    run_test("determinism", test_determinism);
    run_test("container-aware factor cap", test_container_cap);
    run_test("budget_for_span helper", test_budget_for_span);
    run_test("saturated budget is not small", test_saturated_budget_is_not_small);
    run_test("no-regression invariant", test_no_regression_invariant);

    std::fprintf(stderr, "\n%d failed\n", total_failed);
    return total_failed == 0 ? 0 : 1;
}
