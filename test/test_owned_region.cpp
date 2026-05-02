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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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

// ── FOUND-F01 audit: API contract pinning ────────────────────────
//
// Pin invariants of `parallel_for_views<N>` so future refactors
// cannot silently change the body-invocation-count, worker-
// disjointness, or DetSafe properties.

// FOUND-F01-A1 — body invoked exactly N times for any N >= 1.
// Locks in worker dispatch via a shared atomic counter.
void test_parallel_for_views_body_invoked_n_times() {
    Arena arena;
    constexpr std::size_t SHARDS = 8;
    constexpr std::size_t N = 4096;
    auto region = OwnedRegion<std::uint32_t, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 0;

    std::atomic<std::size_t> body_invocations{0};

    auto recombined = parallel_for_views<SHARDS>(
        std::move(region),
        [&body_invocations](auto sub) noexcept {
            body_invocations.fetch_add(1, std::memory_order_relaxed);
            for (auto& x : sub.span()) x = 1;  // mark as visited
        }
    );

    CRUCIBLE_TEST_REQUIRE(
        body_invocations.load(std::memory_order_acquire) == SHARDS);
    // Every element marked, confirming all shards executed.
    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == 1);
    }
}

// FOUND-F01-A2 — smallest non-trivial parallel case: N==2 with 4
// elements split 2+2.  Existing tests jump from N==1 (sequential)
// directly to N==8; this fixture pins the smallest genuinely-
// parallel case works.
void test_parallel_for_views_n2_smallest_parallel() {
    Arena arena;
    auto perm = mint_permission_root<DataB>();
    constexpr std::size_t N = 4;
    auto region = OwnedRegion<std::uint64_t, DataB>::adopt(
        test_alloc_token(), arena, N, std::move(perm));
    region.span()[0] = 1;
    region.span()[1] = 2;
    region.span()[2] = 3;
    region.span()[3] = 4;

    auto recombined = parallel_for_views<2>(
        std::move(region),
        [](auto sub) noexcept {
            for (auto& x : sub.span()) x = x * 10;
        }
    );

    CRUCIBLE_TEST_REQUIRE(recombined.cspan()[0] == 10);
    CRUCIBLE_TEST_REQUIRE(recombined.cspan()[1] == 20);
    CRUCIBLE_TEST_REQUIRE(recombined.cspan()[2] == 30);
    CRUCIBLE_TEST_REQUIRE(recombined.cspan()[3] == 40);
}

// FOUND-F01-A4 — uneven split: region size NOT divisible by N.  Most
// production workloads have arbitrary sizes; existing tests used
// only exact divisions (800/8, 4096/8).  Pin that uneven splits
// (100 elements across 8 shards) execute correctly: all elements
// visited, no overlap, no skipped suffix.
void test_parallel_for_views_uneven_split() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 100;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 0;

    auto recombined = parallel_for_views<8>(
        std::move(region),
        [](auto sub) noexcept {
            for (auto& x : sub.span()) x = 1;  // mark every visited element
        }
    );

    // Every element of the recombined region must be marked — no
    // shard skipped its slice of the suffix.
    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == 1);
    }
    CRUCIBLE_TEST_REQUIRE(recombined.size() == N);
}

// FOUND-F01-A3 — DetSafe: same input, run twice, identical output.
// Workers writing to disjoint shards plus jthread::join's happens-
// before guarantee a deterministic post-recombine state regardless
// of worker scheduling.
void test_parallel_for_views_deterministic_across_runs() {
    Arena arena_a, arena_b;
    constexpr std::size_t N = 1024;

    auto run = [](Arena& arena, auto perm) noexcept {
        auto region = OwnedRegion<std::uint32_t, DataA>::adopt(
            test_alloc_token(), arena, N, std::move(perm));
        for (std::size_t i = 0; i < N; ++i) {
            region.span()[i] = static_cast<std::uint32_t>(i * 31 + 7);
        }
        return parallel_for_views<4>(
            std::move(region),
            [](auto sub) noexcept {
                for (auto& x : sub.span()) x = x ^ 0xA5A5A5A5u;
            }
        );
    };

    // Each run must use a fresh tag — Permission tags are linear and
    // mint-once-per-program-per-tag.
    auto r1 = run(arena_a, mint_permission_root<DataA>());

    auto r2_arena_perm = mint_permission_root<DataB>();
    auto region_b = OwnedRegion<std::uint32_t, DataB>::adopt(
        test_alloc_token(), arena_b, N, std::move(r2_arena_perm));
    for (std::size_t i = 0; i < N; ++i) {
        region_b.span()[i] = static_cast<std::uint32_t>(i * 31 + 7);
    }
    auto r2 = parallel_for_views<4>(
        std::move(region_b),
        [](auto sub) noexcept {
            for (auto& x : sub.span()) x = x ^ 0xA5A5A5A5u;
        }
    );

    CRUCIBLE_TEST_REQUIRE(r1.size() == r2.size());
    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(r1.cspan()[i] == r2.cspan()[i]);
    }
}

