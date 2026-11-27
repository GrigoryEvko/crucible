#include <cstdint>
#include <type_traits>
#include <utility>

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/Linear.h>
#include <foundation/core/Seat.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

namespace {

namespace ms = ::crucible::metalog_session;
namespace fp = ::foundation::permissions;
namespace cc = ::fixy::concurrent;

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

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

// A scope brand that the checks name.  No frame door gives a handle on it, and
// each check below reads a frame handle of the log on it.
struct CheckScope {};

// The gate of each mint reads the context, the log and the handle.
static_assert(
    ms::CtxFitsMetaLogProducerSession<FgCtx, PermissionedLog, PermissionedLog::producer_handle<CheckScope>>
    && ms::CtxFitsMetaLogConsumerSession<FgCtx, PermissionedLog, PermissionedLog::consumer_handle<CheckScope>>);
static_assert(!ms::CtxFitsMetaLogProducerSession<int, PermissionedLog, PermissionedLog::producer_handle<CheckScope>>,
              "an int is not an execution context");
static_assert(!ms::CtxFitsMetaLogProducerSession<FgCtx, int, PermissionedLog::producer_handle<CheckScope>>,
              "an int is not a permissioned log");
static_assert(!ms::CtxFitsMetaLogProducerSession<FgCtx, PermissionedLog, PermissionedLog::consumer_handle<CheckScope>>,
              "the producer mint takes a producer");
static_assert(!ms::CtxFitsMetaLogProducerSession<FgCtx, PermissionedLog, PermissionedLog::producer_handle<CheckScope>&>,
              "the mint takes the handle by move");

// The write of the log table appends in the context of the step, so that
// context has a pure source.  A context whose source permits atoms does not
// step the producer.  The read takes each context.
template <typename Ctx>
concept AppendsIn = ::fixy::session::PoleWritesIn<PermissionedLog::producer_handle<CheckScope>, Ctx, ms::MetaLogRecord>;
static_assert(AppendsIn<FgCtx>);
static_assert(!AppendsIn<::fixy::TestRunnerCtx>);
static_assert(!AppendsIn<::foundation::effects::ExecCtx<::foundation::effects::Test, ::foundation::effects::Row<>>>);
static_assert(::fixy::session::PoleReadsIn<PermissionedLog::consumer_handle<CheckScope>, ::fixy::TestRunnerCtx,
                                           ms::MetaLogRecord>);

// A mint cannot throw.
static_assert(noexcept(ms::mint_metalog_producer_session<PermissionedLog>(
    std::declval<FgCtx const&>(), std::declval<PermissionedLog::producer_handle<CheckScope>&&>())));
static_assert(noexcept(ms::mint_metalog_consumer_session<PermissionedLog>(
    std::declval<FgCtx const&>(), std::declval<PermissionedLog::consumer_handle<CheckScope>&&>())));

using ProducerSession = ms::ProducerSessionHandle<PermissionedLog, FgCtx, CheckScope>;
using ConsumerSession = ms::ConsumerSessionHandle<PermissionedLog, FgCtx, CheckScope>;

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
static_assert(std::is_same_v<ProducerSession::resource_type, PermissionedLog::producer_handle<CheckScope>>);
static_assert(std::is_same_v<ConsumerSession::resource_type, PermissionedLog::consumer_handle<CheckScope>>);

// A handle is exactly one pointer.  The permission token it carries is
// empty and collapses into the binding.
static_assert(sizeof(PermissionedLog::producer_handle<CheckScope>) == sizeof(void*));
static_assert(sizeof(PermissionedLog::consumer_handle<CheckScope>) == sizeof(void*));

// The wrapper holds one MetaLog for life, so it has no copy and no move.
static_assert(std::is_same_v<PermissionedLog::value_type, ::crucible::TensorMeta>);
static_assert(std::is_same_v<PermissionedLog::value_type, ms::MetaLogRecord>);
static_assert(!std::is_copy_constructible_v<PermissionedLog>);
static_assert(!std::is_move_constructible_v<PermissionedLog>);
static_assert(!std::is_copy_assignable_v<PermissionedLog>);
static_assert(!std::is_move_assignable_v<PermissionedLog>);

int total_passed = 0;

// Runs the body of one group.  A check of the group that fails ends the
// process with its condition and its site, so a group that returns passed.
template <typename Body>
void run_test(::fixy::TextView name, Body body) {
    body();
    ++total_passed;
    ::fixy::check::pass(kTest, "  {} ... OK\n", name);
}

[[nodiscard]] ::crucible::TensorMeta make_meta(std::int64_t id) {
    const ::fixy::FixedArray<::crucible::TensorDim, 1> dims{::crucible::tensor_dim(id)};
    ::crucible::TensorMeta meta{};
    meta.set_shape(::crucible::contiguous_tensor_shape(::crucible::TensorSizes{dims.view()})
                       .expect("a contiguous shape of rank 1"));
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

// The frame door with_ends of fixy/concurrent/PermissionedChannel.h opens the
// two ends of the log and gives each handle to a body.  Each check runs in
// that body, and each handle gives its token back at the end of the body.

void test_permissioned_bulk_drain() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();

    const ::fixy::FixedArray<::crucible::TensorMeta, 3> records{
        make_meta(1),
        make_meta(2),
        make_meta(3),
    };

    cc::with_ends(
        log, std::move(producer_token), std::move(consumer_token), [&records](auto producer, auto consumer) noexcept {
            // The pure append takes a context whose row is empty, and it
            // appends where the plain append appends.
            const FgCtx fg = ::foundation::effects::testing::foreground();
            const ::crucible::MetaIndex start = producer.try_append_pure(fg, records.view());
            CRUCIBLE_TEST_ASSERT(kTest, start.is_valid());
            CRUCIBLE_TEST_ASSERT(kTest, start.raw() == 0);

            ::fixy::FixedArray<::crucible::TensorMeta, 3> drained{};
            std::size_t next = 0;
            const std::uint32_t count = consumer.drain([&](const ::crucible::TensorMeta& meta) {
                drained.set(::fixy::Index<3>::check(next++).expect("the drain gives at most the appended records"),
                            meta);
            });

            CRUCIBLE_TEST_ASSERT(kTest, count == records.size());
            CRUCIBLE_TEST_ASSERT(kTest, next == records.size());
            for (std::size_t i = 0; i < records.size(); ++i) {
                CRUCIBLE_TEST_ASSERT(
                    kTest,
                    same_meta(*drained.get(i).expect("a test reads an element at or past the count of its array"),
                              *records.get(i).expect("a test reads an element at or past the count of its array")));
            }
            CRUCIBLE_TEST_ASSERT(kTest, consumer.tail_index().raw() == records.size());
            ::foundation::linear::drop(std::move(producer).release());
            ::foundation::linear::drop(std::move(consumer).release());
        });
}

void test_single_append_drain() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();

