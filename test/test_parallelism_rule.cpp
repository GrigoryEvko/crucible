// Every assertion here is structural rather than numerical.  The chosen
// factor depends on the cache sizes and core count of the host, so the
// tests state invariants that hold on any supported machine instead of
// pinning the values one machine happens to produce.

#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/concurrent/Topology.h>

#include <cstdio>
#include <cstdlib>

using namespace crucible::concurrent;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
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

[[nodiscard]] constexpr bool on_factor_ladder_(std::size_t f) noexcept {
    return f == 1 || f == 2 || f == 4 || f == 8 || f == 16;
}

void test_classify_boundaries() {
    const auto& topo = Topology::instance();
    const std::size_t l1d = topo.l1d_per_core_bytes();
    const std::size_t l2 = topo.l2_per_core_bytes();
    const std::size_t l3 = topo.l3_total_bytes();

    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(0) == Tier::L1Resident);
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l1d - 1) == Tier::L1Resident);

    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l1d) == Tier::L2Resident);
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l2 - 1) == Tier::L2Resident);

    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l2) == Tier::L3Resident);
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l3 - 1) == Tier::L3Resident);

    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l3) == Tier::DRAMBound);
    CRUCIBLE_TEST_REQUIRE(ParallelismRule::classify(l3 * 10) == Tier::DRAMBound);
}

void test_sequential_when_l1_resident() {
    WorkBudget b{
        .read_bytes = 512,  // well below any L1d
        .write_bytes = 512,
        .item_count = 128,
    };
    const auto dec = ParallelismRule::recommend(b);
    CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Sequential);
    CRUCIBLE_TEST_REQUIRE(dec.factor == 1);
    CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::L1Resident);
    CRUCIBLE_TEST_REQUIRE(!dec.is_parallel());
}

void test_sequential_when_l2_resident() {
    const auto& topo = Topology::instance();
    const std::size_t l1d = topo.l1d_per_core_bytes();
    // Twice L1 is past L1 and still inside L2 on any supported host.
    const std::size_t ws_total = (l1d * 2);

    WorkBudget b{
        .read_bytes = ws_total / 2,
        .write_bytes = ws_total / 2,
        .item_count = 1024,
    };
    const auto dec = ParallelismRule::recommend(b);
    CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Sequential);
    CRUCIBLE_TEST_REQUIRE(dec.factor == 1);
    CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::L2Resident);
}

void test_parallel_when_dram_bound() {
    const auto& topo = Topology::instance();
    const std::size_t l3 = topo.l3_total_bytes();
    // Twice L3 leaves no doubt about the tier on any supported host.
    const std::size_t ws = l3 * 2;

    WorkBudget b{
        .read_bytes = ws / 2,
        .write_bytes = ws / 2,
        .item_count = 1000000,
    };
    const auto dec = ParallelismRule::recommend(b);
    if (topo.process_cpu_count() >= 2) {
        CRUCIBLE_TEST_REQUIRE(dec.kind == ParallelismDecision::Kind::Parallel);
        CRUCIBLE_TEST_REQUIRE(dec.factor >= 2);
        CRUCIBLE_TEST_REQUIRE(dec.tier == Tier::DRAMBound);
    }
    CRUCIBLE_TEST_REQUIRE(on_factor_ladder_(dec.factor));
}

void test_factor_ladder() {
    const auto& topo = Topology::instance();
    std::size_t bad_count = 0;
    for (std::size_t ws_kb = 1; ws_kb <= 1024 * 1024; ws_kb *= 4) {
        const std::size_t ws = ws_kb * 1024;
        WorkBudget b{
            .read_bytes = ws / 2,
            .write_bytes = ws / 2,
            .item_count = 10000,
        };
        const auto dec = ParallelismRule::recommend(b);
        if (!on_factor_ladder_(dec.factor)) {
            std::fprintf(stderr, "  OFF-LADDER factor=%zu for ws=%zu\n", dec.factor, ws);
            ++bad_count;
        }
    }
    (void)topo;
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
    for (const auto& b : budgets) {
        const auto dec = ParallelismRule::recommend(b);
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
    WorkBudget b{
        .read_bytes = ws / 2,
        .write_bytes = ws / 2,
        .item_count = 1000000,
    };
    const auto dec = ParallelismRule::recommend(b);
    if (dec.kind == ParallelismDecision::Kind::Parallel && dec.tier == Tier::L3Resident) {
        CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaLocal);
        CRUCIBLE_TEST_REQUIRE(dec.factor <= 4);
    }
}

