#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

namespace {

namespace ms = ::crucible::metalog_session;
namespace fp = ::foundation::permissions;

struct TestMetaLogTag {};

// One call site for every root of the log, so every log of this type
// carries the brand of that site.
[[nodiscard]] auto log_root() noexcept {
    return fp::mint_permission_root<::crucible::metalog_tag::Whole<TestMetaLogTag>>();
}

using PermissionedLog = ::crucible::permissioned_metalog_t<decltype(log_root())>;
using FgCtx = ::fixy::HotFgCtx;
using BgCtx = ::fixy::BgDrainCtx;

// The tag of a snapshot of the log.  The test context holds Nondet.
constexpr ::foundation::effects::cap::Nondet kSnapshot = ::foundation::effects::testing::test().nondet;

// The pure context of each append of the producer endpoint, whose gate reads
// the row and the source of the context of the caller.
constexpr FgCtx kPure = ::foundation::effects::testing::foreground();

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

// The transport of the producer holds a reference to a pure context.  It
// refuses a temporary context and a context whose source permits atoms.
template <typename Ctx>
concept MakesAppendTransport = requires(Ctx&& ctx) { ms::append_one_in(static_cast<Ctx&&>(ctx)); };
static_assert(MakesAppendTransport<FgCtx const&>);
static_assert(!MakesAppendTransport<FgCtx>, "a temporary context ends at the end of the statement");
static_assert(!MakesAppendTransport<::fixy::TestRunnerCtx const&>);
static_assert(!MakesAppendTransport<
              ::foundation::effects::ExecCtx<::foundation::effects::Test, ::foundation::effects::Row<>> const&>);

// A mint cannot throw.
static_assert(noexcept(ms::mint_metalog_producer_session<PermissionedLog>(
    std::declval<FgCtx const&>(), std::declval<PermissionedLog::ProducerHandle&&>())));
static_assert(noexcept(ms::mint_metalog_consumer_session<PermissionedLog>(
    std::declval<FgCtx const&>(), std::declval<PermissionedLog::ConsumerHandle&&>())));

using ProducerSession = ms::ProducerSessionHandle<PermissionedLog, FgCtx>;
using ConsumerSession = ms::ConsumerSessionHandle<PermissionedLog, FgCtx>;

// A Loop at the head is unrolled one iteration, so each first handle sits
// at the choice of its loop body.
static_assert(
    std::is_same_v<ProducerSession::protocol,
                   ::fixy::session::Select<::fixy::session::Send<ms::MetaLogRecord, ::fixy::session::Continue>,
                                           ::fixy::session::End>>);
static_assert(
    std::is_same_v<ConsumerSession::protocol,
                   ::fixy::session::Select<::fixy::session::Recv<ms::MetaLogRecord, ::fixy::session::Continue>,
                                           ::fixy::session::End>>);
static_assert(std::is_same_v<ProducerSession::perm_set, fp::EmptyPermSet>);
static_assert(std::is_same_v<ConsumerSession::perm_set, fp::EmptyPermSet>);

// The session owns the handle, so the handle is its Resource by value.
static_assert(std::is_same_v<ProducerSession::resource_type, PermissionedLog::ProducerHandle>);
static_assert(std::is_same_v<ConsumerSession::resource_type, PermissionedLog::ConsumerHandle>);

// A handle is exactly one pointer.  The permission token it carries is
// empty and collapses into the binding.
static_assert(sizeof(PermissionedLog::ProducerHandle) == sizeof(void*));
static_assert(sizeof(PermissionedLog::ConsumerHandle) == sizeof(void*));

// The wrapper names one MetaLog for life, so it has no copy and no move.
static_assert(std::is_same_v<PermissionedLog::value_type, ::crucible::TensorMeta>);
static_assert(std::is_same_v<PermissionedLog::value_type, ms::MetaLogRecord>);
static_assert(!std::is_copy_constructible_v<PermissionedLog>);
static_assert(!std::is_move_constructible_v<PermissionedLog>);
static_assert(!std::is_copy_assignable_v<PermissionedLog>);
static_assert(!std::is_move_assignable_v<PermissionedLog>);

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

[[nodiscard]] auto mint_halves() noexcept {
    return fp::mint_permission_split<PermissionedLog::producer_tag, PermissionedLog::consumer_tag>(log_root());
}

[[nodiscard]] auto mint_handles(PermissionedLog& log) {
    auto [pp, cp] = mint_halves();
    return std::pair{log.producer(std::move(pp)), log.consumer(std::move(cp))};
}

void test_permissioned_bulk_drain() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);

    const ::fixy::FixedArray<::crucible::TensorMeta, 3> records{
        make_meta(1),
        make_meta(2),
        make_meta(3),
    };

    // The pure append takes a context whose row is empty, and it appends where
    // the plain append appends.
    const FgCtx fg = ::foundation::effects::testing::foreground();
    const ::crucible::MetaIndex start = producer.try_append_pure(fg, records.view());
    CRUCIBLE_TEST_REQUIRE(start.is_valid());
    CRUCIBLE_TEST_REQUIRE(start.raw() == 0);

    std::vector<::crucible::TensorMeta> drained(records.size());
    std::size_t next = 0;
    const std::uint32_t count = consumer.drain([&](const ::crucible::TensorMeta& meta) { drained[next++] = meta; });

    CRUCIBLE_TEST_REQUIRE(count == records.size());
    CRUCIBLE_TEST_REQUIRE(next == records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        CRUCIBLE_TEST_REQUIRE(
            same_meta(drained[i], *records.get(i).expect("a test reads an element at or past the count of its array")));
    }
    CRUCIBLE_TEST_REQUIRE(consumer.tail_index() == records.size());
}

