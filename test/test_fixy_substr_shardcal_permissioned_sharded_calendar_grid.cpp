// ── test_fixy_substr_shardcal_permissioned_sharded_calendar_grid —
// V-049 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/Substr.h` v049:: block under the project's
// warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), plus runtime
// witnesses on top of the compile-time identity sentinels.
//
// Covers the V-049 ShardedCalendarGrid substrate-direct surface additions:
//   * PermissionedShardedCalendarGrid<T, NumShards, NumBuckets,
//                                     BucketCap, KeyExtractor,
//                                     QuantumNs, UserTag>     — substrate alias
//   * ShardedCalendarKeyExtractorOf<K, T>                     — concept re-export
//   * sharded_calendar_tag::{Whole,Producer<S>,Consumer<S>}<UserTag>
//                                                              — tag tree
//   (ShardedCalendarGridSessionSurface already shipped pre-V-049)
//   (mint_sharded_calendar_grid_producer / mint_sharded_calendar_grid_consumer
//    / mint_producer_session / mint_consumer_session already shipped
//    pre-V-049 via using-decl)
//
// ShardedCalendarGrid structural notes (vs V-048 ShardedGrid):
//   * SIXTH cell of the channel-permission family — linear-grid ×
//     linear-grid with KEY-PRIORITY per shard.  Each shard owns an
//     independent NumBuckets × BucketCap calendar (priority queue
//     ladder); current_bucket(S) is a monotone counter only consumer<S>
//     advances.  Inside shard S, lowest-priority key pops first;
//     across shards no global ordering (the cross-thread atomic
//     coordination cost is what the structural design eliminates).
//   * 2*NumShards permissions descend from one Whole<UserTag> root
//     via the FOUND-A22 grid mint pattern
//     mint_grid_permissions<Whole, N, N>(parent) — both `.producers`
//     and `.consumers` tuples have NumShards elements, each Linear.
//   * Substrate requires SpscValue T (trivially-copyable +
//     trivially-destructible) — reused from V-045.  V-049-unique
//     surface is the ShardedCalendarKeyExtractorOf<K, T> concept
//     (K::key(const T&) noexcept → uint64_t).

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
namespace cc      = ::crucible::concurrent;
namespace cs      = ::crucible::safety;

// ═══════════════════════════════════════════════════════════════════
// ── Synthetic UserTag + KeyExtractor for V-049 fixtures ──────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

// TU-local tag so PermissionedShardedCalendarGrid.h's generic
// specialization picks up the fresh (Whole, Producer<S>, Consumer<S>)
// triple via the UserTag-parameterized templates.
struct V049TestUserTag {};

// KeyExtractor: extracts the item itself as the priority key.  The
// substrate clamps the bucket to current_bucket(S) so the test's
// item-equals-key choice means item N lands in bucket N (modulo
// NumBuckets) on a freshly-constructed shard.
struct V049TestKey {
    static std::uint64_t key(std::uint64_t v) noexcept { return v; }
};

}  // namespace probes

// ═══════════════════════════════════════════════════════════════════
// ── Fixture parameters ──────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

constexpr std::size_t   kNumShards  = 4;
constexpr std::size_t   kNumBuckets = 32;
constexpr std::size_t   kBucketCap  = 16;
constexpr std::uint64_t kQuantumNs  = 1ULL;

using TestGrid =
    fsubstr::sharded_calendar_grid::PermissionedShardedCalendarGrid<
        std::uint64_t, kNumShards, kNumBuckets, kBucketCap,
        probes::V049TestKey, kQuantumNs, probes::V049TestUserTag>;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════

// ── 1. Substrate alias identity ─────────────────────────────────
static_assert(std::is_same_v<
    TestGrid,
    cc::PermissionedShardedCalendarGrid<
        std::uint64_t, kNumShards, kNumBuckets, kBucketCap,
        probes::V049TestKey, kQuantumNs, probes::V049TestUserTag>>,
    "fixy::substr::sharded_calendar_grid::PermissionedShardedCalendarGrid "
    "must alias the substrate.");

// ── 2. ShardedCalendarKeyExtractorOf concept admission parity ───
static_assert(fsubstr::sharded_calendar_grid::
    ShardedCalendarKeyExtractorOf<probes::V049TestKey, std::uint64_t>);
static_assert(fsubstr::sharded_calendar_grid::
    ShardedCalendarKeyExtractorOf<probes::V049TestKey, std::uint64_t> ==
    cc::ShardedCalendarKeyExtractorOf<probes::V049TestKey, std::uint64_t>);
// Negative — a type with no `static uint64_t key(const T&) noexcept`
// fails the concept on BOTH paths.
struct NonShardedCalendarKey {};
static_assert(!fsubstr::sharded_calendar_grid::
    ShardedCalendarKeyExtractorOf<NonShardedCalendarKey, std::uint64_t>);
