// ── test_fixy_substr_metalog_permissioned_metalog —
// V-051 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/Substr.h` v051:: block under the project's
// warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), plus runtime
// witnesses on top of the compile-time identity sentinels.
//
// Covers the V-051 MetaLog substrate-direct surface additions:
//   * PermissionedMetaLog<UserTag = void>                  — substrate alias
//   * ::crucible::MetaIndex                                — strong-ID re-export
//   * metalog_tag::{Whole, Producer, Consumer}<UserTag>    — tag tree
//   (MetaLogSessionSurface already shipped pre-V-051 as concept template)
//   (mint_metalog_{producer,consumer}[_session] already shipped
//    pre-V-051 via using-decl)
//
// MetaLog structural notes:
//   * EIGHTH cell of the channel-permission family — linear × linear
//     SPSC with FIXED payload (::crucible::TensorMeta) over the
//     production MetaLog buffer.
//   * SHAPE: 1 × 1 (single foreground recording thread appends; single
//     background drain thread reads).  No grid, no key-priority.
//   * Substrate decorates an externally-owned ::crucible::MetaLog
//     reference rather than owning storage — the PermissionedMetaLog
//     instance is just a reference + EBO-collapsed Permission tokens
//     on handles.
//   * Producer/Consumer split via the standard CSL frame rule —
//     mint_permission_root<whole_tag> then
//     mint_permission_split<producer_tag, consumer_tag>(whole).
//   * V-051-unique cardinality bump: the strong-ID return type
//     ::crucible::MetaIndex (returned by ProducerHandle::try_append).
//     Unlike V-045..V-050 substrates which surface a per-substrate
//     payload-type predicate (SpscValue / MpmcValue / DequeValue /
//     KeyExtractorOf / ShardedCalendarKeyExtractorOf), MetaLog has no
//     such concept — value_type is hard-coded to TensorMeta.

#include <crucible/fixy/Substr.h>

#include <crucible/MetaLog.h>
#include <crucible/Types.h>
#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/permissions/Permission.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc      = ::crucible::concurrent;
namespace cs      = ::crucible::safety;

// ═══════════════════════════════════════════════════════════════════
// ── Synthetic UserTag for V-051 fixtures ─────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

// TU-local tag so PermissionedMetaLog.h's specializations pick up a
// fresh (Whole, Producer, Consumer) triple via the UserTag-
// parameterized templates.
struct V051TestUserTag {};

}  // namespace probes

// ═══════════════════════════════════════════════════════════════════
// ── Fixture aliases ──────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

using TestLog =
    fsubstr::metalog::PermissionedMetaLog<probes::V051TestUserTag>;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════

// ── 1. Substrate alias identity ─────────────────────────────────
static_assert(std::is_same_v<
    TestLog,
    cc::PermissionedMetaLog<probes::V051TestUserTag>>,
    "fixy::substr::metalog::PermissionedMetaLog must alias the substrate.");

// ── 2. MetaIndex strong-ID identity (V-051-unique cardinality bump)
static_assert(std::is_same_v<
    fsubstr::metalog::MetaIndex,
    ::crucible::MetaIndex>,
    "fixy::substr::metalog::MetaIndex must alias ::crucible::MetaIndex.");

// ── 3. Tag tree identity ────────────────────────────────────────
static_assert(std::is_same_v<
    fsubstr::metalog::metalog_tag::Whole<probes::V051TestUserTag>,
    cc::metalog_tag::Whole<probes::V051TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::metalog::metalog_tag::Producer<probes::V051TestUserTag>,
    cc::metalog_tag::Producer<probes::V051TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::metalog::metalog_tag::Consumer<probes::V051TestUserTag>,
    cc::metalog_tag::Consumer<probes::V051TestUserTag>>);

// ── 4. Tag identity propagates through member typedefs ───────────
static_assert(std::is_same_v<
    typename TestLog::whole_tag,
    fsubstr::metalog::metalog_tag::Whole<probes::V051TestUserTag>>);