void test_single_append_drain() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);

    const ::crucible::TensorMeta source = make_meta(42);
    CRUCIBLE_TEST_REQUIRE(producer.try_append_one_pure(kPure, source));

    auto drained = consumer.try_drain_one();
    CRUCIBLE_TEST_REQUIRE(drained.is_some());
    CRUCIBLE_TEST_REQUIRE(same_meta(std::move(drained).expect("the log holds one record"), source));

    auto empty = consumer.try_drain_one();
    CRUCIBLE_TEST_REQUIRE(empty.is_none());

    // The release door gives each role and its token back, and each token
    // opens a new handle of its role with no second claim.
    auto second_producer = log.producer(std::move(producer).release());
    auto second_consumer = log.consumer(std::move(consumer).release());
    CRUCIBLE_TEST_REQUIRE(second_producer.try_append_one_pure(kPure, source));
    CRUCIBLE_TEST_REQUIRE(second_consumer.size_approx(kSnapshot) == 1);
    ::foundation::linear::drop(std::move(second_producer).release());
    ::foundation::linear::drop(std::move(second_consumer).release());
}

// A drain with a limit stops at the limit, and the next drain takes the
// records that stay.
void test_partial_drain() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);

    const ::fixy::FixedArray<::crucible::TensorMeta, 5> source{
        make_meta(1), make_meta(2), make_meta(3), make_meta(4), make_meta(5),
    };
    const ::crucible::MetaIndex start = producer.try_append_pure(kPure, source.view());
    CRUCIBLE_TEST_REQUIRE(start.is_valid());
    CRUCIBLE_TEST_REQUIRE(start.raw() == 0);

    std::array<::crucible::TensorMeta, 3> first_three{};
    std::size_t next = 0;
    const std::uint32_t drained_count =
        consumer.drain([&](const ::crucible::TensorMeta& meta) { first_three[next++] = meta; },
                       /*max_items=*/3);
    CRUCIBLE_TEST_REQUIRE(drained_count == 3);
    CRUCIBLE_TEST_REQUIRE(next == 3);
    for (std::size_t i = 0; i < first_three.size(); ++i) {
        CRUCIBLE_TEST_REQUIRE(same_meta(
            first_three[i], *source.get(i).expect("a test reads an element at or past the count of its array")));
    }
    CRUCIBLE_TEST_REQUIRE(consumer.size_approx(kSnapshot) == 2);
    CRUCIBLE_TEST_REQUIRE(producer.size_approx(kSnapshot) == 2);

    std::array<::crucible::TensorMeta, 2> rest{};
    std::size_t rest_next = 0;
    const std::uint32_t remaining =
        consumer.drain([&](const ::crucible::TensorMeta& meta) { rest[rest_next++] = meta; });
    CRUCIBLE_TEST_REQUIRE(remaining == 2);
    CRUCIBLE_TEST_REQUIRE(same_meta(rest[0], source.at(::fixy::index<3>())));
    CRUCIBLE_TEST_REQUIRE(same_meta(rest[1], source.at(::fixy::index<4>())));
    CRUCIBLE_TEST_REQUIRE(consumer.size_approx(kSnapshot) == 0);
}

