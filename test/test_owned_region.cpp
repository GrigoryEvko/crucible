// The claim these tests exist to support: several threads may write to
// one buffer, with no atomics and no locks in the worker bodies, and the
// result is still well defined.
//
// That holds because a split hands each worker a sub-region over a
// distinct range of the same buffer, the permission tags prove those
// ranges are disjoint, and the only synchronization is the join at the
// end of the fork.  So the tests below are mostly about disjointness and
// about the chunk arithmetic that produces it: every element written
// exactly once, nothing skipped, nothing touched twice.

#include <crucible/Arena.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/safety/_OwnedRegion.h>
#include <crucible/safety/Workload.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>

using namespace crucible;
using namespace crucible::safety;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

// Worker bodies are required to be noexcept, so a check inside one
// aborts rather than throwing.
#define CRUCIBLE_TEST_REQUIRE_NX(...)                                                                     \
    do {                                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                                \
            std::fprintf(stderr, "FAIL (noexcept body): %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            std::abort();                                                                                 \
        }                                                                                                 \
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

struct DataA {};
struct DataB {};
struct DataSeq {};

inline effects::Alloc test_alloc_token() noexcept { return effects::testing::test().alloc; }

void test_compile_time_properties() {
    // A region is a pointer and a count.  The permission token costs
    // nothing because it has no state to store.
    static_assert(sizeof(OwnedRegion<float, DataA>) == sizeof(float*) + sizeof(std::size_t));
    static_assert(sizeof(OwnedRegion<std::uint64_t, DataA>) == sizeof(std::uint64_t*) + sizeof(std::size_t));

    static_assert(!std::is_copy_constructible_v<OwnedRegion<float, DataA>>);
    static_assert(std::is_move_constructible_v<OwnedRegion<float, DataA>>);
    static_assert(std::is_nothrow_move_constructible_v<OwnedRegion<float, DataA>>);

    // The split relation is generated rather than written out, so these
    // three widths stand in for any N.
    static_assert(splits_into_pack_v<DataA, Slice<DataA, 0>, Slice<DataA, 1>>);
    static_assert(splits_into_pack_v<DataA, Slice<DataA, 0>, Slice<DataA, 1>, Slice<DataA, 2>, Slice<DataA, 3>>);
    static_assert(splits_into_pack_v<DataA, Slice<DataA, 0>, Slice<DataA, 1>, Slice<DataA, 2>, Slice<DataA, 3>,
                                     Slice<DataA, 4>, Slice<DataA, 5>, Slice<DataA, 6>, Slice<DataA, 7>>);
}

void test_adopt_and_view() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    auto region = OwnedRegion<float, DataA>::adopt(test_alloc_token(), arena, 64, std::move(perm));

    CRUCIBLE_TEST_REQUIRE(region.size() == 64);
    CRUCIBLE_TEST_REQUIRE(!region.empty());
    CRUCIBLE_TEST_REQUIRE(region.data() != nullptr);
    CRUCIBLE_TEST_REQUIRE(region.span().size() == 64);

    for (std::size_t i = 0; i < 64; ++i)
        region.span()[i] = static_cast<float>(i);
    // Compared as bits rather than as floats, because -Werror=float-equal
    // forbids the direct comparison.  These small integers survive the
    // round trip through float exactly, so the bit compare is sound.
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t got = std::bit_cast<std::uint32_t>(region.cspan()[i]);
        const std::uint32_t exp = std::bit_cast<std::uint32_t>(static_cast<float>(i));
        CRUCIBLE_TEST_REQUIRE(got == exp);
    }
}

void test_split_into_chunk_math() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, 1000, std::move(perm));

    // Each element holds its own index, so a shard's contents identify
    // the offset it was cut from.
    for (std::size_t i = 0; i < 1000; ++i)
        region.span()[i] = i;

    auto [s0, s1, s2, s3, s4, s5, s6, s7] = std::move(region).split_into<8>();

    // 1000 over 8 divides exactly, so every shard is the same size.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 125);
    CRUCIBLE_TEST_REQUIRE(s1.size() == 125);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 125);

    // The shards are views into one buffer, not copies, which is what
    // reading the seeded indices back at the right offsets shows.
    CRUCIBLE_TEST_REQUIRE(s0.cspan()[0] == 0);
    CRUCIBLE_TEST_REQUIRE(s0.cspan()[124] == 124);
    CRUCIBLE_TEST_REQUIRE(s1.cspan()[0] == 125);
    CRUCIBLE_TEST_REQUIRE(s7.cspan()[0] == 875);
    CRUCIBLE_TEST_REQUIRE(s7.cspan()[124] == 999);
}

