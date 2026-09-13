// What is under test is the re-export, not the grid.  Including the
// umbrella header is part of the claim, because the static_asserts it
// carries are never compiled under the project warning flags until some
// translation unit pulls it in.
//
// Each shard owns an independent ladder of buckets and pops its lowest
// key first.  Across shards there is no ordering at all, and avoiding the
// atomic coordination that would impose one is the point of the design.

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc = ::crucible::concurrent;
namespace cs = ::crucible::safety;

namespace probes {

// A tag local to this file gives the templates a whole, producer and
// consumer triple that no other translation unit shares.
struct V049TestUserTag {};

// Using the item as its own priority key means item N lands in bucket N
// on a fresh shard, which is what makes every expectation below readable.
struct V049TestKey {
    static std::uint64_t key(std::uint64_t v) noexcept { return v; }
};

}  // namespace probes

constexpr std::size_t kNumShards = 4;
constexpr std::size_t kNumBuckets = 32;
constexpr std::size_t kBucketCap = 16;
constexpr std::uint64_t kQuantumNs = 1ULL;

using TestGrid = fsubstr::sharded_calendar_grid::PermissionedShardedCalendarGrid<
    std::uint64_t, kNumShards, kNumBuckets, kBucketCap, probes::V049TestKey, kQuantumNs, probes::V049TestUserTag>;

static_assert(
    std::is_same_v<TestGrid,
                   cc::PermissionedShardedCalendarGrid<std::uint64_t, kNumShards, kNumBuckets, kBucketCap,
                                                       probes::V049TestKey, kQuantumNs, probes::V049TestUserTag>>,
    "fixy::substr::sharded_calendar_grid::PermissionedShardedCalendarGrid "
    "must alias the substrate.");

static_assert(fsubstr::sharded_calendar_grid::ShardedCalendarKeyExtractorOf<probes::V049TestKey, std::uint64_t>);
static_assert(fsubstr::sharded_calendar_grid::ShardedCalendarKeyExtractorOf<probes::V049TestKey, std::uint64_t>
              == cc::ShardedCalendarKeyExtractorOf<probes::V049TestKey, std::uint64_t>);
// The concept asks for a static `key(const T&) noexcept` returning a
// 64-bit unsigned value.  An empty struct has none, on either path.
struct NonShardedCalendarKey {};
static_assert(!fsubstr::sharded_calendar_grid::ShardedCalendarKeyExtractorOf<NonShardedCalendarKey, std::uint64_t>);
static_assert(!cc::ShardedCalendarKeyExtractorOf<NonShardedCalendarKey, std::uint64_t>);

static_assert(std::is_same_v<fsubstr::sharded_calendar_grid::sharded_calendar_tag::Whole<probes::V049TestUserTag>,
                             cc::sharded_calendar_tag::Whole<probes::V049TestUserTag>>);
static_assert(std::is_same_v<fsubstr::sharded_calendar_grid::sharded_calendar_tag::Producer<probes::V049TestUserTag, 0>,
                             cc::sharded_calendar_tag::Producer<probes::V049TestUserTag, 0>>);
static_assert(std::is_same_v<
              fsubstr::sharded_calendar_grid::sharded_calendar_tag::Producer<probes::V049TestUserTag, kNumShards - 1>,
              cc::sharded_calendar_tag::Producer<probes::V049TestUserTag, kNumShards - 1>>);
static_assert(std::is_same_v<fsubstr::sharded_calendar_grid::sharded_calendar_tag::Consumer<probes::V049TestUserTag, 0>,
                             cc::sharded_calendar_tag::Consumer<probes::V049TestUserTag, 0>>);
static_assert(std::is_same_v<
              fsubstr::sharded_calendar_grid::sharded_calendar_tag::Consumer<probes::V049TestUserTag, kNumShards - 1>,
              cc::sharded_calendar_tag::Consumer<probes::V049TestUserTag, kNumShards - 1>>);

static_assert(std::is_same_v<typename TestGrid::value_type, std::uint64_t>);
static_assert(std::is_same_v<typename TestGrid::user_tag, probes::V049TestUserTag>);
static_assert(std::is_same_v<typename TestGrid::key_extractor, probes::V049TestKey>);
static_assert(std::is_same_v<typename TestGrid::whole_tag,
                             fsubstr::sharded_calendar_grid::sharded_calendar_tag::Whole<probes::V049TestUserTag>>);
static_assert(
    std::is_same_v<typename TestGrid::template shard_producer_tag<0>,
                   fsubstr::sharded_calendar_grid::sharded_calendar_tag::Producer<probes::V049TestUserTag, 0>>);
static_assert(std::is_same_v<
              typename TestGrid::template shard_consumer_tag<kNumShards - 1>,
              fsubstr::sharded_calendar_grid::sharded_calendar_tag::Consumer<probes::V049TestUserTag, kNumShards - 1>>);

static_assert(TestGrid::num_shards == kNumShards);
static_assert(TestGrid::num_buckets == kNumBuckets);
static_assert(TestGrid::bucket_cap == kBucketCap);
static_assert(TestGrid::quantum_ns == kQuantumNs);

static_assert(fsubstr::sharded_calendar_grid::ShardedCalendarGridSessionSurface<TestGrid>);