// A bulk append returns the index of the first record it wrote, and copy_at()
// copies the record at that index.
void test_metaindex_propagates() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);

    const ::fixy::FixedArray<::crucible::TensorMeta, 2> first{make_meta(10), make_meta(20)};
    const ::fixy::FixedArray<::crucible::TensorMeta, 3> second{make_meta(30), make_meta(40), make_meta(50)};

    const ::crucible::MetaIndex idx_first = producer.try_append_pure(kPure, first.view());
    CRUCIBLE_TEST_REQUIRE(idx_first.is_valid());
    CRUCIBLE_TEST_REQUIRE(idx_first.raw() == 0);

    const ::crucible::MetaIndex idx_second = producer.try_append_pure(kPure, second.view());
    CRUCIBLE_TEST_REQUIRE(idx_second.is_valid());
    CRUCIBLE_TEST_REQUIRE(idx_second.raw() == first.size());

    CRUCIBLE_TEST_REQUIRE(same_meta(consumer.copy_at(::crucible::MetaIndex{0}), make_meta(10)));
    CRUCIBLE_TEST_REQUIRE(same_meta(consumer.copy_at(::crucible::MetaIndex{2}), make_meta(30)));
}

void test_empty_drain() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);
    (void)producer;

    std::size_t visited = 0;
    const std::uint32_t count = consumer.drain([&](const ::crucible::TensorMeta&) { ++visited; });
    CRUCIBLE_TEST_REQUIRE(count == 0);
    CRUCIBLE_TEST_REQUIRE(visited == 0);
    CRUCIBLE_TEST_REQUIRE(consumer.size_approx(kSnapshot) == 0);
    CRUCIBLE_TEST_REQUIRE(consumer.tail_index() == 0);
    CRUCIBLE_TEST_REQUIRE(consumer.head_index() == 0);
}

// Each side holds its session through its mint, stops by its own choice and
// gets its handle back.  A handle of the log is a pole of a channel, so no
// callback session that names its own protocol admits it.
//
// A MetaLog handle refuses move-assignment, so a session over it refuses
// move-assignment too.  Each loop keeps its current session in an optional
// and puts the next one in its place with emplace.
void test_typed_session_round_trip() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);

    constexpr int kCount = 128;
    std::atomic<bool> producer_done{false};
    std::atomic<bool> producer_handle_back{false};
    int matched_count = 0;
    bool consumer_handle_back = false;

    std::jthread prod_thread{[&producer, &producer_done, &producer_handle_back](auto) mutable {
        const FgCtx ctx = ::foundation::effects::testing::foreground();
        std::optional head{ms::mint_metalog_producer_session<PermissionedLog>(ctx, std::move(producer))};
        for (int i = 0; i < kCount; ++i) {
            head.emplace(
                std::move(*head).select<0>(::fixy::session::no_label).send(make_meta(i + 10), ms::append_one_in(ctx)));
        }
        auto back = std::move(*head).select<1>(::fixy::session::no_label).close();
        producer_handle_back.store(back.size_approx(kSnapshot) <= static_cast<std::uint32_t>(kCount),
                                   std::memory_order_release);
        producer_done.store(true, std::memory_order_release);
    }};

    std::jthread cons_thread{[&consumer, &matched_count, &consumer_handle_back](auto) mutable {
        const BgCtx ctx{::foundation::effects::testing::bg()};
        std::optional head{ms::mint_metalog_consumer_session<PermissionedLog>(ctx, std::move(consumer))};
        for (int i = 0; i < kCount; ++i) {
            auto [meta, next] = std::move(*head).select<0>(::fixy::session::no_label).recv(ms::drain_one);
            matched_count += same_meta(meta, make_meta(i + 10)) ? 1 : 0;
            head.emplace(std::move(next));
        }
        auto back = std::move(*head).select<1>(::fixy::session::no_label).close();
        consumer_handle_back = back.tail_index() == static_cast<std::uint32_t>(kCount);
    }};

    prod_thread.join();
    cons_thread.join();

    CRUCIBLE_TEST_REQUIRE(producer_done.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(producer_handle_back.load(std::memory_order_acquire));
    CRUCIBLE_TEST_REQUIRE(consumer_handle_back);
    CRUCIBLE_TEST_REQUIRE(matched_count == kCount);
}

// A session that ends at once gives back a handle that still reaches the
// log, and the moved-from caller variable does not.
void test_session_gives_the_handle_back() {
    const FgCtx fg = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);

    auto producer_head = ms::mint_metalog_producer_session<PermissionedLog>(fg, std::move(producer));
    auto consumer_head = ms::mint_metalog_consumer_session<PermissionedLog>(fg, std::move(consumer));
    auto producer_back = std::move(producer_head).select<1>(::fixy::session::no_label).close();
    auto consumer_back = std::move(consumer_head).select<1>(::fixy::session::no_label).close();

    CRUCIBLE_TEST_REQUIRE(producer_back.try_append_one_pure(fg, make_meta(7)));
    auto drained = consumer_back.try_drain_one();
    CRUCIBLE_TEST_REQUIRE(drained.is_some());
    CRUCIBLE_TEST_REQUIRE(same_meta(std::move(drained).expect("the log holds one record"), make_meta(7)));
}