void test_split_uneven() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, 1001, std::move(perm));

    auto [s0, s1, s2, s3, s4, s5, s6, s7] = std::move(region).split_into<8>();

    // The chunk size rounds up, so the leading shards take 126 each and
    // the last one takes whatever is left, which is fewer.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 126);
    CRUCIBLE_TEST_REQUIRE(s6.size() == 126);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 119);
}

void test_split_smaller_than_n() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, 5, std::move(perm));

    auto [s0, s1, s2, s3, s4, s5, s6, s7] = std::move(region).split_into<8>();

    // With fewer elements than shards the rounded-up chunk is one, so
    // the shards past the end are empty rather than out of range.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 1);
    CRUCIBLE_TEST_REQUIRE(s4.size() == 1);
    CRUCIBLE_TEST_REQUIRE(s5.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s6.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s5.empty());
}

void test_parallel_for_views_squares() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 100000;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = i + 1;

    // The body has to be a generic lambda, because each shard is a
    // different type carrying its own slice index.
    auto recombined = parallel_for_views<8>(std::move(region), [](auto sub) noexcept {
        for (auto& x : sub.span())
            x = x * x;
    });

    CRUCIBLE_TEST_REQUIRE(recombined.size() == N);
    for (std::size_t i = 0; i < N; ++i) {
        const std::uint64_t expected = (i + 1) * (i + 1);
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == expected);
    }
}

// Each worker stamps its own slice index over its whole shard, so the
// post-join scan shows both that no shard wrote outside its range and
// that no element went unwritten.
void test_parallel_for_views_uses_correct_slice_indices() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 800;  // 8 × 100, exact division
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));

    // A value no shard index can produce, so an untouched element is
    // distinguishable from a written one.
    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 0xDEAD;

    auto recombined = parallel_for_views<8>(std::move(region), [](auto sub) noexcept {
        using SubT = std::remove_cvref_t<decltype(sub)>;
        constexpr std::size_t shard_idx = SubT::tag_type::index;
        for (auto& x : sub.span())
            x = shard_idx;
    });

    for (std::size_t shard = 0; shard < 8; ++shard) {
        for (std::size_t i = 0; i < 100; ++i) {
            CRUCIBLE_TEST_REQUIRE(recombined.cspan()[shard * 100 + i] == shard);
        }
    }
}

// The body runs once per shard, no more and no fewer.  A dispatch that
// ran a body twice would still produce the right answer for an
// idempotent body, so the count is checked directly.
void test_parallel_for_views_body_invoked_n_times() {
    Arena arena;
    constexpr std::size_t SHARDS = 8;
    constexpr std::size_t N = 4096;
    auto region = OwnedRegion<std::uint32_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 0;

    std::atomic<std::size_t> body_invocations{0};

    auto recombined = parallel_for_views<SHARDS>(std::move(region), [&body_invocations](auto sub) noexcept {
        body_invocations.fetch_add(1, std::memory_order_relaxed);
        for (auto& x : sub.span())
            x = 1;
    });

    CRUCIBLE_TEST_REQUIRE(body_invocations.load(std::memory_order_acquire) == SHARDS);
    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == 1);
    }
}

// Two shards is the smallest case that is genuinely parallel, since one
// shard takes a separate sequential path.
void test_parallel_for_views_n2_smallest_parallel() {
    Arena arena;
    auto perm = mint_permission_root<DataB>();
    constexpr std::size_t N = 4;
    auto region = OwnedRegion<std::uint64_t, DataB>::adopt(test_alloc_token(), arena, N, std::move(perm));
    region.span()[0] = 1;
    region.span()[1] = 2;
    region.span()[2] = 3;
    region.span()[3] = 4;

    auto recombined = parallel_for_views<2>(std::move(region), [](auto sub) noexcept {
        for (auto& x : sub.span())
            x = x * 10;
    });

    CRUCIBLE_TEST_REQUIRE(recombined.cspan()[0] == 10);
    CRUCIBLE_TEST_REQUIRE(recombined.cspan()[1] == 20);
    CRUCIBLE_TEST_REQUIRE(recombined.cspan()[2] == 30);
    CRUCIBLE_TEST_REQUIRE(recombined.cspan()[3] == 40);
}

// A size that does not divide by the shard count puts the short shard
// at the end, which is where a chunk-arithmetic error drops elements.
void test_parallel_for_views_uneven_split() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 100;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 0;

    auto recombined = parallel_for_views<8>(std::move(region), [](auto sub) noexcept {
        for (auto& x : sub.span())
            x = 1;
    });

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == 1);
    }
    CRUCIBLE_TEST_REQUIRE(recombined.size() == N);
}