static_assert(std::is_same_v<
    typename TestLog::producer_tag,
    fsubstr::metalog::metalog_tag::Producer<probes::V051TestUserTag>>);
static_assert(std::is_same_v<
    typename TestLog::consumer_tag,
    fsubstr::metalog::metalog_tag::Consumer<probes::V051TestUserTag>>);

// ── 5. MetaLogSessionSurface admits the representative log.
static_assert(
    fsubstr::metalog::MetaLogSessionSurface<TestLog>);

// ── 6. value_type identity — fixed to TensorMeta by substrate.
static_assert(std::is_same_v<
    typename TestLog::value_type,
    ::crucible::TensorMeta>);
static_assert(std::is_same_v<
    typename TestLog::value_type,
    fsubstr::metalog::MetaLogRecord>);

// ── 7. Protocol aliases preserved (pre-existing re-exports).
static_assert(std::is_same_v<
    fsubstr::metalog::ProducerProto,
    ::crucible::safety::proto::metalog_session::ProducerProto>);
static_assert(std::is_same_v<
    fsubstr::metalog::ConsumerProto,
    ::crucible::safety::proto::metalog_session::ConsumerProto>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime fixture helpers ──────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace {

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_TEST_REQUIRE(cond)                                  \
    do {                                                              \
        if (!(cond)) {                                                \
            std::fprintf(stderr,                                      \
                "  REQUIRE FAILED: %s @ %s:%d\n",                     \
                #cond, __FILE__, __LINE__);                           \
            ++total_failed;                                           \
            return;                                                   \
        }                                                             \
    } while (0)

template <typename Body>
void run_test(const char* name, Body body) {
    std::fprintf(stderr, "  %s ... ", name);
    int before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        std::fprintf(stderr, "OK\n");
    } else {
        std::fprintf(stderr, "FAILED\n");
    }
}

[[nodiscard]] ::crucible::TensorMeta make_meta(std::int64_t id) {
    ::crucible::TensorMeta meta{};
    meta.sizes[0] = ::crucible::tensor_dim(id);
    meta.strides[0] = ::crucible::tensor_dim(1);
    meta.ndim = 1;
    meta.dtype = ::crucible::ScalarType::Float;
    meta.device_type = ::crucible::DeviceType::CPU;
    meta.device_idx = -1;
    meta.storage_nbytes = static_cast<std::uint32_t>(id * 16);
    meta.version = static_cast<std::uint32_t>(id);
    return meta;
}

[[nodiscard]] bool same_meta(const ::crucible::TensorMeta& a,
                             const ::crucible::TensorMeta& b) {
    return ::crucible::raw_tensor_dim(a.sizes[0]) ==
               ::crucible::raw_tensor_dim(b.sizes[0])
        && ::crucible::raw_tensor_dim(a.strides[0]) ==
               ::crucible::raw_tensor_dim(b.strides[0])
        && a.ndim == b.ndim
        && a.dtype == b.dtype
        && a.device_type == b.device_type
        && a.device_idx == b.device_idx
        && a.storage_nbytes == b.storage_nbytes
        && a.version == b.version;
}

// Mint a fresh (Whole → Producer + Consumer) triple via the fixy::
// substrate-side tag aliases.  Linear × linear SPSC split (NOT a
// grid), so the standard mint_permission_split rail applies.
[[nodiscard]] auto fresh_metalog_handles(::crucible::MetaLog& raw_log) {
    TestLog log{raw_log};
    auto whole = cs::mint_permission_root<TestLog::whole_tag>();
    auto [prod_perm, cons_perm] =
        cs::mint_permission_split<TestLog::producer_tag,
                                  TestLog::consumer_tag>(std::move(whole));
    return std::pair{
        log.producer(std::move(prod_perm)),
        log.consumer(std::move(cons_perm))
    };
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Construct PermissionedMetaLog through fixy:: alias, mint linear
// Producer / Consumer permissions through the fixy:: tag tree, and
// derive handles.  Verifies the surface composes end-to-end through
// fixy:: name lookups (no descent into ::crucible::concurrent:: at
// the caller site).
static void test_runtime_construct_and_handles() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = fresh_metalog_handles(raw_log);
    (void)producer;
    (void)consumer;

    // Sanity: handle size is pointer-sized (Permission EBO-collapses).
    static_assert(sizeof(TestLog::ProducerHandle) ==
                  sizeof(::crucible::MetaLog*));
    static_assert(sizeof(TestLog::ConsumerHandle) ==
                  sizeof(::crucible::MetaLog*));
}

// Linear append → drain SPSC round trip through fixy:: surface.
// Single record case — exercises ProducerHandle::try_append_one and
// ConsumerHandle::try_drain_one through the fixy:: alias chain.
static void test_runtime_single_append_drain() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = fresh_metalog_handles(raw_log);

    const ::crucible::TensorMeta source = make_meta(42);
    const bool appended = producer.try_append_one(source);
    CRUCIBLE_TEST_REQUIRE(appended);

    auto drained = consumer.try_drain_one();
    CRUCIBLE_TEST_REQUIRE(drained.has_value());
    CRUCIBLE_TEST_REQUIRE(same_meta(*drained, source));

    auto empty = consumer.try_drain_one();
    CRUCIBLE_TEST_REQUIRE(!empty.has_value());
}

// Bulk append via try_append (returns MetaIndex), partial drain via
// drain(body, max_items).  Exercises the V-051-unique MetaIndex
// strong-ID return path AND the partial-drain semantic.
static void test_runtime_bulk_append_partial_drain() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = fresh_metalog_handles(raw_log);

    const std::array<::crucible::TensorMeta, 5> source{
        make_meta(1), make_meta(2), make_meta(3),
        make_meta(4), make_meta(5),
    };

    // Bulk append returns MetaIndex of the first record (V-051-
    // unique strong-ID return type).
    const ::crucible::MetaIndex start = producer.try_append(
        source.data(), static_cast<std::uint32_t>(source.size()));
    CRUCIBLE_TEST_REQUIRE(start.is_valid());
    CRUCIBLE_TEST_REQUIRE(start.raw() == 0);

    // Drain the first 3 only.
    std::vector<::crucible::TensorMeta> first_three;
    const std::uint32_t drained_count = consumer.drain(
        [&](const ::crucible::TensorMeta& meta) {
            first_three.push_back(meta);
        },
        /*max_items=*/3);
    CRUCIBLE_TEST_REQUIRE(drained_count == 3);
    CRUCIBLE_TEST_REQUIRE(first_three.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CRUCIBLE_TEST_REQUIRE(same_meta(first_three[i], source[i]));
    }

    // Remaining 2 still in the log.
    CRUCIBLE_TEST_REQUIRE(consumer.size_approx() == 2);

    // Drain the rest with no max.
    std::vector<::crucible::TensorMeta> rest;
    const std::uint32_t remaining = consumer.drain(
        [&](const ::crucible::TensorMeta& meta) {
            rest.push_back(meta);
        });
    CRUCIBLE_TEST_REQUIRE(remaining == 2);
    CRUCIBLE_TEST_REQUIRE(same_meta(rest[0], source[3]));
    CRUCIBLE_TEST_REQUIRE(same_meta(rest[1], source[4]));
    CRUCIBLE_TEST_REQUIRE(consumer.size_approx() == 0);
}