// A use of a moved-from handle ends the process.  The move gives the
// permission and the log to the new handle, so the source has nothing to
// act on.  Each attack runs in a child process, and a signal must end the
// child.
//
// Each attack reaches the moved-from handle through a call that the
// optimizer cannot see into.  Inlined, the empty binding is visible at
// compile time, and -Wstringop-overflow refuses the null access that the
// attack makes on purpose.
template <typename Handle>
[[gnu::noipa]] Handle& opaque_ref(Handle& handle) {
    return handle;
}

void log_producer() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);
    [[maybe_unused]] auto moved = std::move(producer);
    (void)opaque_ref(producer).try_append_one_pure(kPure, make_meta(1));
}

void log_consumer() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer, consumer] = mint_handles(log);
    [[maybe_unused]] auto moved = std::move(consumer);
    (void)opaque_ref(consumer).try_drain_one();
}

// One call site that runs two times mints two roots of one brand.  The
// claim of each role refuses the second live handle of that role.
void second_live_producer() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [first_producer, first_consumer] = mint_halves();
    auto [second_producer, second_consumer] = mint_halves();
    [[maybe_unused]] auto held = log.producer(std::move(first_producer));
    [[maybe_unused]] auto second = log.producer(std::move(second_producer));
}

void second_live_consumer() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [first_producer, first_consumer] = mint_halves();
    auto [second_producer, second_consumer] = mint_halves();
    [[maybe_unused]] auto held = log.consumer(std::move(first_consumer));
    [[maybe_unused]] auto second = log.consumer(std::move(second_consumer));
}

// The consumer drains the first record, and the tail moves past it.  The
// producer can then write a new record into that slot, so a copy of the
// record at index 0 ends the process.
void copy_of_a_released_record() {
    ::crucible::MetaLog raw_log{::foundation::effects::testing::test().alloc};
    PermissionedLog log{raw_log};
    auto [producer_token, consumer_token] = mint_halves();
    auto producer = log.producer(std::move(producer_token));
    auto consumer = log.consumer(std::move(consumer_token));
    const ::crucible::TensorMeta record = make_meta(1);
    CRUCIBLE_FATAL_INVARIANT(producer.try_append_one_pure(kPure, record)
                             && producer.try_append_one_pure(kPure, record));
    CRUCIBLE_FATAL_INVARIANT(consumer.drain([](const ::crucible::TensorMeta&) noexcept {}, 1) == 1);
    (void)consumer.copy_at(::crucible::MetaIndex{0});
}

// Each attack runs in a child through expect_dies of the Check family, and
// the child must end by SIGABRT with the report of the refused use.
void test_moved_from_handles_end_the_process(::fixy::TestRunnerCtx const& test) {
    constexpr ::fixy::TextView kNoChannel{"foundation: fatal invariant failed: channel_ != nullptr"};
    ::fixy::check::expect_dies(test, log_producer, kNoChannel);
    ::fixy::check::expect_dies(test, log_consumer, kNoChannel);
    ::fixy::check::expect_dies(test, second_live_producer,
                               ::fixy::TextView{"two live producer handles of one MetaLog"});
    ::fixy::check::expect_dies(test, second_live_consumer,
                               ::fixy::TextView{"two live consumer handles of one MetaLog"});
    ::fixy::check::expect_dies(
        test, copy_of_a_released_record,
        ::fixy::TextView{"foundation: fatal invariant failed: index.raw() >= ch_->log_.tail.peek_relaxed()"});
    ::fixy::check::pass(test, "  moved_from_handles_end_the_process ... OK\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    std::fprintf(stderr, "[test_metalog_session]\n");
    // The attacks fork, so they run before a test starts a thread.
    test_moved_from_handles_end_the_process(test);
    run_test("permissioned_bulk_drain", test_permissioned_bulk_drain);
    run_test("single_append_drain", test_single_append_drain);
    run_test("partial_drain", test_partial_drain);
    run_test("metaindex_propagates", test_metaindex_propagates);
    run_test("empty_drain", test_empty_drain);
    run_test("typed_session_round_trip", test_typed_session_round_trip);
    run_test("session_gives_the_handle_back", test_session_gives_the_handle_back);
    ::fixy::check::pass(test, "\n{} passed, {} failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