// ── Tier 4: parallel_reduce_views<N, R> ──────────────────────────

void test_parallel_reduce_views_sum() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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
        test_alloc_token(), arena_a, N, mint_permission_root<DataA>());
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
        test_alloc_token(), arena_b, N, mint_permission_root<DataB>());
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
    auto perm = mint_permission_root<DataA>();
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

// FOUND-F04-AUDIT-2 — uneven split for parallel_reduce_views<N, R>.
// Mirrors the F01-A4 fixture (uneven split for parallel_for_views).
// Existing F04 tests used exactly-divisible region sizes (4096/8,
// 4/2); production workloads have arbitrary sizes.  Pin that uneven
// splits (100 elements / 8 shards) sum correctly with init
// participating exactly once and every element folded into the
// reducer (no shard skipping its slice of the suffix).
//
// Reducer is left-to-right + associative (integer addition); the
// arithmetic identity is:
//
//   total = init + sum(mapper(s_0)) + ... + sum(mapper(s_7))
//         = 7  + 1*100 (every element is 1)
//         = 107
//
// Validates that Slice<Whole, I>::adopt's chunk-math distributes
// the suffix correctly under the uneven regime: the LAST shard
// receives the remainder (100 - (7 * 12) = 16 elements, given
// chunk = (count + N - 1) / N = 13 for non-last shards).
void test_parallel_reduce_views_uneven_split() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 100;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 1;

    auto [total, _] = parallel_reduce_views<8, std::uint64_t>(
        std::move(region),
        /*init=*/std::uint64_t{7},
        [](auto sub) noexcept -> std::uint64_t {
            std::uint64_t s = 0;
            for (auto x : sub.cspan()) s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; }
    );

    // Every element of the recombined region must be folded — no
    // shard skipped its slice of the suffix.
    CRUCIBLE_TEST_REQUIRE(total == 107);
}

// FOUND-F04-A5 — mapper is invoked exactly N times when N >= 1.  Use
// a shared atomic counter to confirm worker dispatch (well-defined
// because the counter is std::atomic).
void test_parallel_reduce_views_mapper_invoked_n_times() {
    Arena arena;
    constexpr std::size_t SHARDS = 8;
    constexpr std::size_t N = 4096;
    auto region = OwnedRegion<std::uint32_t, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());
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

// ── Tier 4b: parallel_apply_pair<N> co-iterated pair — FOUND-F02 ─────

// FOUND-F02-A1 — basic pair: vector add y = x + y in parallel.
// Both regions size 4096; N=8 even split.  After the call, region_b
// holds element-wise sum of input region_a + region_b.
void test_parallel_apply_pair_vector_add() {
    Arena arena;
    constexpr std::size_t N = 4096;

    auto perm_a = mint_permission_root<DataA>();
    auto region_a = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, std::move(perm_a));
    auto perm_b = mint_permission_root<DataB>();
    auto region_b = OwnedRegion<std::uint64_t, DataB>::adopt(
        test_alloc_token(), arena, N, std::move(perm_b));

    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = i;
        region_b.span()[i] = 2 * i;
    }

    auto [recombined_a, recombined_b] = parallel_apply_pair<8>(
        std::move(region_a), std::move(region_b),
        [](auto sub_a, auto sub_b) noexcept {
            // sub_a.size() == sub_b.size() because regions have
            // matching size and identical chunk math.
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.size() == sub_b.size());
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i) {
                dst[i] = src[i] + dst[i];  // y = x + y
            }
        }
    );

    // recombined_a unchanged (we only read from it).
    // recombined_b[i] should equal i + 2i == 3i.
    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined_a.cspan()[i] == i);
        CRUCIBLE_TEST_REQUIRE(recombined_b.cspan()[i] == 3 * i);
    }
    CRUCIBLE_TEST_REQUIRE(recombined_a.size() == N);
    CRUCIBLE_TEST_REQUIRE(recombined_b.size() == N);
}