// MetaIndex returned by try_append advances monotonically across
// successive bulk appends — the strong-ID type is the canonical
// V-051 cardinality-bump witness.
static void test_runtime_metaindex_propagates() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = fresh_metalog_handles(raw_log);

    const std::array<::crucible::TensorMeta, 2> first{
        make_meta(10), make_meta(20),
    };
    const std::array<::crucible::TensorMeta, 3> second{
        make_meta(30), make_meta(40), make_meta(50),
    };

    const ::crucible::MetaIndex idx_first = producer.try_append(
        first.data(), static_cast<std::uint32_t>(first.size()));
    CRUCIBLE_TEST_REQUIRE(idx_first.is_valid());
    CRUCIBLE_TEST_REQUIRE(idx_first.raw() == 0);

    const ::crucible::MetaIndex idx_second = producer.try_append(
        second.data(), static_cast<std::uint32_t>(second.size()));
    CRUCIBLE_TEST_REQUIRE(idx_second.is_valid());
    // Second append's start MetaIndex == first count.
    CRUCIBLE_TEST_REQUIRE(idx_second.raw() == first.size());

    // Verify accessing through ConsumerHandle::at with MetaIndex
    // returns the recorded record.
    const ::crucible::TensorMeta& at_first =
        consumer.at(::crucible::MetaIndex{0});
    CRUCIBLE_TEST_REQUIRE(same_meta(at_first, first[0]));

    const ::crucible::TensorMeta& at_third =
        consumer.at(::crucible::MetaIndex{2});
    CRUCIBLE_TEST_REQUIRE(same_meta(at_third, second[0]));
}

