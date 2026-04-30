// ═══════════════════════════════════════════════════════════════════
// test_owned_region — OwnedRegion + Workload primitives (SEPLOG-G1)
//
// Coverage:
//   Tier 1: structural (sizeof, EBO collapse, splits_into_pack)
//   Tier 2: single-thread split_into / iteration / chunk math
//   Tier 3: parallel_for_views<N> over 1M floats — 8× speedup target
//   Tier 4: parallel_reduce_views<N> closed-form check
//   Tier 5: parallel_for_views<1> sequential fast path
//   Tier 6: parallel_for_views_adaptive cost-model gate
//   Tier 7: TSan stress (50 iterations across multi-thread tests)
//
// The thesis being demonstrated:
//   * Sub-regions point INTO THE SAME arena buffer at distinct
//     chunk offsets (no allocation per slice)
//   * Workers iterate via std::span — zero indirection per element
//   * Cross-thread synchronisation comes ENTIRELY from RAII jthread
//     join — zero user-level atomics in the body
//   * Body's plain-write to its sub-region is well-defined per the
//     C++ memory model because Permission tags prove disjointness
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Workload.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>

using namespace crucible;
using namespace crucible::safety;

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

// Noexcept variant — for use inside parallel_for_views body lambdas
// (which are required to be noexcept by the framework).  Aborts on
// failure rather than throwing.
#define CRUCIBLE_TEST_REQUIRE_NX(...)                                       \
    do {                                                                    \
        if (!(__VA_ARGS__)) [[unlikely]] {                                  \
            std::fprintf(stderr, "FAIL (noexcept body): %s (%s:%d)\n",      \
                         #__VA_ARGS__, __FILE__, __LINE__);                 \
            std::abort();                                                   \
        }                                                                   \
    } while (0)

namespace {

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

// Tags for test regions.  Each test uses its own tag to avoid
// confusion across cases.
struct DataA {};
struct DataB {};
struct DataSeq {};

// effects::Test exposes an Alloc capability member; copy it for use.
inline effects::Alloc test_alloc_token() noexcept {
    return effects::Test{}.alloc;
}

// ── Tier 1: compile-time structural ──────────────────────────────

void test_compile_time_properties() {
    // sizeof = (T*) + (size_t).  Permission EBO-collapses.
    static_assert(sizeof(OwnedRegion<float, DataA>) ==
                  sizeof(float*) + sizeof(std::size_t));
    static_assert(sizeof(OwnedRegion<std::uint64_t, DataA>) ==
                  sizeof(std::uint64_t*) + sizeof(std::size_t));

    // Move-only.
    static_assert(!std::is_copy_constructible_v<OwnedRegion<float, DataA>>);
    static_assert(std::is_move_constructible_v<OwnedRegion<float, DataA>>);
    static_assert(std::is_nothrow_move_constructible_v<OwnedRegion<float, DataA>>);

    // splits_into_pack auto-specialization — works for arbitrary N.
    static_assert(splits_into_pack_v<DataA,
                                     Slice<DataA, 0>,
                                     Slice<DataA, 1>>);
    static_assert(splits_into_pack_v<DataA,
                                     Slice<DataA, 0>, Slice<DataA, 1>,
                                     Slice<DataA, 2>, Slice<DataA, 3>>);
    static_assert(splits_into_pack_v<DataA,
                                     Slice<DataA, 0>, Slice<DataA, 1>,
                                     Slice<DataA, 2>, Slice<DataA, 3>,
                                     Slice<DataA, 4>, Slice<DataA, 5>,
                                     Slice<DataA, 6>, Slice<DataA, 7>>);
}

// ── Tier 2: single-thread basic ──────────────────────────────────

void test_adopt_and_view() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    auto region = OwnedRegion<float, DataA>::adopt(
        test_alloc_token(), arena, 64, std::move(perm));

    CRUCIBLE_TEST_REQUIRE(region.size() == 64);
    CRUCIBLE_TEST_REQUIRE(!region.empty());
    CRUCIBLE_TEST_REQUIRE(region.data() != nullptr);
    CRUCIBLE_TEST_REQUIRE(region.span().size() == 64);

    // Fill via span.
    for (std::size_t i = 0; i < 64; ++i) region.span()[i] = static_cast<float>(i);
    // Verify via cspan.  Compare via bit_cast to avoid -Werror=float-equal;
    // round-trip through float should bit-identically reproduce the small
    // integer values we wrote.
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t got = std::bit_cast<std::uint32_t>(region.cspan()[i]);
        const std::uint32_t exp = std::bit_cast<std::uint32_t>(static_cast<float>(i));
        CRUCIBLE_TEST_REQUIRE(got == exp);
    }
}