    const ::crucible::TensorMeta source = make_meta(42);
    // The release door gives each role and its token back.
    auto [producer_back, consumer_back] = cc::with_ends(
        log, std::move(producer_token), std::move(consumer_token), [&source](auto producer, auto consumer) noexcept {
            CRUCIBLE_TEST_ASSERT(kTest, producer.try_append_one_pure(kPure, source));

            auto drained = consumer.try_drain_one();
            CRUCIBLE_TEST_ASSERT(kTest, drained.is_some());
            CRUCIBLE_TEST_ASSERT(kTest, same_meta(std::move(drained).expect("the log holds one record"), source));

            auto empty = consumer.try_drain_one();
            CRUCIBLE_TEST_ASSERT(kTest, empty.is_none());
            return ::fixy::Record{std::move(producer).release(), std::move(consumer).release()};
        });

    // Each token opens a new handle of its role with no second claim.
    cc::with_ends(log, std::move(producer_back), std::move(consumer_back),
                  [&source](auto second_producer, auto second_consumer) noexcept {
                      CRUCIBLE_TEST_ASSERT(kTest, second_producer.try_append_one_pure(kPure, source));
                      CRUCIBLE_TEST_ASSERT(kTest, second_consumer.size_approx(kSnapshot) == 1);
                      ::foundation::linear::drop(std::move(second_producer).release());
                      ::foundation::linear::drop(std::move(second_consumer).release());
                  });
}