// Disjoint shards plus the join ordering make the result independent of
// how the workers were scheduled, so two runs must agree exactly.
void test_parallel_for_views_deterministic_across_runs() {
    Arena arena_a, arena_b;
    constexpr std::size_t N = 1024;

    auto run = [](Arena& arena, auto perm) noexcept {
        auto region = OwnedRegion<std::uint32_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));
        for (std::size_t i = 0; i < N; ++i) {
            region.span()[i] = static_cast<std::uint32_t>(i * 31 + 7);
        }
        return parallel_for_views<4>(std::move(region), [](auto sub) noexcept {
            for (auto& x : sub.span())
                x = x ^ 0xA5A5A5A5u;
        });
    };

    // The second run is written out rather than reusing the lambda,
    // because a root permission is minted once per tag and the two runs
    // therefore need different tags.
    auto r1 = run(arena_a, mint_permission_root<DataA>());

    auto r2_arena_perm = mint_permission_root<DataB>();
    auto region_b = OwnedRegion<std::uint32_t, DataB>::adopt(test_alloc_token(), arena_b, N, std::move(r2_arena_perm));
    for (std::size_t i = 0; i < N; ++i) {
        region_b.span()[i] = static_cast<std::uint32_t>(i * 31 + 7);
    }
    auto r2 = parallel_for_views<4>(std::move(region_b), [](auto sub) noexcept {
        for (auto& x : sub.span())
            x = x ^ 0xA5A5A5A5u;
    });

    CRUCIBLE_TEST_REQUIRE(r1.size() == r2.size());
    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(r1.cspan()[i] == r2.cspan()[i]);
    }
}

void test_parallel_reduce_views_sum() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 50000;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = i + 1;

    auto [total, recombined] = parallel_reduce_views<8, std::uint64_t>(
        std::move(region), std::uint64_t{0},
        [](auto sub) noexcept {
            std::uint64_t local = 0;
            for (auto x : sub.cspan())
                local += x;
            return local;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; });

    // The expected total comes from the closed form rather than from a
    // second loop, so the test does not check the code against itself.
    const std::uint64_t expected = static_cast<std::uint64_t>(N) * (static_cast<std::uint64_t>(N) + 1) / 2;
    CRUCIBLE_TEST_REQUIRE(total == expected);
    CRUCIBLE_TEST_REQUIRE(recombined.size() == N);
}

// Maximum is associative but has no inverse, so it exercises a reducer
// shape that plain addition does not.
void test_parallel_reduce_views_max_abs() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 10000;
    auto region = OwnedRegion<std::int64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));

    // The largest magnitude is negative and sits in the middle, so a
    // reducer that lost the sign handling or skipped a shard would miss
    // it and settle on the positive outlier instead.
    for (std::size_t i = 0; i < N; ++i) {
        region.span()[i] = static_cast<std::int64_t>(i);
    }
    region.span()[5000] = -999999;
    region.span()[7000] = 888888;

    auto [max_abs, _] = parallel_reduce_views<4, std::int64_t>(
        std::move(region), std::int64_t{0},
        [](auto sub) noexcept {
            std::int64_t local = 0;
            for (auto x : sub.cspan()) {
                const std::int64_t a = (x < 0) ? -x : x;
                if (a > local) local = a;
            }
            return local;
        },
        [](std::int64_t a, std::int64_t b) noexcept { return (a > b) ? a : b; });

    CRUCIBLE_TEST_REQUIRE(max_abs == 999999);
}

// One shard takes a separate branch that runs inline instead of
// spawning a worker.  The initial value has to be folded in on that
// branch too, which is the part easy to lose when writing it.
void test_parallel_reduce_views_n1_init_participates() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 100;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 1;

    auto [total, _] = parallel_reduce_views<1, std::uint64_t>(
        std::move(region), std::uint64_t{1000},
        [](auto sub) noexcept {
            std::uint64_t s = 0;
            for (auto x : sub.cspan())
                s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; });

    // The initial 1000 plus the hundred ones in the region.
    CRUCIBLE_TEST_REQUIRE(total == 1100);
}

// The accumulator does not have to be a scalar.  A struct exercises the
// partials array and the fold with a type that is neither register-sized
// nor default-zero.
void test_parallel_reduce_views_struct_accumulator() {
    struct Stats {
        std::uint64_t count = 0;
        std::uint64_t sum = 0;
    };

    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 10000;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = i + 1;

    auto [stats, _] = parallel_reduce_views<8, Stats>(
        std::move(region), Stats{},
        [](auto sub) noexcept {
            Stats local{};
            for (auto x : sub.cspan()) {
                local.count += 1;
                local.sum += x;
            }
            return local;
        },
        [](Stats a, Stats b) noexcept { return Stats{a.count + b.count, a.sum + b.sum}; });

    const std::uint64_t expected_sum = static_cast<std::uint64_t>(N) * (static_cast<std::uint64_t>(N) + 1) / 2;
    CRUCIBLE_TEST_REQUIRE(stats.count == N);
    CRUCIBLE_TEST_REQUIRE(stats.sum == expected_sum);
}