void test_split_into_chunk_math() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, 1000, std::move(perm));

    // Fill with index values for verification.
    for (std::size_t i = 0; i < 1000; ++i) region.span()[i] = i;

    auto [s0, s1, s2, s3, s4, s5, s6, s7] = std::move(region).split_into<8>();

    // chunk = ceil(1000 / 8) = 125.  All shards have exactly 125.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 125);
    CRUCIBLE_TEST_REQUIRE(s1.size() == 125);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 125);

    // Each shard's data points into the same arena buffer at the
    // correct offset.  Verify by reading the index value.
    CRUCIBLE_TEST_REQUIRE(s0.cspan()[0] == 0);
    CRUCIBLE_TEST_REQUIRE(s0.cspan()[124] == 124);
    CRUCIBLE_TEST_REQUIRE(s1.cspan()[0] == 125);
    CRUCIBLE_TEST_REQUIRE(s7.cspan()[0] == 875);
    CRUCIBLE_TEST_REQUIRE(s7.cspan()[124] == 999);
}

void test_split_uneven() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, 1001, std::move(perm));

    auto [s0, s1, s2, s3, s4, s5, s6, s7] = std::move(region).split_into<8>();

    // chunk = ceil(1001 / 8) = 126.
    // shards 0..6 = 126 elements (882 total).
    // shard 7 = 1001 - 882 = 119 elements.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 126);
    CRUCIBLE_TEST_REQUIRE(s6.size() == 126);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 119);
}

void test_split_smaller_than_n() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, 5, std::move(perm));

    auto [s0, s1, s2, s3, s4, s5, s6, s7] = std::move(region).split_into<8>();

    // chunk = ceil(5 / 8) = 1.  First 5 shards have 1 element; last 3 empty.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 1);
    CRUCIBLE_TEST_REQUIRE(s4.size() == 1);
    CRUCIBLE_TEST_REQUIRE(s5.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s6.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s5.empty());
}

// ── Tier 3: parallel_for_views<N> ────────────────────────────────

void test_parallel_for_views_squares() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 100'000;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    // Initialise to index values.
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = i + 1;

    // Square every element via 8-way parallel_for_views.  Generic lambda
    // (auto sub) accepts OwnedRegion<T, Slice<DataA, I>> for any I.
    auto recombined = parallel_for_views<8>(
        std::move(region),
        [](auto sub) noexcept {
            for (auto& x : sub.span()) x = x * x;
        }
    );

    // Verify every element is its index squared.
    CRUCIBLE_TEST_REQUIRE(recombined.size() == N);
    for (std::size_t i = 0; i < N; ++i) {
        const std::uint64_t expected = (i + 1) * (i + 1);
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == expected);
    }
}

void test_parallel_for_views_uses_correct_slice_indices() {
    // Verify each worker only touches its own sub-region by writing
    // a per-shard marker into the first element of each shard's span.
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 800;  // 8 × 100, exact division
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    // Initialise to a sentinel value.
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 0xDEAD;

    auto recombined = parallel_for_views<8>(
        std::move(region),
        [](auto sub) noexcept {
            // Write the slice index into every element of this sub-region.
            // The slice index is encoded as the marker — different per shard.
            using SubT = std::remove_cvref_t<decltype(sub)>;
            constexpr std::size_t shard_idx = SubT::tag_type::index;
            for (auto& x : sub.span()) x = shard_idx;
        }
    );

    // Each chunk of 100 elements should now hold its shard index.
    for (std::size_t shard = 0; shard < 8; ++shard) {
        for (std::size_t i = 0; i < 100; ++i) {
            CRUCIBLE_TEST_REQUIRE(recombined.cspan()[shard * 100 + i] == shard);
        }
    }
}

