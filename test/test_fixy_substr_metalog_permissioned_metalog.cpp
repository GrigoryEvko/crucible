// What is under test is the re-export, not the log.  Including the
// umbrella header is part of the claim, because the static_asserts it
// carries are never compiled under the project warning flags until some
// translation unit pulls it in.

#include <crucible/fixy/Substr.h>

#include <crucible/MetaLog.h>
#include <crucible/Types.h>
#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/permissions/_Permission.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc = ::crucible::concurrent;
namespace cs = ::crucible::safety;

namespace probes {

// A tag local to this file gives the templates a fresh whole, producer
// and consumer triple that no other translation unit shares.
struct V051TestUserTag {};

}  // namespace probes

using TestLog = fsubstr::metalog::PermissionedMetaLog<probes::V051TestUserTag>;

static_assert(std::is_same_v<TestLog, cc::PermissionedMetaLog<probes::V051TestUserTag>>,
              "fixy::substr::metalog::PermissionedMetaLog must alias the substrate.");

static_assert(std::is_same_v<fsubstr::metalog::MetaIndex, ::crucible::MetaIndex>,
              "fixy::substr::metalog::MetaIndex must alias ::crucible::MetaIndex.");

static_assert(std::is_same_v<fsubstr::metalog::metalog_tag::Whole<probes::V051TestUserTag>,
                             cc::metalog_tag::Whole<probes::V051TestUserTag>>);
static_assert(std::is_same_v<fsubstr::metalog::metalog_tag::Producer<probes::V051TestUserTag>,
                             cc::metalog_tag::Producer<probes::V051TestUserTag>>);
static_assert(std::is_same_v<fsubstr::metalog::metalog_tag::Consumer<probes::V051TestUserTag>,
                             cc::metalog_tag::Consumer<probes::V051TestUserTag>>);

static_assert(
    std::is_same_v<typename TestLog::whole_tag, fsubstr::metalog::metalog_tag::Whole<probes::V051TestUserTag>>);
static_assert(
    std::is_same_v<typename TestLog::producer_tag, fsubstr::metalog::metalog_tag::Producer<probes::V051TestUserTag>>);
static_assert(
    std::is_same_v<typename TestLog::consumer_tag, fsubstr::metalog::metalog_tag::Consumer<probes::V051TestUserTag>>);

static_assert(fsubstr::metalog::MetaLogSessionSurface<TestLog>);

static_assert(std::is_same_v<typename TestLog::value_type, ::crucible::TensorMeta>);
static_assert(std::is_same_v<typename TestLog::value_type, fsubstr::metalog::MetaLogRecord>);

static_assert(
    std::is_same_v<fsubstr::metalog::ProducerProto, ::crucible::safety::proto::metalog_session::ProducerProto>);
static_assert(
    std::is_same_v<fsubstr::metalog::ConsumerProto, ::crucible::safety::proto::metalog_session::ConsumerProto>);

namespace {

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_TEST_REQUIRE(cond)                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
            ++total_failed;                                                                    \
            return;                                                                            \
        }                                                                                      \
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

[[nodiscard]] bool same_meta(const ::crucible::TensorMeta& a, const ::crucible::TensorMeta& b) {
    return ::crucible::raw_tensor_dim(a.sizes[0]) == ::crucible::raw_tensor_dim(b.sizes[0])
        && ::crucible::raw_tensor_dim(a.strides[0]) == ::crucible::raw_tensor_dim(b.strides[0]) && a.ndim == b.ndim
        && a.dtype == b.dtype && a.device_type == b.device_type && a.device_idx == b.device_idx
        && a.storage_nbytes == b.storage_nbytes && a.version == b.version;
}

[[nodiscard]] auto fresh_metalog_handles(::crucible::MetaLog& raw_log) {
    TestLog log{raw_log};
    auto whole = cs::mint_permission_root<TestLog::whole_tag>();
    auto [prod_perm, cons_perm] =
        cs::mint_permission_split<TestLog::producer_tag, TestLog::consumer_tag>(std::move(whole));
    return std::pair{log.producer(std::move(prod_perm)), log.consumer(std::move(cons_perm))};
}

}  // namespace

static void test_runtime_construct_and_handles() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = fresh_metalog_handles(raw_log);
    (void)producer;
    (void)consumer;

    // A handle is exactly one pointer.  The permission token it carries
    // is empty and collapses into the base.
    static_assert(sizeof(TestLog::ProducerHandle) == sizeof(::crucible::MetaLog*));
    static_assert(sizeof(TestLog::ConsumerHandle) == sizeof(::crucible::MetaLog*));
}

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

