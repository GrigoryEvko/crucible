// ── test_fixy_substr_calendar_permissioned_calendar_grid —
// V-050 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/Substr.h` v050:: block under the project's
// warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), plus runtime
// witnesses on top of the compile-time identity sentinels.
//
// Covers the V-050 CalendarGrid substrate-direct surface additions:
//   * PermissionedCalendarGrid<T, NumProducers, NumBuckets,
//                              BucketCap, KeyExtractor, QuantumNs,
//                              UserTag>                      — substrate alias
//   * KeyExtractorOf<E, T>                                   — concept re-export
//   * calendar_tag::{Whole, Producer<P>, Consumer}<UserTag>  — tag tree
//   (CalendarGridSessionSurface already shipped pre-V-050)
//   (mint_calendar_grid_producer / mint_calendar_grid_consumer
//    / mint_producer_session / mint_consumer_session already shipped
//    pre-V-050 via using-decl)
//
// CalendarGrid structural notes (vs V-049 ShardedCalendarGrid):
//   * SEVENTH cell of the channel-permission family — linear-row ×
//     linear-singleton with KEY-PRIORITY semantics.
//   * SHAPE: NumProducers × 1 (M producers feed the SAME calendar
//     grid; single consumer drains globally-ordered priority).
//   * V-049 ShardedCalendarGrid uses NumShards × NumShards (per-shard
//     producer↔consumer pairs, no cross-shard ordering); V-050 trades
//     the V-049 same-core push optimization for GLOBAL priority order.
//   * Producer handles are statically indexed (ProducerHandle<P>);
//     consumer handle is singleton (ConsumerHandle, no template index).
//   * NumProducers+1 permissions descend from one Whole<UserTag>
//     root via mint_grid_permissions<Whole, NumProducers, 1>.

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/PermissionedCalendarGrid.h>
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
// ── Synthetic UserTag + KeyExtractor for V-050 fixtures ──────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

// TU-local tag so PermissionedCalendarGrid.h's generic specialization
// picks up the fresh (Whole, Producer<P>, Consumer) triple via the
// UserTag-parameterized templates.
struct V050TestUserTag {};

// KeyExtractor: the item's value IS its priority key.  Producer push
// of item N lands in bucket N (modulo NumBuckets) on a freshly-
// constructed grid (current_bucket == 0 means no clamping).
struct V050TestKey {
    static std::uint64_t key(std::uint64_t v) noexcept { return v; }
};

}  // namespace probes

// ═══════════════════════════════════════════════════════════════════
// ── Fixture parameters ──────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

constexpr std::size_t   kNumProducers = 4;
constexpr std::size_t   kNumBuckets   = 32;
constexpr std::size_t   kBucketCap    = 16;
constexpr std::uint64_t kQuantumNs    = 1ULL;

using TestGrid =
    fsubstr::calendar_grid::PermissionedCalendarGrid<
        std::uint64_t, kNumProducers, kNumBuckets, kBucketCap,
        probes::V050TestKey, kQuantumNs, probes::V050TestUserTag>;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════

// ── 1. Substrate alias identity ─────────────────────────────────
static_assert(std::is_same_v<
    TestGrid,
    cc::PermissionedCalendarGrid<
        std::uint64_t, kNumProducers, kNumBuckets, kBucketCap,
        probes::V050TestKey, kQuantumNs, probes::V050TestUserTag>>,
    "fixy::substr::calendar_grid::PermissionedCalendarGrid "
    "must alias the substrate.");

// ── 2. KeyExtractorOf concept admission parity ───────────────────
static_assert(fsubstr::calendar_grid::
    KeyExtractorOf<probes::V050TestKey, std::uint64_t>);
static_assert(fsubstr::calendar_grid::
    KeyExtractorOf<probes::V050TestKey, std::uint64_t> ==
    cc::KeyExtractorOf<probes::V050TestKey, std::uint64_t>);
// Negative — a type with no `static uint64_t key(const T&) noexcept`
// fails the concept on BOTH paths.
struct NonCalendarKey {};
static_assert(!fsubstr::calendar_grid::
    KeyExtractorOf<NonCalendarKey, std::uint64_t>);
static_assert(!cc::KeyExtractorOf<NonCalendarKey, std::uint64_t>);

// ── 3. Tag template identity — fixy path === concurrent path ─────
static_assert(std::is_same_v<
    fsubstr::calendar_grid::calendar_tag::Whole<probes::V050TestUserTag>,
    cc::calendar_tag::Whole<probes::V050TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::calendar_grid::calendar_tag::Producer<
        probes::V050TestUserTag, 0>,
    cc::calendar_tag::Producer<probes::V050TestUserTag, 0>>);