// A reduce reads the region and returns it, so the buffer that comes
// back must still hold the original bytes for whoever consumes it next.
void test_parallel_reduce_views_recombined_data_unchanged() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 1024;
    auto region = OwnedRegion<std::uint32_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i) {
        region.span()[i] = static_cast<std::uint32_t>(i * 31 + 7);
    }

    auto [total, recombined] = parallel_reduce_views<8, std::uint64_t>(
        std::move(region), std::uint64_t{0},
        [](auto sub) noexcept {
            std::uint64_t s = 0;
            for (auto x : sub.cspan())
                s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; });
    (void)total;

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == static_cast<std::uint32_t>(i * 31 + 7));
    }
}

// The partials are folded left to right in shard order, not in the
// order the workers happened to finish.  A reducer that is sensitive to
// argument order is what makes the difference visible.
void test_parallel_reduce_views_deterministic_across_runs() {
    Arena arena_a, arena_b;
    constexpr std::size_t N = 1024;

    auto region_a =
        OwnedRegion<std::uint32_t, DataA>::adopt(test_alloc_token(), arena_a, N, mint_permission_root<DataA>());
    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = static_cast<std::uint32_t>(i % 17);
    }

    // This reducer is neither commutative nor associative, so the same
    // partials fold to the same value only when the order is fixed.
    auto reducer = [](std::int64_t a, std::int64_t b) noexcept { return 2 * a - b; };
    auto mapper = [](auto sub) noexcept {
        std::int64_t s = 0;
        for (auto x : sub.cspan())
            s += x;
        return s;
    };

    auto [r1, _r1] = parallel_reduce_views<4, std::int64_t>(std::move(region_a), std::int64_t{0}, mapper, reducer);

    auto region_b =
        OwnedRegion<std::uint32_t, DataB>::adopt(test_alloc_token(), arena_b, N, mint_permission_root<DataB>());
    for (std::size_t i = 0; i < N; ++i) {
        region_b.span()[i] = static_cast<std::uint32_t>(i % 17);
    }

    auto [r2, _r2] = parallel_reduce_views<4, std::int64_t>(std::move(region_b), std::int64_t{0}, mapper, reducer);

    CRUCIBLE_TEST_REQUIRE(r1 == r2);
}

// Two shards is the smallest reduce that produces two partials and
// folds them after the join.
void test_parallel_reduce_views_n2_smallest_parallel() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 4;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));
    region.span()[0] = 10;
    region.span()[1] = 20;
    region.span()[2] = 30;
    region.span()[3] = 40;

    auto [total, _] = parallel_reduce_views<2, std::uint64_t>(
        std::move(region), std::uint64_t{0},
        [](auto sub) noexcept {
            std::uint64_t s = 0;
            for (auto x : sub.cspan())
                s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; });

    CRUCIBLE_TEST_REQUIRE(total == 100);
}

// Every element is one and the initial value is seven, so the expected
// total is seven plus the element count.  That makes the check sensitive
// to two separate faults: a shard skipping part of the uneven suffix
// lowers the total, and folding the initial value more than once raises
// it.
void test_parallel_reduce_views_uneven_split() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 100;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 1;

    auto [total, _] = parallel_reduce_views<8, std::uint64_t>(
        std::move(region),
        /*init=*/std::uint64_t{7},
        [](auto sub) noexcept -> std::uint64_t {
            std::uint64_t s = 0;
            for (auto x : sub.cspan())
                s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; });

    CRUCIBLE_TEST_REQUIRE(total == 107);
}

// The counter is atomic because it is the one thing in this file that
// several workers write to on purpose.
void test_parallel_reduce_views_mapper_invoked_n_times() {
    Arena arena;
    constexpr std::size_t SHARDS = 8;
    constexpr std::size_t N = 4096;
    auto region = OwnedRegion<std::uint32_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 1;

    std::atomic<std::size_t> mapper_invocations{0};

    auto [total, _] = parallel_reduce_views<SHARDS, std::uint64_t>(
        std::move(region), std::uint64_t{0},
        [&mapper_invocations](auto sub) noexcept {
            mapper_invocations.fetch_add(1, std::memory_order_relaxed);
            std::uint64_t s = 0;
            for (auto x : sub.cspan())
                s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; });

    CRUCIBLE_TEST_REQUIRE(total == N);
    CRUCIBLE_TEST_REQUIRE(mapper_invocations.load(std::memory_order_acquire) == SHARDS);
}