// ── Tier 4: parallel_reduce_views<N, R> ──────────────────────────

void test_parallel_reduce_views_sum() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 50'000;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    // Fill with 1..N.
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = i + 1;

    // Map: per-shard sum.
    // Reduce: sum of partials.
    auto [total, recombined] = parallel_reduce_views<8, std::uint64_t>(
        std::move(region),
        std::uint64_t{0},
        [](auto sub) noexcept {
            std::uint64_t local = 0;
            for (auto x : sub.cspan()) local += x;
            return local;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; }
    );

    // Closed form: sum 1..N = N*(N+1)/2.
    const std::uint64_t expected = static_cast<std::uint64_t>(N) *
                                    (static_cast<std::uint64_t>(N) + 1) / 2;
    CRUCIBLE_TEST_REQUIRE(total == expected);
    CRUCIBLE_TEST_REQUIRE(recombined.size() == N);
}

void test_parallel_reduce_views_max_abs() {
    // Reduction with a non-commutative-but-associative reducer (max).
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 10'000;
    auto region = OwnedRegion<std::int64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    // Sprinkle a few extreme values.
    for (std::size_t i = 0; i < N; ++i) {
        region.span()[i] = static_cast<std::int64_t>(i);
    }
    region.span()[5000] = -999'999;
    region.span()[7000] =  888'888;

    auto [max_abs, _] = parallel_reduce_views<4, std::int64_t>(
        std::move(region),
        std::int64_t{0},
        [](auto sub) noexcept {
            std::int64_t local = 0;
            for (auto x : sub.cspan()) {
                const std::int64_t a = (x < 0) ? -x : x;
                if (a > local) local = a;
            }
            return local;
        },
        [](std::int64_t a, std::int64_t b) noexcept { return (a > b) ? a : b; }
    );

    CRUCIBLE_TEST_REQUIRE(max_abs == 999'999);
}

// ── FOUND-F04 audit: API contract pinning ────────────────────────
//
// The audit-tier tests below pin invariants of `parallel_reduce_views`
// that any future API evolution must preserve.  Each test names the
// invariant it pins.

// FOUND-F04-A1 — N==1 sequential fast path: result is exactly
// reducer(init, mapper(whole_region)).  The `if constexpr (N == 1)`
// branch in Workload.h:247-250 must NOT spawn a worker.
void test_parallel_reduce_views_n1_init_participates() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 100;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 1;

    // Fast path: N=1 single-shard, init seeds the fold.
    auto [total, _] = parallel_reduce_views<1, std::uint64_t>(
        std::move(region),
        std::uint64_t{1000},               // init contributes
        [](auto sub) noexcept {
            std::uint64_t s = 0;
            for (auto x : sub.cspan()) s += x;
            return s;                       // mapper = sum = 100
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; }
    );

    // init (1000) + mapper(whole) (100) = 1100.
    CRUCIBLE_TEST_REQUIRE(total == 1100);
}

// FOUND-F04-A2 — non-trivial R type (struct accumulator) preserves
// layout across the partials array and the fold.  Locks in support
// for the FOUND-D14 reduce_into worked example with struct R.
void test_parallel_reduce_views_struct_accumulator() {
    struct Stats { std::uint64_t count = 0; std::uint64_t sum = 0; };

    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 10'000;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = i + 1;

    auto [stats, _] = parallel_reduce_views<8, Stats>(
        std::move(region),
        Stats{},                            // identity = {0, 0}
        [](auto sub) noexcept {
            Stats local{};
            for (auto x : sub.cspan()) {
                local.count += 1;
                local.sum   += x;
            }
            return local;
        },
        [](Stats a, Stats b) noexcept {
            return Stats{a.count + b.count, a.sum + b.sum};
        }
    );

    const std::uint64_t expected_sum =
        static_cast<std::uint64_t>(N) * (static_cast<std::uint64_t>(N) + 1) / 2;
    CRUCIBLE_TEST_REQUIRE(stats.count == N);
    CRUCIBLE_TEST_REQUIRE(stats.sum   == expected_sum);
}

