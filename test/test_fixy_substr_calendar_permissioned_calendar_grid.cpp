// The compile-time witnesses below are re-stated at translation-unit
// scope on purpose.  A static_assert that lives only in a header is
// never evaluated under the project warning flags until some
// translation unit includes that header.

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
namespace cc = ::crucible::concurrent;
namespace cs = ::crucible::safety;

namespace probes {

// A tag private to this translation unit gives the fixture its own
// (Whole, Producer<P>, Consumer) triple, disjoint from every other
// user of the substrate.
struct V050TestUserTag {};

// Value-as-key, so a push of item N lands in bucket N modulo
// NumBuckets while the grid is fresh and current_bucket is still 0.
struct V050TestKey {
    static std::uint64_t key(std::uint64_t v) noexcept { return v; }
};

}  // namespace probes

constexpr std::size_t kNumProducers = 4;
constexpr std::size_t kNumBuckets = 32;
constexpr std::size_t kBucketCap = 16;
constexpr std::uint64_t kQuantumNs = 1ULL;

using TestGrid =
    fsubstr::calendar_grid::PermissionedCalendarGrid<std::uint64_t, kNumProducers, kNumBuckets, kBucketCap,
                                                     probes::V050TestKey, kQuantumNs, probes::V050TestUserTag>;

static_assert(
    std::is_same_v<TestGrid, cc::PermissionedCalendarGrid<std::uint64_t, kNumProducers, kNumBuckets, kBucketCap,
                                                          probes::V050TestKey, kQuantumNs, probes::V050TestUserTag>>,
    "fixy::substr::calendar_grid::PermissionedCalendarGrid "
    "must alias the substrate.");

static_assert(fsubstr::calendar_grid::KeyExtractorOf<probes::V050TestKey, std::uint64_t>);
static_assert(fsubstr::calendar_grid::KeyExtractorOf<probes::V050TestKey, std::uint64_t>
              == cc::KeyExtractorOf<probes::V050TestKey, std::uint64_t>);
struct NonCalendarKey {};
static_assert(!fsubstr::calendar_grid::KeyExtractorOf<NonCalendarKey, std::uint64_t>);
static_assert(!cc::KeyExtractorOf<NonCalendarKey, std::uint64_t>);

static_assert(std::is_same_v<fsubstr::calendar_grid::calendar_tag::Whole<probes::V050TestUserTag>,
                             cc::calendar_tag::Whole<probes::V050TestUserTag>>);
static_assert(std::is_same_v<fsubstr::calendar_grid::calendar_tag::Producer<probes::V050TestUserTag, 0>,
                             cc::calendar_tag::Producer<probes::V050TestUserTag, 0>>);
static_assert(std::is_same_v<fsubstr::calendar_grid::calendar_tag::Producer<probes::V050TestUserTag, kNumProducers - 1>,
                             cc::calendar_tag::Producer<probes::V050TestUserTag, kNumProducers - 1>>);
static_assert(std::is_same_v<fsubstr::calendar_grid::calendar_tag::Consumer<probes::V050TestUserTag>,
                             cc::calendar_tag::Consumer<probes::V050TestUserTag>>);

static_assert(std::is_same_v<typename TestGrid::value_type, std::uint64_t>);
static_assert(std::is_same_v<typename TestGrid::user_tag, probes::V050TestUserTag>);
static_assert(std::is_same_v<typename TestGrid::key_extractor, probes::V050TestKey>);
static_assert(
    std::is_same_v<typename TestGrid::whole_tag, fsubstr::calendar_grid::calendar_tag::Whole<probes::V050TestUserTag>>);
static_assert(std::is_same_v<typename TestGrid::consumer_tag,
                             fsubstr::calendar_grid::calendar_tag::Consumer<probes::V050TestUserTag>>);
static_assert(std::is_same_v<typename TestGrid::template producer_tag<0>,
                             fsubstr::calendar_grid::calendar_tag::Producer<probes::V050TestUserTag, 0>>);
static_assert(
    std::is_same_v<typename TestGrid::template producer_tag<kNumProducers - 1>,
                   fsubstr::calendar_grid::calendar_tag::Producer<probes::V050TestUserTag, kNumProducers - 1>>);

static_assert(TestGrid::num_producers == kNumProducers);
static_assert(TestGrid::num_buckets == kNumBuckets);
static_assert(TestGrid::bucket_cap == kBucketCap);
static_assert(TestGrid::quantum_ns == kQuantumNs);