static_assert(std::is_same_v<
    fsubstr::calendar_grid::calendar_tag::Producer<
        probes::V050TestUserTag, kNumProducers - 1>,
    cc::calendar_tag::Producer<probes::V050TestUserTag, kNumProducers - 1>>);
static_assert(std::is_same_v<
    fsubstr::calendar_grid::calendar_tag::Consumer<probes::V050TestUserTag>,
    cc::calendar_tag::Consumer<probes::V050TestUserTag>>);

// ── 4. Member typedef parity through TestGrid ───────────────────
static_assert(std::is_same_v<typename TestGrid::value_type,
                             std::uint64_t>);
static_assert(std::is_same_v<typename TestGrid::user_tag,
                             probes::V050TestUserTag>);
static_assert(std::is_same_v<typename TestGrid::key_extractor,
                             probes::V050TestKey>);
static_assert(std::is_same_v<typename TestGrid::whole_tag,
    fsubstr::calendar_grid::calendar_tag::Whole<probes::V050TestUserTag>>);
static_assert(std::is_same_v<typename TestGrid::consumer_tag,
    fsubstr::calendar_grid::calendar_tag::Consumer<probes::V050TestUserTag>>);
static_assert(std::is_same_v<
    typename TestGrid::template producer_tag<0>,
    fsubstr::calendar_grid::calendar_tag::Producer<
        probes::V050TestUserTag, 0>>);
static_assert(std::is_same_v<
    typename TestGrid::template producer_tag<kNumProducers - 1>,
    fsubstr::calendar_grid::calendar_tag::Producer<
        probes::V050TestUserTag, kNumProducers - 1>>);

// ── 5. Static value-template parity ──────────────────────────────
static_assert(TestGrid::num_producers == kNumProducers);
static_assert(TestGrid::num_buckets   == kNumBuckets);
static_assert(TestGrid::bucket_cap    == kBucketCap);
static_assert(TestGrid::quantum_ns    == kQuantumNs);

// ── 6. CalendarGridSessionSurface admits the representative grid
static_assert(fsubstr::calendar_grid::
    CalendarGridSessionSurface<TestGrid>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Helper — mint Whole + split into M producer + 1 consumer Linear
// permissions via FOUND-A22 mint_grid_permissions<Whole, M, 1>.
static auto fresh_calendar_perms() {
    auto whole = cs::mint_permission_root<TestGrid::whole_tag>();
    return cs::mint_grid_permissions<TestGrid::whole_tag,
                                     kNumProducers, 1>(std::move(whole));
}

// Construct + mint per-producer ProducerHandle<P> + single ConsumerHandle.
// Each handle holds a Linear Permission EBO-collapsed to zero bytes;
// existence proves the linear token was consumed.
static void test_runtime_construct_and_handles() {
    TestGrid grid{};
    auto perms = fresh_calendar_perms();

    auto p0 = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));
    auto p3 = grid.template producer<3>(
        std::move(std::get<3>(perms.producers)));
    auto c  = grid.consumer(std::move(std::get<0>(perms.consumers)));
    (void)c;

    // Static_assert: row index encoded at compile time on the
    // producer handle.
    static_assert(decltype(p0)::row_index == 0);
    static_assert(decltype(p3)::row_index == 3);
}

// Within-grid priority order from ONE producer: push items with
// monotone keys 0..N-1 in REVERSE, drain in priority (key) order.
// Bucket math under (key < num_buckets, quantum_ns == 1,
// current_bucket == 0) maps item N to bucket N; consumer's forward
// scan from current_bucket yields items in monotone key order.
static void test_runtime_single_producer_priority_order() {
    TestGrid grid{};
    auto perms = fresh_calendar_perms();

    auto p0 = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));
    auto c  = grid.consumer(std::move(std::get<0>(perms.consumers)));

    constexpr int N = 8;  // safely under num_buckets (32)
    // Push items 7,6,5,...,0 — should drain as 0,1,2,...,7.
    for (int i = N - 1; i >= 0; --i) {
        if (!p0.try_push(static_cast<std::uint64_t>(i))) std::abort();
    }
    for (int expected = 0; expected < N; ++expected) {
        std::optional<std::uint64_t> r = c.try_pop();
        if (!r) std::abort();
        if (*r != static_cast<std::uint64_t>(expected)) std::abort();
    }
    if (c.try_pop()) std::abort();
}