// A drain with a limit stops at the limit, and the next drain takes the
// records that stay.
void test_partial_drain() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();

    const ::fixy::FixedArray<::crucible::TensorMeta, 5> source{
        make_meta(1), make_meta(2), make_meta(3), make_meta(4), make_meta(5),
    };
    cc::with_ends(
        log, std::move(producer_token), std::move(consumer_token), [&source](auto producer, auto consumer) noexcept {
            const ::crucible::MetaIndex start = producer.try_append_pure(kPure, source.view());
            CRUCIBLE_TEST_ASSERT(kTest, start.is_valid());
            CRUCIBLE_TEST_ASSERT(kTest, start.raw() == 0);

            ::fixy::FixedArray<::crucible::TensorMeta, 3> first_three{};
            std::size_t next = 0;
            const std::uint32_t drained_count = consumer.drain(
                [&](const ::crucible::TensorMeta& meta) {
                    first_three.set(
                        ::fixy::Index<3>::check(next++).expect("the drain gives at most its limit of records"), meta);
                },
                /*max_items=*/3);
            CRUCIBLE_TEST_ASSERT(kTest, drained_count == 3);
            CRUCIBLE_TEST_ASSERT(kTest, next == 3);
            for (std::size_t i = 0; i < first_three.size(); ++i) {
                CRUCIBLE_TEST_ASSERT(
                    kTest,
                    same_meta(*first_three.get(i).expect("a test reads an element at or past the count of its array"),
                              *source.get(i).expect("a test reads an element at or past the count of its array")));
            }
            CRUCIBLE_TEST_ASSERT(kTest, consumer.size_approx(kSnapshot) == 2);
            CRUCIBLE_TEST_ASSERT(kTest, producer.size_approx(kSnapshot) == 2);

            ::fixy::FixedArray<::crucible::TensorMeta, 2> rest{};
            std::size_t rest_next = 0;
            const std::uint32_t remaining = consumer.drain([&](const ::crucible::TensorMeta& meta) {
                rest.set(::fixy::Index<2>::check(rest_next++).expect("the drain gives the records that stay"), meta);
            });
            CRUCIBLE_TEST_ASSERT(kTest, remaining == 2);
            CRUCIBLE_TEST_ASSERT(kTest, same_meta(rest.at(::fixy::index<0>()), source.at(::fixy::index<3>())));
            CRUCIBLE_TEST_ASSERT(kTest, same_meta(rest.at(::fixy::index<1>()), source.at(::fixy::index<4>())));
            CRUCIBLE_TEST_ASSERT(kTest, consumer.size_approx(kSnapshot) == 0);
            ::foundation::linear::drop(std::move(producer).release());
            ::foundation::linear::drop(std::move(consumer).release());
        });
}

// A bulk append returns the index of the first record it wrote, and copy_at()
// copies the record at that index.
void test_metaindex_propagates() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();

    const ::fixy::FixedArray<::crucible::TensorMeta, 2> first{make_meta(10), make_meta(20)};
    const ::fixy::FixedArray<::crucible::TensorMeta, 3> second{make_meta(30), make_meta(40), make_meta(50)};

    cc::with_ends(log, std::move(producer_token), std::move(consumer_token),
                  [&first, &second](auto producer, auto consumer) noexcept {
                      const ::crucible::MetaIndex idx_first = producer.try_append_pure(kPure, first.view());
                      CRUCIBLE_TEST_ASSERT(kTest, idx_first.is_valid());
                      CRUCIBLE_TEST_ASSERT(kTest, idx_first.raw() == 0);

                      const ::crucible::MetaIndex idx_second = producer.try_append_pure(kPure, second.view());
                      CRUCIBLE_TEST_ASSERT(kTest, idx_second.is_valid());
                      CRUCIBLE_TEST_ASSERT(kTest, idx_second.raw() == first.size());

                      CRUCIBLE_TEST_ASSERT(kTest, same_meta(consumer.copy_at(::crucible::MetaIndex{0}), make_meta(10)));
                      CRUCIBLE_TEST_ASSERT(kTest, same_meta(consumer.copy_at(::crucible::MetaIndex{2}), make_meta(30)));
                      ::foundation::linear::drop(std::move(producer).release());
                      ::foundation::linear::drop(std::move(consumer).release());
                  });
}

void test_empty_drain() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();

    // The producer appends nothing, and it gives its token back.
    cc::with_ends(log, std::move(producer_token), std::move(consumer_token), [](auto producer, auto consumer) noexcept {
        std::size_t visited = 0;
        const std::uint32_t count = consumer.drain([&](const ::crucible::TensorMeta&) { ++visited; });
        CRUCIBLE_TEST_ASSERT(kTest, count == 0);
        CRUCIBLE_TEST_ASSERT(kTest, visited == 0);
        CRUCIBLE_TEST_ASSERT(kTest, consumer.size_approx(kSnapshot) == 0);
        CRUCIBLE_TEST_ASSERT(kTest, consumer.tail_index() == ::crucible::MetaIndex{0});
        CRUCIBLE_TEST_ASSERT(kTest, consumer.head_index() == ::crucible::MetaIndex{0});
        ::foundation::linear::drop(std::move(producer).release());
        ::foundation::linear::drop(std::move(consumer).release());
    });
}