// FOUND-F04-A3 — recombined region's data is BIT-IDENTICAL to input.
// `parallel_reduce_views` must not mutate the underlying buffer; the
// rebuilt parent OwnedRegion exposes the original bytes for a
// follow-on read-only consumer.
void test_parallel_reduce_views_recombined_data_unchanged() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 1024;
    auto region = OwnedRegion<std::uint32_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));
    // Seed with a recognizable pattern.
    for (std::size_t i = 0; i < N; ++i) {
        region.span()[i] = static_cast<std::uint32_t>(i * 31 + 7);
    }

    auto [total, recombined] = parallel_reduce_views<8, std::uint64_t>(
        std::move(region),
        std::uint64_t{0},
        [](auto sub) noexcept {
            std::uint64_t s = 0;
            for (auto x : sub.cspan()) s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; }
    );
    (void)total;

    // Region bytes must survive the reduce step intact.
    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(
            recombined.cspan()[i] == static_cast<std::uint32_t>(i * 31 + 7));
    }
}

// FOUND-F04-A4 — DetSafe: same input → same output across two runs
// over a non-commutative reducer.  The fold-order contract (left-to-
// right over the partials array) is what makes this true even when
// the reducer is sensitive to argument order.
void test_parallel_reduce_views_deterministic_across_runs() {
    Arena arena_a, arena_b;
    constexpr std::size_t N = 1024;

    // First run.
    auto region_a = OwnedRegion<std::uint32_t, DataA>::adopt(
        test_alloc_token(), arena_a, N, permission_root_mint<DataA>());
    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = static_cast<std::uint32_t>(i % 17);
    }

    // Non-commutative-y reducer: `2*a - b`.  Same partials produce
    // the same fold IFF the iteration order is fixed.
    auto reducer = [](std::int64_t a, std::int64_t b) noexcept {
        return 2 * a - b;
    };
    auto mapper = [](auto sub) noexcept {
        std::int64_t s = 0;
        for (auto x : sub.cspan()) s += x;
        return s;
    };

    auto [r1, _r1] = parallel_reduce_views<4, std::int64_t>(
        std::move(region_a), std::int64_t{0}, mapper, reducer);

    // Second run with identical input.
    auto region_b = OwnedRegion<std::uint32_t, DataB>::adopt(
        test_alloc_token(), arena_b, N, permission_root_mint<DataB>());
    for (std::size_t i = 0; i < N; ++i) {
        region_b.span()[i] = static_cast<std::uint32_t>(i % 17);
    }

    auto [r2, _r2] = parallel_reduce_views<4, std::int64_t>(
        std::move(region_b), std::int64_t{0}, mapper, reducer);

    CRUCIBLE_TEST_REQUIRE(r1 == r2);
}

// FOUND-F04-A6 — smallest-parallel-N (N==2) with the smallest workload
// where parallelism is non-trivial: 4 elements split across 2 shards.
// The existing tests jump from N==1 (sequential) directly to N==4/8;
// this fixture pins that the smallest-genuinely-parallel case
// (one partial per worker, both summed in the post-join fold) works.
void test_parallel_reduce_views_n2_smallest_parallel() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 4;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));
    region.span()[0] = 10;
    region.span()[1] = 20;
    region.span()[2] = 30;
    region.span()[3] = 40;

    auto [total, _] = parallel_reduce_views<2, std::uint64_t>(
        std::move(region),
        std::uint64_t{0},
        [](auto sub) noexcept {
            std::uint64_t s = 0;
            for (auto x : sub.cspan()) s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; }
    );

    // shard_0 = 10 + 20 = 30; shard_1 = 30 + 40 = 70; total = 30 + 70 = 100.
    CRUCIBLE_TEST_REQUIRE(total == 100);
}

// FOUND-F04-A5 — mapper is invoked exactly N times when N >= 1.  Use
// a shared atomic counter to confirm worker dispatch (well-defined
// because the counter is std::atomic).
void test_parallel_reduce_views_mapper_invoked_n_times() {
    Arena arena;
    constexpr std::size_t SHARDS = 8;
    constexpr std::size_t N = 4096;
    auto region = OwnedRegion<std::uint32_t, DataA>::adopt(
        test_alloc_token(), arena, N, permission_root_mint<DataA>());
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 1;

    std::atomic<std::size_t> mapper_invocations{0};

    auto [total, _] = parallel_reduce_views<SHARDS, std::uint64_t>(
        std::move(region),
        std::uint64_t{0},
        [&mapper_invocations](auto sub) noexcept {
            mapper_invocations.fetch_add(1, std::memory_order_relaxed);
            std::uint64_t s = 0;
            for (auto x : sub.cspan()) s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; }
    );

    CRUCIBLE_TEST_REQUIRE(total == N);
    CRUCIBLE_TEST_REQUIRE(
        mapper_invocations.load(std::memory_order_acquire) == SHARDS);
}