// A paired apply splits two regions in lockstep and hands each worker
// the matching shard of both.  The tests below check the pairing, since
// a misalignment would still produce plausible-looking output.
void test_parallel_apply_pair_vector_add() {
    Arena arena;
    constexpr std::size_t N = 4096;

    auto perm_a = mint_permission_root<DataA>();
    auto region_a = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm_a));
    auto perm_b = mint_permission_root<DataB>();
    auto region_b = OwnedRegion<std::uint64_t, DataB>::adopt(test_alloc_token(), arena, N, std::move(perm_b));

    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = i;
        region_b.span()[i] = 2 * i;
    }

    auto [recombined_a, recombined_b] =
        parallel_apply_pair<8>(std::move(region_a), std::move(region_b), [](auto sub_a, auto sub_b) noexcept {
            // The two shards match in size because the parents match in
            // size and the chunk arithmetic is the same for both.
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.size() == sub_b.size());
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i) {
                dst[i] = src[i] + dst[i];
            }
        });

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined_a.cspan()[i] == i);
        CRUCIBLE_TEST_REQUIRE(recombined_b.cspan()[i] == 3 * i);
    }
    CRUCIBLE_TEST_REQUIRE(recombined_a.size() == N);
    CRUCIBLE_TEST_REQUIRE(recombined_b.size() == N);
}

// One shard runs inline and still presents both whole regions to the
// body as single-shard sub-regions.
void test_parallel_apply_pair_sequential_n1() {
    Arena arena;
    constexpr std::size_t N = 100;

    auto region_a =
        OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b =
        OwnedRegion<std::uint64_t, DataB>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataB>());

    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = i;
        region_b.span()[i] = 0;
    }

    auto [out_a, out_b] =
        parallel_apply_pair<1>(std::move(region_a), std::move(region_b), [](auto sub_a, auto sub_b) noexcept {
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.size() == 100);
            CRUCIBLE_TEST_REQUIRE_NX(sub_b.size() == 100);
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i)
                dst[i] = src[i] * 7;
        });

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(out_b.cspan()[i] == i * 7);
    }
}

void test_parallel_apply_pair_n2_smallest_parallel() {
    Arena arena;
    constexpr std::size_t N = 4;

    auto region_a =
        OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b =
        OwnedRegion<std::uint64_t, DataB>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataB>());
    region_a.span()[0] = 1;
    region_a.span()[1] = 2;
    region_a.span()[2] = 3;
    region_a.span()[3] = 4;
    for (std::size_t i = 0; i < N; ++i)
        region_b.span()[i] = 0;

    auto [out_a, out_b] =
        parallel_apply_pair<2>(std::move(region_a), std::move(region_b), [](auto sub_a, auto sub_b) noexcept {
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.size() == sub_b.size());
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i) {
                dst[i] = src[i] * src[i];
            }
        });

    CRUCIBLE_TEST_REQUIRE(out_b.cspan()[0] == 1);
    CRUCIBLE_TEST_REQUIRE(out_b.cspan()[1] == 4);
    CRUCIBLE_TEST_REQUIRE(out_b.cspan()[2] == 9);
    CRUCIBLE_TEST_REQUIRE(out_b.cspan()[3] == 16);
}

// Under an uneven split the two regions must still be cut identically,
// or the short shard of one would pair with a full shard of the other.
void test_parallel_apply_pair_uneven_split() {
    Arena arena;
    constexpr std::size_t N = 100;

    auto region_a =
        OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b =
        OwnedRegion<std::uint64_t, DataB>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataB>());

    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = i + 1;
        region_b.span()[i] = 0;
    }

    auto [out_a, out_b] =
        parallel_apply_pair<8>(std::move(region_a), std::move(region_b), [](auto sub_a, auto sub_b) noexcept {
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.size() == sub_b.size());
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i)
                dst[i] = src[i];
        });

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(out_b.cspan()[i] == i + 1);
    }
}

void test_parallel_apply_pair_body_invoked_n_times() {
    Arena arena;
    constexpr std::size_t SHARDS = 8;
    constexpr std::size_t N = 800;

    auto region_a =
        OwnedRegion<std::uint32_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b =
        OwnedRegion<std::uint32_t, DataB>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataB>());

    std::atomic<std::size_t> body_invocations{0};

    auto recombined = parallel_apply_pair<SHARDS>(std::move(region_a), std::move(region_b),
                                                  [&body_invocations](auto sub_a, auto sub_b) noexcept {
                                                      body_invocations.fetch_add(1, std::memory_order_relaxed);
                                                      (void)sub_a;
                                                      (void)sub_b;
                                                  });

    CRUCIBLE_TEST_REQUIRE(body_invocations.load(std::memory_order_acquire) == SHARDS);
    CRUCIBLE_TEST_REQUIRE(recombined.first.size() == N);
    CRUCIBLE_TEST_REQUIRE(recombined.second.size() == N);
}

