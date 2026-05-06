// GAPS-061 integration test for PermissionedMetaLog + MetaLogSession.
//
// Tier A: header-level static_asserts prove pointer-sized endpoint
// handles and PSH-vs-bare SessionHandle size equality.
// Tier B: direct permissioned endpoint use verifies role-typed bulk
// append plus consumer drain over the real MetaLog storage.
// Tier C: typed-session send/recv round trip verifies the infinite
// Loop<Send/Recv<TensorMeta, Continue>> shape against the production
// MetaLog buffer.

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/MetaLogSession.h>

namespace {

struct TestMetaLogTag {};
using PermissionedLog = ::crucible::concurrent::PermissionedMetaLog<TestMetaLogTag>;

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
    meta.sizes[0] = id;
    meta.strides[0] = 1;
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
    return a.sizes[0] == b.sizes[0]
        && a.strides[0] == b.strides[0]
        && a.ndim == b.ndim
        && a.dtype == b.dtype
        && a.device_type == b.device_type
        && a.device_idx == b.device_idx
        && a.storage_nbytes == b.storage_nbytes
        && a.version == b.version;
}

[[nodiscard]] auto mint_handles(::crucible::MetaLog& raw_log) {
    using ::crucible::safety::mint_permission_root;
    using ::crucible::safety::mint_permission_split;

    PermissionedLog log{raw_log};
    auto whole = mint_permission_root<PermissionedLog::whole_tag>();
    auto [pp, cp] = mint_permission_split<PermissionedLog::producer_tag,
                                          PermissionedLog::consumer_tag>(
        std::move(whole));
    return std::pair{
        log.producer(std::move(pp)),
        log.consumer(std::move(cp))
    };
}

void test_permissioned_bulk_drain() {
    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = mint_handles(raw_log);

    const std::array<::crucible::TensorMeta, 3> records{
        make_meta(1),
        make_meta(2),
        make_meta(3),
    };

    const ::crucible::MetaIndex start =
        producer.try_append(records.data(),
                            static_cast<std::uint32_t>(records.size()));
    CRUCIBLE_TEST_REQUIRE(start.is_valid());
    CRUCIBLE_TEST_REQUIRE(start.raw() == 0);

    std::vector<::crucible::TensorMeta> drained;
    const std::uint32_t count = consumer.drain(
        [&](const ::crucible::TensorMeta& meta) {
            drained.push_back(meta);
        });

    CRUCIBLE_TEST_REQUIRE(count == records.size());
    CRUCIBLE_TEST_REQUIRE(drained.size() == records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        CRUCIBLE_TEST_REQUIRE(same_meta(drained[i], records[i]));
    }
    CRUCIBLE_TEST_REQUIRE(consumer.tail_index() == records.size());
}

void test_typed_session_round_trip() {
    namespace ses = ::crucible::safety::proto::metalog_session;
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = mint_handles(raw_log);

    constexpr int kCount = 128;
    std::atomic<bool> producer_done{false};
    std::vector<::crucible::TensorMeta> received;
    received.reserve(kCount);

    std::jthread prod_thread{
        [&producer, &producer_done](auto) mutable {
            auto psh = ses::mint_metalog_producer_session<PermissionedLog>(
                producer);
            for (int i = 0; i < kCount; ++i) {
                auto next = std::move(psh).send(make_meta(i + 10),
                                                ses::blocking_append);
                psh = std::move(next);
            }
            std::move(psh).detach(TestInstrumentation{});
            producer_done.store(true, std::memory_order_release);
        }
    };

    std::jthread cons_thread{
        [&consumer, &received](auto) mutable {
            auto psh = ses::mint_metalog_consumer_session<PermissionedLog>(
                consumer);
            for (int i = 0; i < kCount; ++i) {
                auto [meta, next] = std::move(psh).recv(ses::blocking_drain);
                received.push_back(meta);
                psh = std::move(next);
            }
            std::move(psh).detach(TestInstrumentation{});
        }
    };

    prod_thread.join();
    cons_thread.join();

    CRUCIBLE_TEST_REQUIRE(producer_done.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(received.size() == static_cast<std::size_t>(kCount));
    for (std::size_t i = 0; i < received.size(); ++i) {
        CRUCIBLE_TEST_REQUIRE(same_meta(received[i],
                                        make_meta(static_cast<int>(i) + 10)));
    }
}

void test_typed_session_immediate_detach() {
    namespace ses = ::crucible::safety::proto::metalog_session;
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    ::crucible::MetaLog raw_log;
    auto [producer, consumer] = mint_handles(raw_log);

    auto prod_psh =
        ses::mint_metalog_producer_session<PermissionedLog>(producer);
    auto cons_psh =
        ses::mint_metalog_consumer_session<PermissionedLog>(consumer);

    std::move(prod_psh).detach(TestInstrumentation{});
    std::move(cons_psh).detach(TestInstrumentation{});

    CRUCIBLE_TEST_REQUIRE(true);
}

namespace witness {
namespace proto = ::crucible::safety::proto;
using PSH_End_Prod = proto::PermissionedSessionHandle<
    proto::End, proto::EmptyPermSet, PermissionedLog::ProducerHandle*>;
using SH_End_Prod = proto::SessionHandle<
    proto::End, PermissionedLog::ProducerHandle*>;
static_assert(sizeof(PSH_End_Prod) == sizeof(SH_End_Prod));

using PSH_End_Cons = proto::PermissionedSessionHandle<
    proto::End, proto::EmptyPermSet, PermissionedLog::ConsumerHandle*>;
using SH_End_Cons = proto::SessionHandle<
    proto::End, PermissionedLog::ConsumerHandle*>;
static_assert(sizeof(PSH_End_Cons) == sizeof(SH_End_Cons));
}  // namespace witness

}  // namespace

int main() {
    std::fprintf(stderr, "[test_metalog_session]\n");
    run_test("permissioned_bulk_drain", test_permissioned_bulk_drain);
    run_test("typed_session_round_trip", test_typed_session_round_trip);
    run_test("typed_session_immediate_detach",
             test_typed_session_immediate_detach);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