void test_numa_policy_dram_bound() {
    const auto& topo = Topology::instance();
    const std::size_t ws = topo.l3_total_bytes() * 2;
    WorkBudget b{
        .read_bytes = ws,
        .write_bytes = 0,
        .item_count = 1000000,
    };
    const auto dec = ParallelismRule::recommend(b);
    if (dec.kind == ParallelismDecision::Kind::Parallel) {
        if (topo.numa_nodes() > 1) {
            CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaSpread);
        } else {
            CRUCIBLE_TEST_REQUIRE(dec.numa == NumaPolicy::NumaIgnore);
        }
    }
}

void test_determinism() {
    WorkBudget b{
        .read_bytes = 1000000,
        .write_bytes = 1000000,
        .item_count = 100000,
    };
    const auto d1 = ParallelismRule::recommend(b);
    for (int i = 0; i < 100; ++i) {
        const auto d = ParallelismRule::recommend(b);
        CRUCIBLE_TEST_REQUIRE(d.kind == d1.kind);
        CRUCIBLE_TEST_REQUIRE(d.factor == d1.factor);
        CRUCIBLE_TEST_REQUIRE(d.numa == d1.numa);
        CRUCIBLE_TEST_REQUIRE(d.tier == d1.tier);
    }
}

void test_container_cap() {
    const auto& topo = Topology::instance();
    const std::size_t allowed = topo.process_cpu_count();

    // The budget is made far larger than L3 so the factor is driven to its
    // maximum.  Only then does the cap get exercised at all.
    WorkBudget b{
        .read_bytes = topo.l3_total_bytes() * 100,
        .write_bytes = 0,
        .item_count = 100000000,
    };
    const auto dec = ParallelismRule::recommend(b);
    CRUCIBLE_TEST_REQUIRE(dec.factor <= allowed);
    CRUCIBLE_TEST_REQUIRE(on_factor_ladder_(dec.factor));
}

void test_free_function_equivalence() {
    WorkBudget b{
        .read_bytes = 64 * 1024 * 1024,
        .write_bytes = 64 * 1024 * 1024,
        .item_count = 1000000,
    };
    const auto a = ParallelismRule::recommend(b);
    const auto c = recommend_parallelism(b);
    CRUCIBLE_TEST_REQUIRE(a.kind == c.kind);
    CRUCIBLE_TEST_REQUIRE(a.factor == c.factor);
    CRUCIBLE_TEST_REQUIRE(a.numa == c.numa);
    CRUCIBLE_TEST_REQUIRE(a.tier == c.tier);
}

void test_budget_for_span() {
    const auto b = ParallelismRule::budget_for_span<std::uint64_t>(
        /*count=*/1024);
    CRUCIBLE_TEST_REQUIRE(b.item_count == 1024);
    CRUCIBLE_TEST_REQUIRE(b.read_bytes == 1024 * sizeof(std::uint64_t));
    CRUCIBLE_TEST_REQUIRE(b.write_bytes == 1024 * sizeof(std::uint64_t));
}

// Any budget that fits in L2 must stay sequential.  Splitting such a
// budget across cores costs more in cache traffic than the extra cores
// return, so a parallel answer here is a regression at some call site.
void test_no_regression_invariant() {
    const auto& topo = Topology::instance();
    const std::size_t l2 = topo.l2_per_core_bytes();

    for (std::size_t ws : {std::size_t{0}, std::size_t{256}, std::size_t{4096}, l2 / 2, l2 - 1}) {
        for (std::size_t items : {std::size_t{1}, std::size_t{100}, std::size_t{10000}}) {
            WorkBudget b{
                .read_bytes = ws / 2,
                .write_bytes = ws / 2,
                .item_count = items,
            };
            const auto dec = ParallelismRule::recommend(b);
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

}  // namespace

int main() {
    std::fprintf(stderr, "test_parallelism_cost_model:\n");

    // The summary comes first because every result below depends on the
    // host topology and cannot be read without it.
    Topology::instance().log_summary(stderr);
    std::fprintf(stderr, "\n");

    run_test("classify boundaries", test_classify_boundaries);
    run_test("sequential when L1-resident", test_sequential_when_l1_resident);
    run_test("sequential when L2-resident", test_sequential_when_l2_resident);
    run_test("parallel when DRAM-bound", test_parallel_when_dram_bound);
    run_test("factor always on ladder", test_factor_ladder);
    run_test("Sequential iff factor==1", test_kind_matches_factor);
    run_test("NumaLocal on L3-resident", test_numa_policy_l3_resident);
    run_test("NumaSpread/Ignore on DRAM", test_numa_policy_dram_bound);
    run_test("determinism", test_determinism);
    run_test("container-aware factor cap", test_container_cap);
    run_test("free-function equivalence", test_free_function_equivalence);
    run_test("budget_for_span helper", test_budget_for_span);
    run_test("no-regression invariant", test_no_regression_invariant);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