// The per-element operation folds in the element's position within its
// shard, so a run that cut the shards differently produces different
// bytes rather than matching by luck.
void test_parallel_apply_pair_deterministic_across_runs() {
    Arena arena;
    constexpr std::size_t N = 1024;

    auto run_once = [&arena]() noexcept {
        auto src =
            OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
        auto dst =
            OwnedRegion<std::uint64_t, DataB>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataB>());
        for (std::size_t i = 0; i < N; ++i) {
            src.span()[i] = std::uint64_t{i} * 31u + 7u;
            dst.span()[i] = std::uint64_t{i} * 17u + 3u;
        }

        auto [_a, out_b] = parallel_apply_pair<8>(std::move(src), std::move(dst), [](auto sub_a, auto sub_b) noexcept {
            auto a = sub_a.cspan();
            auto b = sub_b.span();
            for (std::size_t i = 0; i < b.size(); ++i) {
                b[i] = a[i] ^ b[i] ^ static_cast<std::uint64_t>(i);
            }
        });
        std::array<std::uint64_t, N> snapshot{};
        for (std::size_t i = 0; i < N; ++i)
            snapshot[i] = out_b.cspan()[i];
        return snapshot;
    };

    auto first = run_once();
    auto second = run_once();
    auto third = run_once();
    CRUCIBLE_TEST_REQUIRE(first == second);
    CRUCIBLE_TEST_REQUIRE(second == third);
}

// The two regions are allowed to share a tag.  Each carries its own
// root permission, so the split chains stay independent even though the
// shard types coincide, and the pairing does not depend on the types
// being distinguishable.
void test_parallel_apply_pair_same_tag() {
    Arena arena;
    constexpr std::size_t N = 64;

    auto region_a =
        OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b =
        OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());

    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = i;
        region_b.span()[i] = 0;
    }

    auto [out_a, out_b] =
        parallel_apply_pair<4>(std::move(region_a), std::move(region_b), [](auto sub_a, auto sub_b) noexcept {
            // The two shards have identical types here, so only the
            // pointers can show that they do not alias.
            CRUCIBLE_TEST_REQUIRE_NX(sub_a.data() != sub_b.data());
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i)
                dst[i] = src[i] * 11;
        });

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(out_b.cspan()[i] == i * 11);
    }
}

// The same disjointness scan as for a single region, applied to the
// written side of the pair.  The read-only side is checked to be
// untouched, which the single-region version cannot test.
void test_parallel_apply_pair_disjoint_shards() {
    Arena arena;
    constexpr std::size_t N = 800;  // 8 × 100, exact division.

    auto region_a =
        OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b =
        OwnedRegion<std::uint64_t, DataB>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataB>());

    // Both sentinels are outside the range of shard indices, so an
    // untouched element stays distinguishable from a written one.
    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = 0xCAFE;
        region_b.span()[i] = 0xDEAD;
    }

    auto [out_a, out_b] =
        parallel_apply_pair<8>(std::move(region_a), std::move(region_b), [](auto sub_a, auto sub_b) noexcept {
            using SubB = std::remove_cvref_t<decltype(sub_b)>;
            constexpr std::size_t shard_idx = SubB::tag_type::index;
            (void)sub_a;
            for (auto& x : sub_b.span())
                x = shard_idx;
        });

    for (std::size_t shard = 0; shard < 8; ++shard) {
        for (std::size_t i = 0; i < 100; ++i) {
            CRUCIBLE_TEST_REQUIRE(out_a.cspan()[shard * 100 + i] == 0xCAFE);
            CRUCIBLE_TEST_REQUIRE(out_b.cspan()[shard * 100 + i] == shard);
        }
    }
}

// The two regions need share neither element type nor tag, which is
// what makes a paired apply usable for a conversion.
void test_parallel_apply_pair_heterogeneous_types() {
    Arena arena;
    constexpr std::size_t N = 64;

    auto region_a = OwnedRegion<float, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());
    auto region_b =
        OwnedRegion<std::int32_t, DataB>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataB>());
    // The half in each source value is what the conversion drops, so
    // the expected output is the index rather than the value.
    for (std::size_t i = 0; i < N; ++i) {
        region_a.span()[i] = static_cast<float>(i) + 0.5f;
        region_b.span()[i] = 0;
    }

    auto [out_a, out_b] =
        parallel_apply_pair<4>(std::move(region_a), std::move(region_b), [](auto sub_a, auto sub_b) noexcept {
            auto src = sub_a.cspan();
            auto dst = sub_b.span();
            for (std::size_t i = 0; i < dst.size(); ++i) {
                dst[i] = static_cast<std::int32_t>(src[i]);
            }
        });

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(out_b.cspan()[i] == static_cast<std::int32_t>(i));
    }
}