// Empty-drain semantics — drain on a fresh log returns 0, advances
// no tail.  Sanity witness for the surface-level Consumer interface.
static void test_runtime_empty_drain() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = fresh_metalog_handles(raw_log);
    (void)producer;

    std::vector<::crucible::TensorMeta> collected;
    const std::uint32_t count = consumer.drain(
        [&](const ::crucible::TensorMeta& meta) {
            collected.push_back(meta);
        });
    CRUCIBLE_TEST_REQUIRE(count == 0);
    CRUCIBLE_TEST_REQUIRE(collected.empty());
    CRUCIBLE_TEST_REQUIRE(consumer.size_approx() == 0);
    CRUCIBLE_TEST_REQUIRE(consumer.tail_index() == 0);
    CRUCIBLE_TEST_REQUIRE(consumer.head_index() == 0);
}

// Identity sanity at runtime — TestLog::value_type IS TensorMeta,
// member typedef IS the same TensorMeta exposed via the fixy::
// alias MetaLogRecord, and protocol aliases are unchanged.
static void test_runtime_substrate_identity() {
    static_assert(std::is_same_v<
        typename TestLog::value_type,
        ::crucible::TensorMeta>);
    static_assert(std::is_same_v<
        typename TestLog::value_type,
        fsubstr::metalog::MetaLogRecord>);

    // PermissionedMetaLog is Pinned (delete copy/move) — verify via
    // type-trait predicates.
    static_assert(!std::is_copy_constructible_v<TestLog>);
    static_assert(!std::is_move_constructible_v<TestLog>);
    static_assert(!std::is_copy_assignable_v<TestLog>);
    static_assert(!std::is_move_assignable_v<TestLog>);
}

int main() {
    std::fprintf(stderr,
        "test_fixy_substr_metalog_permissioned_metalog: "
        "starting V-051 runtime witnesses\n");

    run_test("construct_and_handles",
             test_runtime_construct_and_handles);
    run_test("single_append_drain",
             test_runtime_single_append_drain);
    run_test("bulk_append_partial_drain",
             test_runtime_bulk_append_partial_drain);
    run_test("metaindex_propagates",
             test_runtime_metaindex_propagates);
    run_test("empty_drain",
             test_runtime_empty_drain);
    run_test("substrate_identity",
             test_runtime_substrate_identity);

    std::fprintf(stderr,
        "test_fixy_substr_metalog_permissioned_metalog: "
        "%d/%d runtime witnesses passed\n",
        total_passed, total_passed + total_failed);

    return total_failed == 0 ? 0 : 1;
}