static_assert(!cc::ShardedCalendarKeyExtractorOf<
    NonShardedCalendarKey, std::uint64_t>);

// ── 3. Tag template identity — fixy path === concurrent path ─────
static_assert(std::is_same_v<
    fsubstr::sharded_calendar_grid::sharded_calendar_tag::Whole<
        probes::V049TestUserTag>,
    cc::sharded_calendar_tag::Whole<probes::V049TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::sharded_calendar_grid::sharded_calendar_tag::Producer<
        probes::V049TestUserTag, 0>,
    cc::sharded_calendar_tag::Producer<probes::V049TestUserTag, 0>>);
static_assert(std::is_same_v<
    fsubstr::sharded_calendar_grid::sharded_calendar_tag::Producer<
        probes::V049TestUserTag, kNumShards - 1>,
    cc::sharded_calendar_tag::Producer<probes::V049TestUserTag,
                                       kNumShards - 1>>);
static_assert(std::is_same_v<
    fsubstr::sharded_calendar_grid::sharded_calendar_tag::Consumer<
        probes::V049TestUserTag, 0>,
    cc::sharded_calendar_tag::Consumer<probes::V049TestUserTag, 0>>);
static_assert(std::is_same_v<
    fsubstr::sharded_calendar_grid::sharded_calendar_tag::Consumer<
        probes::V049TestUserTag, kNumShards - 1>,
    cc::sharded_calendar_tag::Consumer<probes::V049TestUserTag,
                                       kNumShards - 1>>);

// ── 4. Member typedef parity through TestGrid ───────────────────
static_assert(std::is_same_v<typename TestGrid::value_type,
                             std::uint64_t>);
static_assert(std::is_same_v<typename TestGrid::user_tag,
                             probes::V049TestUserTag>);
static_assert(std::is_same_v<typename TestGrid::key_extractor,
                             probes::V049TestKey>);
static_assert(std::is_same_v<typename TestGrid::whole_tag,
    fsubstr::sharded_calendar_grid::sharded_calendar_tag::Whole<
        probes::V049TestUserTag>>);
static_assert(std::is_same_v<
    typename TestGrid::template shard_producer_tag<0>,
    fsubstr::sharded_calendar_grid::sharded_calendar_tag::Producer<
        probes::V049TestUserTag, 0>>);
static_assert(std::is_same_v<
    typename TestGrid::template shard_consumer_tag<kNumShards - 1>,
    fsubstr::sharded_calendar_grid::sharded_calendar_tag::Consumer<
        probes::V049TestUserTag, kNumShards - 1>>);

// ── 5. Static value-template parity ──────────────────────────────
static_assert(TestGrid::num_shards  == kNumShards);
static_assert(TestGrid::num_buckets == kNumBuckets);
static_assert(TestGrid::bucket_cap  == kBucketCap);
static_assert(TestGrid::quantum_ns  == kQuantumNs);

// ── 6. ShardedCalendarGridSessionSurface admits the representative grid
static_assert(fsubstr::sharded_calendar_grid::
    ShardedCalendarGridSessionSurface<TestGrid>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Helper — mint Whole + split into the 2 × kNumShards grid of Linear
// permissions via FOUND-A22 mint_grid_permissions<Whole, N, N>.
static auto fresh_sharded_calendar_perms() {
    auto whole = cs::mint_permission_root<TestGrid::whole_tag>();
    return cs::mint_grid_permissions<TestGrid::whole_tag,
                                     kNumShards, kNumShards>(
        std::move(whole));
}

// Construct + mint the per-shard producer + consumer handles for
// shard 0 and shard 2.  Each handle holds a Linear Permission;
// existence proves the linear token was consumed; size_approx == 0
// on a fresh grid (per-shard view).
static void test_runtime_construct_and_handles() {
    TestGrid grid{};
    auto perms = fresh_sharded_calendar_perms();

    auto p0 = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));
    auto c0 = grid.template consumer<0>(
        std::move(std::get<0>(perms.consumers)));

    if (p0.size_approx() != 0) std::abort();
    if (!c0.empty_approx()) std::abort();
    if (TestGrid::ProducerHandle<0>::capacity() !=
        kNumBuckets * kBucketCap) std::abort();
    if (TestGrid::ConsumerHandle<0>::capacity() !=
        kNumBuckets * kBucketCap) std::abort();

    // Static_assert: shard_index encoded at compile time on the handle.
    static_assert(decltype(p0)::shard_index == 0);
    static_assert(decltype(c0)::shard_index == 0);
}