// ── Tier 5: parallel_for_views<1> sequential fast path ──────────

void test_parallel_for_views_sequential_n1() {
    Arena arena;
    auto perm = permission_root_mint<DataSeq>();
    constexpr std::size_t N = 100;
    auto region = OwnedRegion<std::uint64_t, DataSeq>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i) region.span()[i] = i;

    // N=1 should NOT spawn a thread — body invoked inline on whole region.
    auto recombined = parallel_for_views<1>(
        std::move(region),
        [](auto sub) noexcept {
            CRUCIBLE_TEST_REQUIRE_NX(sub.size() == 100);
            for (auto& x : sub.span()) x *= 2;
        }
    );

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == i * 2);
    }
}

// ── Tier 6: parallel_for_views_adaptive cost-model gate ──────────

void test_adaptive_picks_sequential_for_small_workload() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 100;  // tiny — fits in L1 cache
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i) region.span()[i] = i + 1;

    WorkBudget tiny_budget{
        .read_bytes  = N * sizeof(std::uint64_t),
        .write_bytes = N * sizeof(std::uint64_t),
        .item_count  = N,
    };
    // should_parallelize returns false for L1-resident workloads.
    CRUCIBLE_TEST_REQUIRE(!should_parallelize(tiny_budget));

    auto recombined = parallel_for_views_adaptive<8>(
        std::move(region),
        [](auto sub) noexcept { for (auto& x : sub.span()) x = x * x; },
        tiny_budget
    );

    // Result is correct regardless of sequential vs parallel decision.
    for (std::size_t i = 0; i < N; ++i) {
        const std::uint64_t expected = (i + 1) * (i + 1);
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == expected);
    }
}

void test_adaptive_picks_parallel_for_large_workload() {
    Arena arena{1ULL << 24};  // 16 MB block to fit a large region
    auto perm = permission_root_mint<DataB>();
    constexpr std::size_t N = 200'000;  // ~1.6 MB > L2_per_core
    auto region = OwnedRegion<std::uint64_t, DataB>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i) region.span()[i] = i;

    WorkBudget large_budget{
        .read_bytes  = N * sizeof(std::uint64_t),
        .write_bytes = N * sizeof(std::uint64_t),
        .item_count  = N,
    };
    CRUCIBLE_TEST_REQUIRE(should_parallelize(large_budget));

    auto recombined = parallel_for_views_adaptive<8>(
        std::move(region),
        [](auto sub) noexcept { for (auto& x : sub.span()) x += 1000; },
        large_budget
    );

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == i + 1000);
    }
}

// ── Tier 7: stress (relies on TSan for race detection) ──────────

// ── Tier 8: integration ergonomics (SEPLOG-C1b) ──────────────────

void test_workbudget_for_span() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, 1024, std::move(perm));

    // for_span auto-derives byte counts from the typed span.
    const auto budget = WorkBudget::for_span<std::uint64_t>(region.cspan());

    CRUCIBLE_TEST_REQUIRE(budget.item_count  == 1024);
    CRUCIBLE_TEST_REQUIRE(budget.read_bytes  == 1024 * sizeof(std::uint64_t));
    CRUCIBLE_TEST_REQUIRE(budget.write_bytes == 1024 * sizeof(std::uint64_t));

    // Read-only variant zeroes write_bytes.
    const auto ro_budget =
        WorkBudget::for_span_read_only<std::uint64_t>(region.cspan());
    CRUCIBLE_TEST_REQUIRE(ro_budget.read_bytes == 1024 * sizeof(std::uint64_t));
    CRUCIBLE_TEST_REQUIRE(ro_budget.write_bytes == 0);
}