static_assert(fsubstr::calendar_grid::CalendarGridSessionSurface<TestGrid>);

static auto fresh_calendar_perms() {
    auto whole = cs::mint_permission_root<TestGrid::whole_tag>();
    return cs::mint_grid_permissions<TestGrid::whole_tag, kNumProducers, 1>(std::move(whole));
}

// A handle carries no storage, so its construction is the whole claim:
// each one consumed the linear permission it was handed.
static void test_runtime_construct_and_handles() {
    TestGrid grid{};
    auto perms = fresh_calendar_perms();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto c = grid.consumer(std::move(std::get<0>(perms.consumers)));
    (void)c;

    static_assert(decltype(p0)::row_index == 0);
    static_assert(decltype(p3)::row_index == 3);
}

// Push order is the reverse of the expected drain order, so a queue
// that merely preserved arrival order would fail every comparison.
// The expectation holds because key < num_buckets and quantum_ns == 1
// put item N in bucket N, and the consumer scans buckets forward from
// current_bucket.
static void test_runtime_single_producer_priority_order() {
    TestGrid grid{};
    auto perms = fresh_calendar_perms();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto c = grid.consumer(std::move(std::get<0>(perms.consumers)));

    constexpr int N = 8;  // under num_buckets, so no key wraps
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

// Both producers feed one grid, not one grid each.  Producer 0 pushes
// the even keys and producer 1 the odd keys, so a globally monotone
// drain is only possible if the two producers share a bucket array.
static void test_runtime_multi_producer_global_merge() {
    TestGrid grid{};
    auto perms = fresh_calendar_perms();

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto c = grid.consumer(std::move(std::get<0>(perms.consumers)));

    // The two producers interleave, so a per-producer arrival bias in
    // the merge would surface as an ordering failure below.
    for (std::uint64_t k = 0; k < 8; ++k) {
        if (k % 2 == 0) {
            if (!p0.try_push(k)) std::abort();
        } else {
            if (!p1.try_push(k)) std::abort();
        }
    }

    for (std::uint64_t expected = 0; expected < 8; ++expected) {
        std::optional<std::uint64_t> r = c.try_pop();
        if (!r) std::abort();
        if (*r != expected) std::abort();
    }
    if (c.try_pop()) std::abort();
}

// The volatile reads defeat constant folding, so the constants are
// checked as they reach runtime and not only as they were written.
static void test_runtime_value_template_constants() {
    static_assert(TestGrid::num_producers == kNumProducers);
    static_assert(TestGrid::num_buckets == kNumBuckets);
    static_assert(TestGrid::bucket_cap == kBucketCap);
    static_assert(TestGrid::quantum_ns == kQuantumNs);
    volatile std::size_t n = TestGrid::num_producers;
    volatile std::size_t b = TestGrid::num_buckets;
    volatile std::size_t bc = TestGrid::bucket_cap;
    volatile std::uint64_t q = TestGrid::quantum_ns;
    if (n != kNumProducers) std::abort();
    if (b != kNumBuckets) std::abort();
    if (bc != kBucketCap) std::abort();
    if (q != kQuantumNs) std::abort();
}

static void test_runtime_substrate_identity() {
    static_assert(
        std::is_same_v<TestGrid,
                       cc::PermissionedCalendarGrid<std::uint64_t, kNumProducers, kNumBuckets, kBucketCap,
                                                    probes::V050TestKey, kQuantumNs, probes::V050TestUserTag>>);
    TestGrid grid{};
    cc::PermissionedCalendarGrid<std::uint64_t, kNumProducers, kNumBuckets, kBucketCap, probes::V050TestKey, kQuantumNs,
                                 probes::V050TestUserTag>* via_sub = &grid;
    TestGrid* via_fixy = via_sub;
    if (via_fixy != via_sub) std::abort();
}

static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::calendar_grid::ProducerProto<std::uint64_t>;
    using FixyCons = fsubstr::calendar_grid::ConsumerProto<std::uint64_t>;
    using SubsProd = ::crucible::safety::proto::calendar_grid_session::ProducerProto<std::uint64_t>;
    using SubsCons = ::crucible::safety::proto::calendar_grid_session::ConsumerProto<std::uint64_t>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

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