// Within shard S: push items with monotone keys 0..N-1, drain in
// priority (key) order.  Bucket math under (key < num_buckets,
// quantum_ns == 1, current_bucket == 0) maps item N to bucket N — the
// consumer's forward scan from current_bucket yields items in monotone
// key order = monotone push order.
static void test_runtime_within_shard_priority_order() {
    TestGrid grid{};
    auto perms = fresh_sharded_calendar_perms();

    auto p1 = grid.template producer<1>(
        std::move(std::get<1>(perms.producers)));
    auto c1 = grid.template consumer<1>(
        std::move(std::get<1>(perms.consumers)));

    constexpr int N = 8;  // safely under num_buckets (32)
    // Push in REVERSE order of priority — items 7,6,5,...,0 land in
    // buckets 7,6,5,...,0 respectively (each bucket cap >> 1 so no
    // bucket overflow).  Consumer scans forward from current_bucket=0
    // → should yield 0,1,2,...,7 in priority order.
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

// Cross-shard isolation: pushes to shard A never appear in shard B's
// consumer drain.  Producers on different shards write to disjoint
// per-shard storage; the per-shard priority semantics hold
// independently in each shard.
static void test_runtime_cross_shard_isolation() {
    TestGrid grid{};
    auto perms = fresh_sharded_calendar_perms();

    auto p0 = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));
    auto p2 = grid.template producer<2>(
        std::move(std::get<2>(perms.producers)));
    auto c0 = grid.template consumer<0>(
        std::move(std::get<0>(perms.consumers)));
    auto c2 = grid.template consumer<2>(
        std::move(std::get<2>(perms.consumers)));

    // Shard 0 gets even keys 0,2,4,6; shard 2 gets odd keys 1,3,5,7.
    for (std::uint64_t k = 0; k < 8; k += 2) {
        if (!p0.try_push(k)) std::abort();
    }
    for (std::uint64_t k = 1; k < 8; k += 2) {
        if (!p2.try_push(k)) std::abort();
    }

    // Drain shard 0 — must see ONLY even keys 0,2,4,6 in priority order.
    for (std::uint64_t expected = 0; expected < 8; expected += 2) {
        std::optional<std::uint64_t> r = c0.try_pop();
        if (!r) std::abort();
        if (*r != expected) std::abort();
    }
    if (c0.try_pop()) std::abort();

    // Drain shard 2 — must see ONLY odd keys 1,3,5,7 in priority order.
    for (std::uint64_t expected = 1; expected < 8; expected += 2) {
        std::optional<std::uint64_t> r = c2.try_pop();
        if (!r) std::abort();
        if (*r != expected) std::abort();
    }
    if (c2.try_pop()) std::abort();
}

// Static value templates passthrough (num_shards/num_buckets/bucket_cap/
// quantum_ns) reach runtime unchanged.
static void test_runtime_value_template_constants() {
    static_assert(TestGrid::num_shards == kNumShards);
    static_assert(TestGrid::num_buckets == kNumBuckets);
    static_assert(TestGrid::bucket_cap == kBucketCap);
    static_assert(TestGrid::quantum_ns == kQuantumNs);
    volatile std::size_t n  = TestGrid::num_shards;
    volatile std::size_t b  = TestGrid::num_buckets;
    volatile std::size_t bc = TestGrid::bucket_cap;
    volatile std::uint64_t q = TestGrid::quantum_ns;
    if (n  != kNumShards)  std::abort();
    if (b  != kNumBuckets) std::abort();
    if (bc != kBucketCap)  std::abort();
    if (q  != kQuantumNs)  std::abort();
}

// Substrate-pointer identity — fsubstr::sharded_calendar_grid::
// PermissionedShardedCalendarGrid alias produces EXACTLY the substrate
// type, not a fixy-side wrapper.
static void test_runtime_substrate_identity() {
    static_assert(std::is_same_v<
        TestGrid,
        cc::PermissionedShardedCalendarGrid<
            std::uint64_t, kNumShards, kNumBuckets, kBucketCap,
            probes::V049TestKey, kQuantumNs, probes::V049TestUserTag>>);
    TestGrid grid{};
    cc::PermissionedShardedCalendarGrid<
        std::uint64_t, kNumShards, kNumBuckets, kBucketCap,
        probes::V049TestKey, kQuantumNs, probes::V049TestUserTag>*
            via_sub = &grid;
    TestGrid* via_fixy = via_sub;
    if (via_fixy != via_sub) std::abort();
}

// ProtocolType aliases unchanged from pre-V-049 surface — re-verify
// at runtime scope (compile-time witness already in Substr.h's U-103
// block; this is a runtime-callsite parity rail).
static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::sharded_calendar_grid::ProducerProto<
        std::uint64_t>;
    using FixyCons = fsubstr::sharded_calendar_grid::ConsumerProto<
        std::uint64_t>;
    using SubsProd = ::crucible::safety::proto::
        sharded_calendar_grid_session::ProducerProto<std::uint64_t>;
    using SubsCons = ::crucible::safety::proto::
        sharded_calendar_grid_session::ConsumerProto<std::uint64_t>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

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