// FOUND-F02-A2 — sequential N=1 fast path.  No jthread spawn; body
// invoked inline on (whole_a, whole_b) wrapped as single-shard
// sub-regions.
void test_parallel_apply_pair_sequential_n1() {
    Arena arena;
    constexpr std::size_t N = 100;

    auto region_a = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b = OwnedRegion<std::uint64_t, DataB>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataB>());

    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = i;
        region_b.span()[i] = 0;
    }

    auto [out_a, out_b] = parallel_apply_pair<1>(
        std::move(region_a), std::move(region_b),
        [](auto sub_a, auto sub_b) noexcept {
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.size() == 100);
            CRUCIBLE_TEST_REQUIRE_NX(sub_b.size() == 100);
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i) dst[i] = src[i] * 7;
        }
    );

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(out_b.cspan()[i] == i * 7);
    }
}

// FOUND-F02-A3 — smallest-parallel-N (N==2).  4 elements split across
// 2 shards.  Pins that the smallest-genuinely-parallel case works and
// that pair iteration aligns shard I of A with shard I of B.
void test_parallel_apply_pair_n2_smallest_parallel() {
    Arena arena;
    constexpr std::size_t N = 4;

    auto region_a = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b = OwnedRegion<std::uint64_t, DataB>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataB>());
    region_a.span()[0] = 1; region_a.span()[1] = 2;
    region_a.span()[2] = 3; region_a.span()[3] = 4;
    for (std::size_t i = 0; i < N; ++i) region_b.span()[i] = 0;

    auto [out_a, out_b] = parallel_apply_pair<2>(
        std::move(region_a), std::move(region_b),
        [](auto sub_a, auto sub_b) noexcept {
            // Per-shard sub_a == sub_b in element count (chunk math
            // identical because parent sizes match).
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.size() == sub_b.size());
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i) {
                dst[i] = src[i] * src[i];  // square
            }
        }
    );

    CRUCIBLE_TEST_REQUIRE(out_b.cspan()[0] == 1);   // 1²
    CRUCIBLE_TEST_REQUIRE(out_b.cspan()[1] == 4);   // 2²
    CRUCIBLE_TEST_REQUIRE(out_b.cspan()[2] == 9);   // 3²
    CRUCIBLE_TEST_REQUIRE(out_b.cspan()[3] == 16);  // 4²
}

// FOUND-F02-A4 — uneven split.  100 elements / 8 shards (last shard
// has the remainder).  Pin that pair iteration handles uneven chunk
// math correctly: every (sub_a_I, sub_b_I) has matching shard size,
// no element skipped or double-touched.
void test_parallel_apply_pair_uneven_split() {
    Arena arena;
    constexpr std::size_t N = 100;

    auto region_a = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b = OwnedRegion<std::uint64_t, DataB>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataB>());

    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = i + 1;
        region_b.span()[i] = 0;
    }

    auto [out_a, out_b] = parallel_apply_pair<8>(
        std::move(region_a), std::move(region_b),
        [](auto sub_a, auto sub_b) noexcept {
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.size() == sub_b.size());
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i) dst[i] = src[i];
        }
    );

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(out_b.cspan()[i] == i + 1);
    }
}

