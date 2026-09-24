#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

namespace {

struct TestMetaLogTag {};
using PermissionedLog = ::crucible::PermissionedMetaLog<TestMetaLogTag>;

namespace ms = ::crucible::metalog_session;
namespace fp = ::foundation::permissions;
using FgCtx = ::fixy::HotFgCtx;
using BgCtx = ::fixy::BgDrainCtx;

// The gate of each mint reads the context, the log and the handle.
static_assert(ms::CtxFitsMetaLogProducerSession<FgCtx, PermissionedLog, PermissionedLog::ProducerHandle>
              && ms::CtxFitsMetaLogConsumerSession<FgCtx, PermissionedLog, PermissionedLog::ConsumerHandle>);
static_assert(!ms::CtxFitsMetaLogProducerSession<int, PermissionedLog, PermissionedLog::ProducerHandle>,
              "an int is not an execution context");
static_assert(!ms::CtxFitsMetaLogProducerSession<FgCtx, int, PermissionedLog::ProducerHandle>,
              "an int is not a permissioned log");
static_assert(!ms::CtxFitsMetaLogProducerSession<FgCtx, PermissionedLog, PermissionedLog::ConsumerHandle>,
              "the producer mint takes a producer");
static_assert(!ms::CtxFitsMetaLogProducerSession<FgCtx, PermissionedLog, PermissionedLog::ProducerHandle&>,
              "the mint takes the handle by move");

using ProducerSession = ms::ProducerSessionHandle<PermissionedLog, FgCtx>;
using ConsumerSession = ms::ConsumerSessionHandle<PermissionedLog, FgCtx>;

// A Loop at the head is unrolled one iteration, so each first handle sits
// at the choice of its loop body.
static_assert(std::is_same_v<ProducerSession::protocol,
                             ::fixy::session::Select<::fixy::session::Send<ms::MetaLogRecord, ::fixy::session::Continue>,
                                                     ::fixy::session::End>>);
static_assert(std::is_same_v<ConsumerSession::protocol,
                             ::fixy::session::Select<::fixy::session::Recv<ms::MetaLogRecord, ::fixy::session::Continue>,
                                                     ::fixy::session::End>>);
static_assert(std::is_same_v<ProducerSession::perm_set, fp::EmptyPermSet>);
static_assert(std::is_same_v<ConsumerSession::perm_set, fp::EmptyPermSet>);

// The session owns the handle, so the handle is its Resource by value.
static_assert(std::is_same_v<ProducerSession::resource_type, PermissionedLog::ProducerHandle>);
static_assert(std::is_same_v<ConsumerSession::resource_type, PermissionedLog::ConsumerHandle>);

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

[[nodiscard]] auto mint_handles(PermissionedLog& log) {
    auto whole = fp::mint_permission_root<PermissionedLog::whole_tag>();
    auto [pp, cp] = fp::mint_permission_split<PermissionedLog::producer_tag, PermissionedLog::consumer_tag>(
        std::move(whole));
    return std::pair{log.producer(std::move(pp)), log.consumer(std::move(cp))};
}

void test_permissioned_bulk_drain() {
    ::crucible::MetaLog raw_log;
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);

    const std::array<::crucible::TensorMeta, 3> records{
        make_meta(1),
        make_meta(2),
        make_meta(3),
    };

    const ::crucible::MetaIndex start = producer.try_append(records.data(), static_cast<std::uint32_t>(records.size()));
    CRUCIBLE_TEST_REQUIRE(start.is_valid());
    CRUCIBLE_TEST_REQUIRE(start.raw() == 0);

    std::vector<::crucible::TensorMeta> drained(records.size());
    std::size_t next = 0;
    const std::uint32_t count = consumer.drain([&](const ::crucible::TensorMeta& meta) { drained[next++] = meta; });

    CRUCIBLE_TEST_REQUIRE(count == records.size());
    CRUCIBLE_TEST_REQUIRE(next == records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        CRUCIBLE_TEST_REQUIRE(same_meta(drained[i], records[i]));
    }
    CRUCIBLE_TEST_REQUIRE(consumer.tail_index() == records.size());
}

// The producer holds its session through the mint.  The consumer lends its
// handle to a body through with_session.  Each side stops by its own choice
// and gets its handle back.
//
// A MetaLog handle refuses move-assignment, so a session over it refuses
// move-assignment too.  Each loop keeps its current session in an optional
// and puts the next one in its place with emplace.
void test_typed_session_round_trip() {
    ::crucible::MetaLog raw_log;
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);

    constexpr int kCount = 128;
    std::atomic<bool> producer_done{false};
    std::atomic<bool> producer_handle_back{false};
    std::vector<::crucible::TensorMeta> received(kCount);
    bool consumer_handle_back = false;

    std::jthread prod_thread{[&producer, &producer_done, &producer_handle_back](auto) mutable {
        const FgCtx ctx = ::foundation::effects::testing::foreground();
        std::optional head{ms::mint_metalog_producer_session<PermissionedLog>(ctx, std::move(producer))};
        for (int i = 0; i < kCount; ++i) {
            head.emplace(std::move(*head).select_local<0>().send(make_meta(i + 10), ms::append_one));
        }
        auto back = std::move(*head).select_local<1>().close();
        producer_handle_back.store(back.size_approx() <= static_cast<std::uint32_t>(kCount),
                                   std::memory_order_release);
        producer_done.store(true, std::memory_order_release);
    }};

    std::jthread cons_thread{[&consumer, &received, &consumer_handle_back](auto) mutable {
        const BgCtx ctx{::foundation::effects::testing::bg()};
        auto back = ::fixy::session::with_session<ms::ConsumerProto>(
            ctx, std::move(consumer), [&received](auto first) noexcept {
                std::optional head{std::move(first)};
                for (int i = 0; i < kCount; ++i) {
                    auto [meta, next] = std::move(*head).template select_local<0>().recv(ms::drain_one);
                    received[static_cast<std::size_t>(i)] = meta;
                    head.emplace(std::move(next));
                }
                return std::move(*head).template select_local<1>();
            });
        consumer_handle_back = back.tail_index() == static_cast<std::uint32_t>(kCount);
    }};

    prod_thread.join();
    cons_thread.join();

    CRUCIBLE_TEST_REQUIRE(producer_done.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(producer_handle_back.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(consumer_handle_back);
    for (std::size_t i = 0; i < received.size(); ++i) {
        CRUCIBLE_TEST_REQUIRE(same_meta(received[i], make_meta(static_cast<int>(i) + 10)));
    }
}

// A session that ends at once gives back a handle that still reaches the
// log, and the moved-from caller variable does not.
void test_session_gives_the_handle_back() {
    const FgCtx fg = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw_log;
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);

    auto producer_head = ms::mint_metalog_producer_session<PermissionedLog>(fg, std::move(producer));
    auto consumer_head = ms::mint_metalog_consumer_session<PermissionedLog>(fg, std::move(consumer));
    auto producer_back = std::move(producer_head).select_local<1>().close();
    auto consumer_back = std::move(consumer_head).select_local<1>().close();

    CRUCIBLE_TEST_REQUIRE(producer_back.try_append_one(make_meta(7)));
    auto drained = consumer_back.try_drain_one();
    CRUCIBLE_TEST_REQUIRE(drained.has_value());
    CRUCIBLE_TEST_REQUIRE(same_meta(*drained, make_meta(7)));
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_metalog_session]\n");
    run_test("permissioned_bulk_drain", test_permissioned_bulk_drain);
    run_test("typed_session_round_trip", test_typed_session_round_trip);
    run_test("session_gives_the_handle_back", test_session_gives_the_handle_back);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