static auto fresh_sharded_calendar_perms() {
    auto whole = cs::mint_permission_root<TestGrid::whole_tag>();
    return cs::mint_grid_permissions<TestGrid::whole_tag, kNumShards, kNumShards>(std::move(whole));
}

// Each handle holds a linear permission, so the fact that the handles
// exist at all is the proof that the tokens were consumed.
static void test_runtime_construct_and_handles() {
    TestGrid grid{};
    auto perms = fresh_sharded_calendar_perms();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));

    if (p0.size_approx() != 0) std::abort();
    if (!c0.empty_approx()) std::abort();
    if (TestGrid::ProducerHandle<0>::capacity() != kNumBuckets * kBucketCap) std::abort();
    if (TestGrid::ConsumerHandle<0>::capacity() != kNumBuckets * kBucketCap) std::abort();

    static_assert(decltype(p0)::shard_index == 0);
    static_assert(decltype(c0)::shard_index == 0);
}

// With a quantum of one and the current bucket still at zero, item N
// lands in bucket N.  The consumer scans forward from the current bucket,
// so it yields ascending keys whatever order they were pushed in.
static void test_runtime_within_shard_priority_order() {
    TestGrid grid{};
    auto perms = fresh_sharded_calendar_perms();

    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));

    constexpr int N = 8;  // well under the bucket count, so no wraparound
    // The push loop runs downwards on purpose.  Draining must still come
    // back ascending.
    for (int i = N - 1; i >= 0; --i) {
        if (!p1.try_push(static_cast<std::uint64_t>(i))) std::abort();
    }

    for (int expected = 0; expected < N; ++expected) {
        std::optional<std::uint64_t> r = c1.try_pop();
        if (!r) std::abort();
        if (*r != static_cast<std::uint64_t>(expected)) std::abort();
    }
    if (c1.try_pop()) std::abort();
}

// Shards write to disjoint storage, so an item pushed to one can never
// surface in another one's drain.
static void test_runtime_cross_shard_isolation() {
    TestGrid grid{};
    auto perms = fresh_sharded_calendar_perms();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));

    for (std::uint64_t k = 0; k < 8; k += 2) {
        if (!p0.try_push(k)) std::abort();
    }
    for (std::uint64_t k = 1; k < 8; k += 2) {
        if (!p2.try_push(k)) std::abort();
    }

    for (std::uint64_t expected = 0; expected < 8; expected += 2) {
        std::optional<std::uint64_t> r = c0.try_pop();
        if (!r) std::abort();
        if (*r != expected) std::abort();
    }
    if (c0.try_pop()) std::abort();

    for (std::uint64_t expected = 1; expected < 8; expected += 2) {
        std::optional<std::uint64_t> r = c2.try_pop();
        if (!r) std::abort();
        if (*r != expected) std::abort();
    }
    if (c2.try_pop()) std::abort();
}

static void test_runtime_value_template_constants() {
    static_assert(TestGrid::num_shards == kNumShards);
    static_assert(TestGrid::num_buckets == kNumBuckets);
    static_assert(TestGrid::bucket_cap == kBucketCap);
    static_assert(TestGrid::quantum_ns == kQuantumNs);
    volatile std::size_t n = TestGrid::num_shards;
    volatile std::size_t b = TestGrid::num_buckets;
    volatile std::size_t bc = TestGrid::bucket_cap;
    volatile std::uint64_t q = TestGrid::quantum_ns;
    if (n != kNumShards) std::abort();
    if (b != kNumBuckets) std::abort();
    if (bc != kBucketCap) std::abort();
    if (q != kQuantumNs) std::abort();
}

// The pointer assignment below compiles only if the alias names the
// substrate type itself, rather than some wrapper around it.
static void test_runtime_substrate_identity() {
    static_assert(
        std::is_same_v<TestGrid,
                       cc::PermissionedShardedCalendarGrid<std::uint64_t, kNumShards, kNumBuckets, kBucketCap,
                                                           probes::V049TestKey, kQuantumNs, probes::V049TestUserTag>>);
    TestGrid grid{};
    cc::PermissionedShardedCalendarGrid<std::uint64_t, kNumShards, kNumBuckets, kBucketCap, probes::V049TestKey,
                                        kQuantumNs, probes::V049TestUserTag>* via_sub = &grid;
    TestGrid* via_fixy = via_sub;
    if (via_fixy != via_sub) std::abort();
}

static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::sharded_calendar_grid::ProducerProto<std::uint64_t>;
    using FixyCons = fsubstr::sharded_calendar_grid::ConsumerProto<std::uint64_t>;
    using SubsProd = ::crucible::safety::proto::sharded_calendar_grid_session::ProducerProto<std::uint64_t>;
    using SubsCons = ::crucible::safety::proto::sharded_calendar_grid_session::ConsumerProto<std::uint64_t>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

int main() {
    test_runtime_construct_and_handles();
    test_runtime_within_shard_priority_order();
    test_runtime_cross_shard_isolation();
    test_runtime_value_template_constants();
    test_runtime_substrate_identity();
    test_runtime_protocol_aliases_unchanged();
    std::printf("test_fixy_substr_shardcal_permissioned_sharded_calendar_grid: "
                "6/6 runtime witnesses passed\n");
    return 0;
}