// Each side holds its session through its mint, stops by its own choice and
// gets its handle back.  A handle of the log is a pole of a channel, so no
// callback session that names its own protocol admits it.
//
// A MetaLog handle refuses move-assignment, so a session over it refuses
// move-assignment too.  Each loop keeps its current session in a Seat of
// foundation/core/Seat.h: each step runs on the session in the storage of the
// Seat and puts the next session there, and the take after the loop gives the
// last session back.
//
// The result of the producer of the round trip: the end of its loop, and the
// handle that came back.
struct ProducerResult {
    bool is_done = false;
    bool has_handle_back = false;
};

// The result of the consumer of the round trip: the count of matched items,
// and the handle that came back.  The flag word holds no padding byte.
struct ConsumerResult {
    int matched = 0;
    int has_handle_back = 0;
};

// Each body owns its handle, and it gives its result back.  The thread door
// joins both threads before it returns, and the test reads the results from
// the value of the call.  The two bodies take the handles of one frame by
// move, so the thread door runs inside that frame.
void test_typed_session_round_trip() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();

    constexpr int kCount = 128;

    cc::with_ends(log, std::move(producer_token), std::move(consumer_token), [](auto producer, auto consumer) noexcept {
        auto produce = [handle = std::move(producer)]() mutable noexcept {
            const FgCtx ctx = ::foundation::effects::testing::foreground();
            auto head = ::foundation::core::mint_seat(
                ms::mint_metalog_producer_session<PermissionedLog>(ctx, std::move(handle)));
            for (int i = 0; i < kCount; ++i) {
                head.step([&ctx, i](auto&& held) noexcept {
                    return std::move(held).template select<0>(::fixy::session::no_label).send(ctx, make_meta(i + 10));
                });
            }
            auto back = std::move(head).take().template select<1>(::fixy::session::no_label).close();
            const bool has_handle_back = back.size_approx(kSnapshot) <= static_cast<std::uint32_t>(kCount);
            ::foundation::linear::drop(std::move(back).release());
            return ProducerResult{.is_done = true, .has_handle_back = has_handle_back};
        };

        auto consume = [handle = std::move(consumer)]() mutable noexcept {
            const BgCtx ctx{::foundation::effects::testing::bg()};
            int matched = 0;
            auto head = ::foundation::core::mint_seat(
                ms::mint_metalog_consumer_session<PermissionedLog>(ctx, std::move(handle)));
            for (int i = 0; i < kCount; ++i) {
                head.step([&ctx, &matched, i](auto&& held) noexcept {
                    auto [meta, next] = std::move(held).template select<0>(::fixy::session::no_label).recv(ctx);
                    matched += same_meta(meta, make_meta(i + 10)) ? 1 : 0;
                    return std::move(next);
                });
            }
            auto back = std::move(head).take().template select<1>(::fixy::session::no_label).close();
            const int has_handle_back = back.tail_index().raw() == static_cast<std::uint32_t>(kCount) ? 1 : 0;
            ::foundation::linear::drop(std::move(back).release());
            return ConsumerResult{.matched = matched, .has_handle_back = has_handle_back};
        };

        const ::fixy::BgLoadCtx bg{::foundation::effects::testing::bg()};
        auto const [produced, consumed] =
            ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(produce), std::move(consume))
                .expect("the thread door starts the producer and the consumer");

        CRUCIBLE_TEST_ASSERT(kTest, produced.is_done);
        CRUCIBLE_TEST_ASSERT(kTest, produced.has_handle_back);
        CRUCIBLE_TEST_ASSERT(kTest, consumed.has_handle_back == 1);
        CRUCIBLE_TEST_ASSERT(kTest, consumed.matched == kCount);
    });
}

// A session that ends at once gives back a handle that still reaches the
// log, and the moved-from caller variable does not.
void test_session_gives_the_handle_back() {
    const FgCtx fg = ::foundation::effects::testing::foreground();
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();

    cc::with_ends(
        log, std::move(producer_token), std::move(consumer_token), [&fg](auto producer, auto consumer) noexcept {
            auto producer_head = ms::mint_metalog_producer_session<PermissionedLog>(fg, std::move(producer));
            auto consumer_head = ms::mint_metalog_consumer_session<PermissionedLog>(fg, std::move(consumer));
            auto producer_back = std::move(producer_head).template select<1>(::fixy::session::no_label).close();
            auto consumer_back = std::move(consumer_head).template select<1>(::fixy::session::no_label).close();

            CRUCIBLE_TEST_ASSERT(kTest, producer_back.try_append_one_pure(fg, make_meta(7)));
            auto drained = consumer_back.try_drain_one();
            CRUCIBLE_TEST_ASSERT(kTest, drained.is_some());
            CRUCIBLE_TEST_ASSERT(kTest, same_meta(std::move(drained).expect("the log holds one record"), make_meta(7)));
            ::foundation::linear::drop(std::move(producer_back).release());
            ::foundation::linear::drop(std::move(consumer_back).release());
        });
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
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();
    cc::with_ends(log, std::move(producer_token), std::move(consumer_token), [](auto producer, auto consumer) noexcept {
        auto moved = std::move(producer);
        (void)opaque_ref(producer).try_append_one_pure(kPure, make_meta(1));
        ::foundation::linear::drop(std::move(moved).release());
        ::foundation::linear::drop(std::move(consumer).release());
    });
}