void test_parallel_for_views_sequential_n1() {
    Arena arena;
    auto perm = mint_permission_root<DataSeq>();
    constexpr std::size_t N = 100;
    auto region = OwnedRegion<std::uint64_t, DataSeq>::adopt(test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = i;

    // One shard runs inline rather than spawning a thread, and the body
    // still sees the whole region.
    auto recombined = parallel_for_views<1>(std::move(region), [](auto sub) noexcept {
        CRUCIBLE_TEST_REQUIRE_NX(sub.size() == 100);
        for (auto& x : sub.span())
            x *= 2;
    });

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == i * 2);
    }
}

// The adaptive form consults a cost model before deciding to fork.  The
// two tests below pick sizes on either side of that decision and then
// assert the result is right either way, because the decision itself
// depends on the machine.
void test_adaptive_picks_sequential_for_small_workload() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 100;  // small enough to sit in L1
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = i + 1;

    WorkBudget tiny_budget{
        .read_bytes = N * sizeof(std::uint64_t),
        .write_bytes = N * sizeof(std::uint64_t),
        .item_count = N,
    };
    // A workload that already fits in the closest cache gains nothing
    // from more cores, so the model declines to fork.
    CRUCIBLE_TEST_REQUIRE(!should_parallelize(tiny_budget));

    auto recombined = parallel_for_views_adaptive<8>(
        std::move(region),
        [](auto sub) noexcept {
            for (auto& x : sub.span())
                x = x * x;
        },
        tiny_budget);

    for (std::size_t i = 0; i < N; ++i) {
        const std::uint64_t expected = (i + 1) * (i + 1);
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == expected);
    }
}

void test_adaptive_picks_parallel_for_large_workload() {
    Arena arena{1ULL << 24};
    auto perm = mint_permission_root<DataB>();
    constexpr std::size_t N = 200000;  // larger than one core's L2
    auto region = OwnedRegion<std::uint64_t, DataB>::adopt(test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = i;

    WorkBudget large_budget{
        .read_bytes = N * sizeof(std::uint64_t),
        .write_bytes = N * sizeof(std::uint64_t),
        .item_count = N,
    };
    CRUCIBLE_TEST_REQUIRE(should_parallelize(large_budget));

    auto recombined = parallel_for_views_adaptive<8>(
        std::move(region),
        [](auto sub) noexcept {
            for (auto& x : sub.span())
                x += 1000;
        },
        large_budget);

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == i + 1000);
    }
}

void test_workbudget_for_span() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, 1024, std::move(perm));

    const auto budget = WorkBudget::for_span<std::uint64_t>(region.cspan());

    CRUCIBLE_TEST_REQUIRE(budget.item_count == 1024);
    CRUCIBLE_TEST_REQUIRE(budget.read_bytes == 1024 * sizeof(std::uint64_t));
    CRUCIBLE_TEST_REQUIRE(budget.write_bytes == 1024 * sizeof(std::uint64_t));

    const auto ro_budget = WorkBudget::for_span_read_only<std::uint64_t>(region.cspan());
    CRUCIBLE_TEST_REQUIRE(ro_budget.read_bytes == 1024 * sizeof(std::uint64_t));
    CRUCIBLE_TEST_REQUIRE(ro_budget.write_bytes == 0);
}

// The smart form derives the budget from the region itself, so the
// caller does not state it.  Correctness of the result is all these two
// can assert, since the decision depends on the machine.
void test_parallel_for_smart_small_workload() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 100;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = i + 1;

    auto recombined = parallel_for_smart(std::move(region), [](auto sub) noexcept {
        for (auto& x : sub.span())
            x *= 3;
    });

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == (i + 1) * 3);
    }
}

void test_parallel_for_smart_large_workload() {
    Arena arena{1ULL << 24};
    auto perm = mint_permission_root<DataB>();
    constexpr std::size_t N = 200000;  // larger than one core's L2
    auto region = OwnedRegion<std::uint64_t, DataB>::adopt(test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 0;

    auto recombined = parallel_for_smart(std::move(region), [](auto sub) noexcept {
        for (auto& x : sub.span())
            x = 42;
    });

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(recombined.cspan()[i] == 42);
    }
}