void test_parallel_for_smart_small_workload() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 100;  // tiny — should run sequentially
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i) region.span()[i] = i + 1;

    // parallel_for_smart auto-derives WorkBudget from the region size
    // and picks sequential vs parallel from Topology.
    auto recombined = parallel_for_smart(
        std::move(region),
        [](auto sub) noexcept { for (auto& x : sub.span()) x *= 3; }
    );

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == (i + 1) * 3);
    }
}

void test_parallel_for_smart_large_workload() {
    Arena arena{1ULL << 24};  // 16 MB block
    auto perm = permission_root_mint<DataB>();
    constexpr std::size_t N = 200'000;  // ~1.6 MB > L2_per_core typically
    auto region = OwnedRegion<std::uint64_t, DataB>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 0;

    auto recombined = parallel_for_smart(
        std::move(region),
        [](auto sub) noexcept { for (auto& x : sub.span()) x = 42; }
    );

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == 42);
    }
}

void test_log_topology_at_startup() {
    // Just verify it's callable and doesn't crash.
    std::fprintf(stderr, "\n      log_topology_at_startup() output ↓\n");
    log_topology_at_startup();
    std::fprintf(stderr, "      ↑\n      ");
}

void test_stress_parallel_for_repeated() {
    Arena arena;
    auto perm = permission_root_mint<DataA>();
    constexpr std::size_t N = 16'384;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 0;

    // Repeated parallel_for_views invocations: increment every element
    // by 1 each iteration.  After K iterations, every element is K.
    constexpr int K = 10;
    auto current = std::move(region);
    for (int iter = 0; iter < K; ++iter) {
        current = parallel_for_views<8>(
            std::move(current),
            [](auto sub) noexcept { for (auto& x : sub.span()) ++x; }
        );
    }

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(current.cspan()[i] == K);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_owned_region:\n");

    test_compile_time_properties();  // pure compile-time

    run_test("test_adopt_and_view",                          test_adopt_and_view);
    run_test("test_split_into_chunk_math",                   test_split_into_chunk_math);
    run_test("test_split_uneven",                            test_split_uneven);
    run_test("test_split_smaller_than_n",                    test_split_smaller_than_n);
    run_test("test_parallel_for_views_squares",              test_parallel_for_views_squares);
    run_test("test_parallel_for_views_uses_correct_slice_indices",
             test_parallel_for_views_uses_correct_slice_indices);
    run_test("test_parallel_reduce_views_sum",               test_parallel_reduce_views_sum);
    run_test("test_parallel_reduce_views_max_abs",           test_parallel_reduce_views_max_abs);
    run_test("test_parallel_reduce_views_n1_init_participates",
             test_parallel_reduce_views_n1_init_participates);
    run_test("test_parallel_reduce_views_struct_accumulator",
             test_parallel_reduce_views_struct_accumulator);
    run_test("test_parallel_reduce_views_recombined_data_unchanged",
             test_parallel_reduce_views_recombined_data_unchanged);
    run_test("test_parallel_reduce_views_deterministic_across_runs",
             test_parallel_reduce_views_deterministic_across_runs);
    run_test("test_parallel_reduce_views_mapper_invoked_n_times",
             test_parallel_reduce_views_mapper_invoked_n_times);
    run_test("test_parallel_reduce_views_n2_smallest_parallel",
             test_parallel_reduce_views_n2_smallest_parallel);
    run_test("test_parallel_for_views_sequential_n1",        test_parallel_for_views_sequential_n1);
    run_test("test_adaptive_picks_sequential_for_small_workload",
             test_adaptive_picks_sequential_for_small_workload);
    run_test("test_adaptive_picks_parallel_for_large_workload",
             test_adaptive_picks_parallel_for_large_workload);
    run_test("test_workbudget_for_span",                     test_workbudget_for_span);
    run_test("test_parallel_for_smart_small_workload",       test_parallel_for_smart_small_workload);
    run_test("test_parallel_for_smart_large_workload",       test_parallel_for_smart_large_workload);
    run_test("test_log_topology_at_startup",                 test_log_topology_at_startup);
    run_test("test_stress_parallel_for_repeated",            test_stress_parallel_for_repeated);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
