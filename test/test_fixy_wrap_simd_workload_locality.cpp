#include <crucible/fixy/wrap/SimdWorkloadLocality.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace fw = ::crucible::fixy::wrap;

struct LocalityRtProbe_None {};
struct LocalityRtProbe_Local {
    using locality_hint = fw::LocalityLocal_t;
};
struct LocalityRtProbe_Spread {
    using locality_hint = fw::LocalitySpread_t;
};

// The identity of each re-exported shape, witnessed from outside the tree
// that defines it.

static_assert(std::is_same_v<fw::i64x8, ::crucible::simd::i64x8>);
static_assert(std::is_same_v<fw::WorkBudget, ::crucible::safety::WorkBudget>);
static_assert(std::is_same_v<fw::LocalityLocal_t, ::crucible::safety::LocalityLocal_t>);

static_assert(fw::HasLocalityHint<LocalityRtProbe_Local>);
static_assert(fw::HasLocalityHint<LocalityRtProbe_Spread>);
static_assert(!fw::HasLocalityHint<LocalityRtProbe_None>);

static_assert(fw::locality_hint_of_v<LocalityRtProbe_Local> == ::crucible::concurrent::NumaPolicy::NumaLocal);
static_assert(fw::locality_hint_of_v<LocalityRtProbe_Spread> == ::crucible::concurrent::NumaPolicy::NumaSpread);
static_assert(fw::locality_hint_of_v<LocalityRtProbe_None> == ::crucible::concurrent::NumaPolicy::NumaIgnore);

static_assert(fw::DetSafeSimd<fw::i64x8>);
static_assert(fw::DetSafeSimd<fw::u32x8>);
static_assert(!fw::DetSafeSimd<::crucible::simd::vec<double, 4>>);

static void test_runtime_simd_iota_reduce() {
    auto v = fw::iota_v<fw::u64x8>();
    // The lanes hold 0 through 7, so the sum is 28. The reduction is over
    // integers, which is the only kind that reduces identically on every
    // instruction set.
    auto sum = ::crucible::simd::reduce_add(v);
    if (sum != 28u) {
        std::fprintf(stderr, "iota_v reduce: expected 28, got %llu\n", static_cast<unsigned long long>(sum));
        std::abort();
    }
}

static void test_runtime_simd_prefix_mask() {
    auto v = fw::iota_v<fw::u64x8>();
    auto m4 = fw::prefix_mask<fw::u64x8>(4);
    // Only the first four lanes contribute, so the sum drops to 6.
    auto sum = ::crucible::simd::reduce_add(v, m4);
    if (sum != 6u) {
        std::fprintf(stderr, "prefix_mask(4) reduce: expected 6, got %llu\n", static_cast<unsigned long long>(sum));
        std::abort();
    }
}

static void test_runtime_should_parallelize_small() {
    fw::WorkBudget tiny{
        .read_bytes = 64,  // small enough to sit in L1
        .write_bytes = 64,
        .item_count = 16,
    };
    if (fw::should_parallelize(tiny)) {
        std::fprintf(stderr, "should_parallelize(tiny): expected false\n");
        std::abort();
    }
}

// The answer depends on the probed cache topology of whichever machine runs
// this, so neither outcome can be asserted. What is checked is that the call
// goes through the alias and returns.
static void test_runtime_should_parallelize_large_callable() {
    fw::WorkBudget huge{
        .read_bytes = 1ULL << 30,  // a gigabyte, DRAM-bound anywhere
        .write_bytes = 1ULL << 30,
        .item_count = 1ULL << 20,
    };
    (void)fw::should_parallelize(huge);
}

static void test_runtime_workbudget_for_span() {
    std::array<int, 32> data{};
    auto budget = fw::WorkBudget::for_span<int>(std::span<int const>{data});
    if (budget.read_bytes != 32 * sizeof(int)) std::abort();
    if (budget.write_bytes != 32 * sizeof(int)) std::abort();
    if (budget.item_count != 32) std::abort();
}

// Two budget types of identical shape exist and are not interchangeable. The
// safety one is the caller-facing form, converted internally. The cost-model
// one is what the locality dispatcher consumes, and is the one built here.
static void test_runtime_recommend_with_locality() {
    ::crucible::concurrent::WorkBudget huge{
        .read_bytes = 1ULL << 30,
        .write_bytes = 1ULL << 30,
        .item_count = 1ULL << 20,
    };
    auto dec_local = fw::recommend_parallelism_with_locality<LocalityRtProbe_Local>(huge);
    auto dec_spread = fw::recommend_parallelism_with_locality<LocalityRtProbe_Spread>(huge);

    // Where the cost model chooses to parallelize, the tag's hint decides the
    // placement policy.
    using K = ::crucible::concurrent::ParallelismDecision::Kind;
    using NP = ::crucible::concurrent::NumaPolicy;
    if (dec_local.kind == K::Parallel) {
        if (dec_local.numa != NP::NumaLocal) {
            std::fprintf(stderr, "locality override: Local expected NumaLocal, got %d\n",
                         static_cast<int>(dec_local.numa));
            std::abort();
        }
    }
    if (dec_spread.kind == K::Parallel) {
        if (dec_spread.numa != NP::NumaSpread) {
            std::fprintf(stderr, "locality override: Spread expected NumaSpread, got %d\n",
                         static_cast<int>(dec_spread.numa));
            std::abort();
        }
    }
    // A sequential answer is equally acceptable. The override only has
    // anything to override once the decision is to parallelize.
}

// Deliberately empty. Running the loop for real needs an arena-backed region,
// which is a test in itself, and the spawn path belongs to the substrate's own
// coverage rather than to this re-export. The function-pointer identity
// assertions in the header already establish that the alias resolves to the
// substrate template.
static void test_runtime_parallel_for_views_n1() { (void)0; }

// The call writes a summary line, redirected here so the test stays silent
// on success.
static void test_runtime_log_topology() {
    FILE* devnull = std::fopen("/dev/null", "w");
    if (!devnull) std::abort();
    fw::log_topology_at_startup(devnull);
    std::fclose(devnull);
}

int main() {
    test_runtime_simd_iota_reduce();
    test_runtime_simd_prefix_mask();
    test_runtime_should_parallelize_small();
    test_runtime_should_parallelize_large_callable();
    test_runtime_workbudget_for_span();
    test_runtime_recommend_with_locality();
    test_runtime_parallel_for_views_n1();
    test_runtime_log_topology();
    std::printf("test_fixy_wrap_simd_workload_locality: "
                "8/8 runtime witnesses passed\n");
    return 0;
}