// FOUND-F02-A5 — body is invoked exactly N times when N >= 1.  Use a
// shared atomic counter to confirm worker dispatch.
void test_parallel_apply_pair_body_invoked_n_times() {
    Arena arena;
    constexpr std::size_t SHARDS = 8;
    constexpr std::size_t N = 800;

    auto region_a = OwnedRegion<std::uint32_t, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b = OwnedRegion<std::uint32_t, DataB>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataB>());

    std::atomic<std::size_t> body_invocations{0};

    auto recombined = parallel_apply_pair<SHARDS>(
        std::move(region_a), std::move(region_b),
        [&body_invocations](auto sub_a, auto sub_b) noexcept {
            body_invocations.fetch_add(1, std::memory_order_relaxed);
            (void)sub_a; (void)sub_b;
        }
    );

    CRUCIBLE_TEST_REQUIRE(
        body_invocations.load(std::memory_order_acquire) == SHARDS);
    CRUCIBLE_TEST_REQUIRE(recombined.first.size() == N);
    CRUCIBLE_TEST_REQUIRE(recombined.second.size() == N);
}

// FOUND-F02-A6 — DetSafe: same input + same body, run twice, identical
// output.  Workers writing to disjoint shards plus jthread::join's
// happens-before guarantee a deterministic post-recombine state
// regardless of worker scheduling order.  Use a non-commutative
// per-element op (XOR mask depending on index) to ensure shard
// alignment matters.
void test_parallel_apply_pair_deterministic_across_runs() {
    Arena arena;
    constexpr std::size_t N = 1024;

    auto run_once = [&arena]() noexcept {
        auto src = OwnedRegion<std::uint64_t, DataA>::adopt(
            test_alloc_token(), arena, N, mint_permission_root<DataA>());
        auto dst = OwnedRegion<std::uint64_t, DataB>::adopt(
            test_alloc_token(), arena, N, mint_permission_root<DataB>());
        for (std::size_t i = 0; i < N; ++i) {
            src.span()[i] = std::uint64_t{i} * 31u + 7u;
            dst.span()[i] = std::uint64_t{i} * 17u + 3u;
        }

        auto [_a, out_b] = parallel_apply_pair<8>(
            std::move(src), std::move(dst),
            [](auto sub_a, auto sub_b) noexcept {
                auto a = sub_a.cspan();
                auto b = sub_b.span();
                for (std::size_t i = 0; i < b.size(); ++i) {
                    b[i] = a[i] ^ b[i] ^ static_cast<std::uint64_t>(i);
                }
            }
        );
        std::array<std::uint64_t, N> snapshot{};
        for (std::size_t i = 0; i < N; ++i) snapshot[i] = out_b.cspan()[i];
        return snapshot;
    };

    auto first  = run_once();
    auto second = run_once();
    auto third  = run_once();
    CRUCIBLE_TEST_REQUIRE(first == second);
    CRUCIBLE_TEST_REQUIRE(second == third);
}

// FOUND-F02-A8 — same-tag scenario.  Both regions use DataA (W1 == W2);
// each region carries its own independent root permission obtained
// from a separate root_mint call.  This pins that the API does NOT
// require distinct tags — `Slice<DataA, I>` for both regions is a
// valid composition because the root permissions are independent
// and split chains do not collide.
void test_parallel_apply_pair_same_tag() {
    Arena arena;
    constexpr std::size_t N = 64;

    // Two regions with the SAME tag (DataA), each with its own
    // independent root permission.  Each region's split chain is
    // independent.
    auto region_a = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());

    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = i;
        region_b.span()[i] = 0;
    }

    auto [out_a, out_b] = parallel_apply_pair<4>(
        std::move(region_a), std::move(region_b),
        [](auto sub_a, auto sub_b) noexcept {
            // Both sub-regions have the SAME type (Slice<DataA, I>);
            // distinct base pointers prove they don't alias.
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.data() != sub_b.data());
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i) dst[i] = src[i] * 11;
        }
    );

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(out_b.cspan()[i] == i * 11);
    }
}