// This one only checks that the call is reachable and survives, since
// its output is the machine's topology and not something to assert on.
void test_log_topology_at_startup() {
    std::fprintf(stderr, "\n      log_topology_at_startup() output ↓\n");
    log_topology_at_startup();
    std::fprintf(stderr, "      ↑\n      ");
}

void test_stress_parallel_for_repeated() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    constexpr std::size_t N = 16384;
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 0;

    // Repeated forks over the same region.  A lost or repeated update on
    // any pass leaves an element off the final count, and the whole test
    // is worth running again under a race detector.
    constexpr int K = 10;
    auto current = std::move(region);
    for (int iter = 0; iter < K; ++iter) {
        current = parallel_for_views<8>(std::move(current), [](auto sub) noexcept {
            for (auto& x : sub.span())
                ++x;
        });
    }

    for (std::size_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_REQUIRE(current.cspan()[i] == K);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_owned_region:\n");

    test_compile_time_properties();  // pure compile-time

    run_test("test_adopt_and_view", test_adopt_and_view);
    run_test("test_split_into_chunk_math", test_split_into_chunk_math);
    run_test("test_split_uneven", test_split_uneven);
    run_test("test_split_smaller_than_n", test_split_smaller_than_n);
    run_test("test_parallel_for_views_squares", test_parallel_for_views_squares);
    run_test("test_parallel_for_views_uses_correct_slice_indices", test_parallel_for_views_uses_correct_slice_indices);
    run_test("test_parallel_for_views_body_invoked_n_times", test_parallel_for_views_body_invoked_n_times);
    run_test("test_parallel_for_views_n2_smallest_parallel", test_parallel_for_views_n2_smallest_parallel);
    run_test("test_parallel_for_views_uneven_split", test_parallel_for_views_uneven_split);
    run_test("test_parallel_for_views_deterministic_across_runs", test_parallel_for_views_deterministic_across_runs);
    run_test("test_parallel_reduce_views_sum", test_parallel_reduce_views_sum);
    run_test("test_parallel_reduce_views_max_abs", test_parallel_reduce_views_max_abs);
    run_test("test_parallel_reduce_views_n1_init_participates", test_parallel_reduce_views_n1_init_participates);
    run_test("test_parallel_reduce_views_struct_accumulator", test_parallel_reduce_views_struct_accumulator);
    run_test("test_parallel_reduce_views_recombined_data_unchanged",
             test_parallel_reduce_views_recombined_data_unchanged);
    run_test("test_parallel_reduce_views_deterministic_across_runs",
             test_parallel_reduce_views_deterministic_across_runs);
    run_test("test_parallel_reduce_views_mapper_invoked_n_times", test_parallel_reduce_views_mapper_invoked_n_times);
    run_test("test_parallel_reduce_views_n2_smallest_parallel", test_parallel_reduce_views_n2_smallest_parallel);
    run_test("test_parallel_reduce_views_uneven_split", test_parallel_reduce_views_uneven_split);
    run_test("test_parallel_apply_pair_vector_add", test_parallel_apply_pair_vector_add);
    run_test("test_parallel_apply_pair_sequential_n1", test_parallel_apply_pair_sequential_n1);
    run_test("test_parallel_apply_pair_n2_smallest_parallel", test_parallel_apply_pair_n2_smallest_parallel);
    run_test("test_parallel_apply_pair_uneven_split", test_parallel_apply_pair_uneven_split);
    run_test("test_parallel_apply_pair_body_invoked_n_times", test_parallel_apply_pair_body_invoked_n_times);
    run_test("test_parallel_apply_pair_deterministic_across_runs", test_parallel_apply_pair_deterministic_across_runs);
    run_test("test_parallel_apply_pair_heterogeneous_types", test_parallel_apply_pair_heterogeneous_types);
    run_test("test_parallel_apply_pair_same_tag", test_parallel_apply_pair_same_tag);
    run_test("test_parallel_apply_pair_disjoint_shards", test_parallel_apply_pair_disjoint_shards);
    run_test("test_parallel_for_views_sequential_n1", test_parallel_for_views_sequential_n1);
    run_test("test_adaptive_picks_sequential_for_small_workload", test_adaptive_picks_sequential_for_small_workload);
    run_test("test_adaptive_picks_parallel_for_large_workload", test_adaptive_picks_parallel_for_large_workload);
    run_test("test_workbudget_for_span", test_workbudget_for_span);
    run_test("test_parallel_for_smart_small_workload", test_parallel_for_smart_small_workload);
    run_test("test_parallel_for_smart_large_workload", test_parallel_for_smart_large_workload);
    run_test("test_log_topology_at_startup", test_log_topology_at_startup);
    run_test("test_stress_parallel_for_repeated", test_stress_parallel_for_repeated);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