static void test_runtime_bulk_append_partial_drain() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = fresh_metalog_handles(raw_log);

    const std::array<::crucible::TensorMeta, 5> source{
        make_meta(1), make_meta(2), make_meta(3), make_meta(4), make_meta(5),
    };

    // A bulk append returns the index of the first record it wrote, not
    // of the last one.
    const ::crucible::MetaIndex start = producer.try_append(source.data(), static_cast<std::uint32_t>(source.size()));
    CRUCIBLE_TEST_REQUIRE(start.is_valid());
    CRUCIBLE_TEST_REQUIRE(start.raw() == 0);

    std::vector<::crucible::TensorMeta> first_three;
    const std::uint32_t drained_count =
        consumer.drain([&](const ::crucible::TensorMeta& meta) { first_three.push_back(meta); },
                       /*max_items=*/3);
    CRUCIBLE_TEST_REQUIRE(drained_count == 3);
    CRUCIBLE_TEST_REQUIRE(first_three.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CRUCIBLE_TEST_REQUIRE(same_meta(first_three[i], source[i]));
    }

    CRUCIBLE_TEST_REQUIRE(consumer.size_approx() == 2);

    std::vector<::crucible::TensorMeta> rest;
    const std::uint32_t remaining = consumer.drain([&](const ::crucible::TensorMeta& meta) { rest.push_back(meta); });
    CRUCIBLE_TEST_REQUIRE(remaining == 2);
    CRUCIBLE_TEST_REQUIRE(same_meta(rest[0], source[3]));
    CRUCIBLE_TEST_REQUIRE(same_meta(rest[1], source[4]));
    CRUCIBLE_TEST_REQUIRE(consumer.size_approx() == 0);
}

static void test_runtime_metaindex_propagates() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = fresh_metalog_handles(raw_log);

    const std::array<::crucible::TensorMeta, 2> first{
        make_meta(10),
        make_meta(20),
    };
    const std::array<::crucible::TensorMeta, 3> second{
        make_meta(30),
        make_meta(40),
        make_meta(50),
    };

    const ::crucible::MetaIndex idx_first = producer.try_append(first.data(), static_cast<std::uint32_t>(first.size()));
    CRUCIBLE_TEST_REQUIRE(idx_first.is_valid());
    CRUCIBLE_TEST_REQUIRE(idx_first.raw() == 0);

    const ::crucible::MetaIndex idx_second =
        producer.try_append(second.data(), static_cast<std::uint32_t>(second.size()));
    CRUCIBLE_TEST_REQUIRE(idx_second.is_valid());
    CRUCIBLE_TEST_REQUIRE(idx_second.raw() == first.size());

    const ::crucible::TensorMeta& at_first = consumer.at(::crucible::MetaIndex{0});
    CRUCIBLE_TEST_REQUIRE(same_meta(at_first, first[0]));

    const ::crucible::TensorMeta& at_third = consumer.at(::crucible::MetaIndex{2});
    CRUCIBLE_TEST_REQUIRE(same_meta(at_third, second[0]));
}

static void test_runtime_empty_drain() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = fresh_metalog_handles(raw_log);
    (void)producer;

    std::vector<::crucible::TensorMeta> collected;
    const std::uint32_t count = consumer.drain([&](const ::crucible::TensorMeta& meta) { collected.push_back(meta); });
    CRUCIBLE_TEST_REQUIRE(count == 0);
    CRUCIBLE_TEST_REQUIRE(collected.empty());
    CRUCIBLE_TEST_REQUIRE(consumer.size_approx() == 0);
    CRUCIBLE_TEST_REQUIRE(consumer.tail_index() == 0);
    CRUCIBLE_TEST_REQUIRE(consumer.head_index() == 0);
}

static void test_runtime_substrate_identity() {
    static_assert(std::is_same_v<typename TestLog::value_type, ::crucible::TensorMeta>);
    static_assert(std::is_same_v<typename TestLog::value_type, fsubstr::metalog::MetaLogRecord>);

    static_assert(!std::is_copy_constructible_v<TestLog>);
    static_assert(!std::is_move_constructible_v<TestLog>);
    static_assert(!std::is_copy_assignable_v<TestLog>);
    static_assert(!std::is_move_assignable_v<TestLog>);
}

int main() {
    std::fprintf(stderr, "test_fixy_substr_metalog_permissioned_metalog: "
                         "starting runtime witnesses\n");

    run_test("construct_and_handles", test_runtime_construct_and_handles);
    run_test("single_append_drain", test_runtime_single_append_drain);
    run_test("bulk_append_partial_drain", test_runtime_bulk_append_partial_drain);
    run_test("metaindex_propagates", test_runtime_metaindex_propagates);
    run_test("empty_drain", test_runtime_empty_drain);
    run_test("substrate_identity", test_runtime_substrate_identity);

    std::fprintf(stderr,
                 "test_fixy_substr_metalog_permissioned_metalog: "
                 "%d/%d runtime witnesses passed\n",
                 total_passed, total_passed + total_failed);

    return total_failed == 0 ? 0 : 1;
}