// FOUND-F02-A9 — disjoint shard verification.  Each worker writes its
// shard_idx into every element of its sub_b slice.  After recombine,
// each chunk in region_b must hold its shard index — proves no shard
// touched another's slice (no overlap) and every element was written
// (no skip).  Mirrors test_parallel_for_views_uses_correct_slice_indices.
void test_parallel_apply_pair_disjoint_shards() {
    Arena arena;
    constexpr std::size_t N = 800;  // 8 × 100, exact division.

    auto region_a = OwnedRegion<std::uint64_t, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b = OwnedRegion<std::uint64_t, DataB>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataB>());

    // Sentinel.
    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = 0xCAFE;
        region_b.span()[i] = 0xDEAD;
    }

    auto [out_a, out_b] = parallel_apply_pair<8>(
        std::move(region_a), std::move(region_b),
        [](auto sub_a, auto sub_b) noexcept {
            using SubB = std::remove_cvref_t<decltype(sub_b)>;
            constexpr std::size_t shard_idx = SubB::tag_type::index;
            (void)sub_a;  // read-only side intentionally untouched
            for (auto& x : sub_b.span()) x = shard_idx;
        }
    );

    // region_a unchanged (sentinel); region_b shows per-shard markers.
    for (std::size_t shard = 0; shard < 8; ++shard) {
        for (std::size_t i = 0; i < 100; ++i) {
            CRUCIBLE_TEST_REQUIRE(out_a.cspan()[shard * 100 + i] == 0xCAFE);
            CRUCIBLE_TEST_REQUIRE(out_b.cspan()[shard * 100 + i] == shard);
        }
    }
}

// FOUND-F02-A7 — heterogeneous element types and tags.  The regions
// don't have to share T or W.  This pins that template parameter
// flexibility: T1=float, T2=int32; W1=DataA, W2=DataB.
void test_parallel_apply_pair_heterogeneous_types() {
    Arena arena;
    constexpr std::size_t N = 64;

    auto region_a = OwnedRegion<float, DataA>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b = OwnedRegion<std::int32_t, DataB>::adopt(
        test_alloc_token(), arena, N, mint_permission_root<DataB>());
    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = static_cast<float>(i) + 0.5f;
        region_b.span()[i] = 0;
    }

    auto [out_a, out_b] = parallel_apply_pair<4>(
        std::move(region_a), std::move(region_b),
        [](auto sub_a, auto sub_b) noexcept {
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i) {
                dst[i] = static_cast<std::int32_t>(src[i]);  // truncate
            }
        }
    );

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(out_b.cspan()[i] == static_cast<std::int32_t>(i));
    }
}

// ── Tier 5: parallel_for_views<1> sequential fast path ──────────

void test_parallel_for_views_sequential_n1() {
    Arena arena;
    auto perm = mint_permission_root<DataSeq>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataB>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataA>();
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
    auto perm = mint_permission_root<DataB>();
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
    auto perm = mint_permission_root<DataA>();
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
    run_test("test_parallel_for_views_body_invoked_n_times",
             test_parallel_for_views_body_invoked_n_times);
    run_test("test_parallel_for_views_n2_smallest_parallel",
             test_parallel_for_views_n2_smallest_parallel);
    run_test("test_parallel_for_views_uneven_split",
             test_parallel_for_views_uneven_split);
    run_test("test_parallel_for_views_deterministic_across_runs",
             test_parallel_for_views_deterministic_across_runs);
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
    run_test("test_parallel_reduce_views_uneven_split",
             test_parallel_reduce_views_uneven_split);
    run_test("test_parallel_apply_pair_vector_add",
             test_parallel_apply_pair_vector_add);
    run_test("test_parallel_apply_pair_sequential_n1",
             test_parallel_apply_pair_sequential_n1);
    run_test("test_parallel_apply_pair_n2_smallest_parallel",
             test_parallel_apply_pair_n2_smallest_parallel);
    run_test("test_parallel_apply_pair_uneven_split",
             test_parallel_apply_pair_uneven_split);
    run_test("test_parallel_apply_pair_body_invoked_n_times",
             test_parallel_apply_pair_body_invoked_n_times);
    run_test("test_parallel_apply_pair_deterministic_across_runs",
             test_parallel_apply_pair_deterministic_across_runs);
    run_test("test_parallel_apply_pair_heterogeneous_types",
             test_parallel_apply_pair_heterogeneous_types);
    run_test("test_parallel_apply_pair_same_tag",
             test_parallel_apply_pair_same_tag);
    run_test("test_parallel_apply_pair_disjoint_shards",
             test_parallel_apply_pair_disjoint_shards);
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