void log_consumer() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();
    cc::with_ends(log, std::move(producer_token), std::move(consumer_token), [](auto producer, auto consumer) noexcept {
        auto moved = std::move(consumer);
        (void)opaque_ref(consumer).try_drain_one();
        ::foundation::linear::drop(std::move(producer).release());
        ::foundation::linear::drop(std::move(moved).release());
    });
}

// One call site that runs two times mints two roots of one brand.  The
// claim of each role refuses the second live handle of that role, so the
// second frame of a role, inside the first, ends the process.
void second_live_producer() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [first_producer, first_consumer] = mint_halves();
    auto [second_producer, second_consumer] = mint_halves();
    cc::with_producer(log, std::move(first_producer),
                      [&log, second_producer = std::move(second_producer)](auto held) mutable noexcept {
                          cc::with_producer(log, std::move(second_producer), [](auto second) noexcept {
                              ::foundation::linear::drop(std::move(second).release());
                          });
                          ::foundation::linear::drop(std::move(held).release());
                      });
    ::foundation::linear::drop(std::move(first_consumer));
    ::foundation::linear::drop(std::move(second_consumer));
}

void second_live_consumer() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [first_producer, first_consumer] = mint_halves();
    auto [second_producer, second_consumer] = mint_halves();
    cc::with_consumer(log, std::move(first_consumer),
                      [&log, second_consumer = std::move(second_consumer)](auto held) mutable noexcept {
                          cc::with_consumer(log, std::move(second_consumer), [](auto second) noexcept {
                              ::foundation::linear::drop(std::move(second).release());
                          });
                          ::foundation::linear::drop(std::move(held).release());
                      });
    ::foundation::linear::drop(std::move(first_producer));
    ::foundation::linear::drop(std::move(second_producer));
}

// The consumer drains the first record, and the tail moves past it.  The
// producer can then write a new record into that slot, so a copy of the
// record at index 0 ends the process.
void copy_of_a_released_record() {
    PermissionedLog log{::foundation::effects::testing::test().alloc};
    auto [producer_token, consumer_token] = mint_halves();
    cc::with_ends(log, std::move(producer_token), std::move(consumer_token), [](auto producer, auto consumer) noexcept {
        const ::crucible::TensorMeta record = make_meta(1);
        CRUCIBLE_FATAL_INVARIANT(producer.try_append_one_pure(kPure, record)
                                 && producer.try_append_one_pure(kPure, record));
        CRUCIBLE_FATAL_INVARIANT(consumer.drain([](const ::crucible::TensorMeta&) noexcept {}, 1) == 1);
        (void)consumer.copy_at(::crucible::MetaIndex{0});
        ::foundation::linear::drop(std::move(producer).release());
        ::foundation::linear::drop(std::move(consumer).release());
    });
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
        ::fixy::TextView{
            "foundation: fatal invariant failed: ::crucible::is_at_or_after(index, log->total_released())"});
    ::fixy::check::pass(test, "  moved_from_handles_end_the_process ... OK\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::report(::fixy::Sink::Err, "[test_metalog_session]\n");
    // The attacks fork, so they run before a test starts a thread.
    test_moved_from_handles_end_the_process(test);
    run_test("permissioned_bulk_drain", test_permissioned_bulk_drain);
    run_test("single_append_drain", test_single_append_drain);
    run_test("partial_drain", test_partial_drain);
    run_test("metaindex_propagates", test_metaindex_propagates);
    run_test("empty_drain", test_empty_drain);
    run_test("typed_session_round_trip", test_typed_session_round_trip);
    run_test("session_gives_the_handle_back", test_session_gives_the_handle_back);
    ::fixy::check::pass(test, "\n{} passed, {} failed\n", total_passed, 0);
    return 0;
}