// CROSS-PRODUCER GLOBAL MERGE — the key V-050-vs-V-049 distinction.
// Producer<0> pushes even keys 0,2,4,6 and producer<1> pushes odd
// keys 1,3,5,7 — items from BOTH producers land in the SAME calendar
// grid (NOT separate per-shard grids).  The single consumer drains
// the merged stream in global monotone key order: 0,1,2,3,4,5,6,7.
static void test_runtime_multi_producer_global_merge() {
    TestGrid grid{};
    auto perms = fresh_calendar_perms();

    auto p0 = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(
        std::move(std::get<1>(perms.producers)));
    auto c  = grid.consumer(std::move(std::get<0>(perms.consumers)));

    // Producer 0 pushes evens; producer 1 pushes odds.  Interleave
    // pushes so the test catches any per-producer FIFO bias.
    for (std::uint64_t k = 0; k < 8; ++k) {
        if (k % 2 == 0) {
            if (!p0.try_push(k)) std::abort();
        } else {
            if (!p1.try_push(k)) std::abort();
        }
    }

    // Drain — must see globally-monotone 0,1,2,3,4,5,6,7.  This is
    // EXACTLY the property V-049 ShardedCalendarGrid does NOT
    // provide (its per-shard isolation deliberately trades global
    // ordering for cross-thread atomic-read elimination).
    for (std::uint64_t expected = 0; expected < 8; ++expected) {
        std::optional<std::uint64_t> r = c.try_pop();
        if (!r) std::abort();
        if (*r != expected) std::abort();
    }
    if (c.try_pop()) std::abort();
}

// Static value templates passthrough — num_producers / num_buckets /
// bucket_cap / quantum_ns reach runtime unchanged.
static void test_runtime_value_template_constants() {
    static_assert(TestGrid::num_producers == kNumProducers);
    static_assert(TestGrid::num_buckets == kNumBuckets);
    static_assert(TestGrid::bucket_cap == kBucketCap);
    static_assert(TestGrid::quantum_ns == kQuantumNs);
    volatile std::size_t n  = TestGrid::num_producers;
    volatile std::size_t b  = TestGrid::num_buckets;
    volatile std::size_t bc = TestGrid::bucket_cap;
    volatile std::uint64_t q = TestGrid::quantum_ns;
    if (n  != kNumProducers) std::abort();
    if (b  != kNumBuckets)   std::abort();
    if (bc != kBucketCap)    std::abort();
    if (q  != kQuantumNs)    std::abort();
}

// Substrate-pointer identity — fsubstr::calendar_grid::
// PermissionedCalendarGrid alias produces EXACTLY the substrate type,
// not a fixy-side wrapper.
static void test_runtime_substrate_identity() {
    static_assert(std::is_same_v<
        TestGrid,
        cc::PermissionedCalendarGrid<
            std::uint64_t, kNumProducers, kNumBuckets, kBucketCap,
            probes::V050TestKey, kQuantumNs, probes::V050TestUserTag>>);
    TestGrid grid{};
    cc::PermissionedCalendarGrid<
        std::uint64_t, kNumProducers, kNumBuckets, kBucketCap,
        probes::V050TestKey, kQuantumNs, probes::V050TestUserTag>*
            via_sub = &grid;
    TestGrid* via_fixy = via_sub;
    if (via_fixy != via_sub) std::abort();
}

// ProtocolType aliases unchanged from pre-V-050 surface — re-verify
// at runtime scope (compile-time witness already in Substr.h's U-103
// block; this is a runtime-callsite parity rail).
static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::calendar_grid::ProducerProto<
        std::uint64_t>;
    using FixyCons = fsubstr::calendar_grid::ConsumerProto<
        std::uint64_t>;
    using SubsProd = ::crucible::safety::proto::
        calendar_grid_session::ProducerProto<std::uint64_t>;
    using SubsCons = ::crucible::safety::proto::
        calendar_grid_session::ConsumerProto<std::uint64_t>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

int main() {
    test_runtime_construct_and_handles();
    test_runtime_single_producer_priority_order();
    test_runtime_multi_producer_global_merge();
    test_runtime_value_template_constants();
    test_runtime_substrate_identity();
    test_runtime_protocol_aliases_unchanged();
    std::printf("test_fixy_substr_calendar_permissioned_calendar_grid: "
                "6/6 runtime witnesses passed\n");
    return 0;
}
