// What fixy/channels/SwmrSession.h claims, checked at run time.
//
// Each check moves real values through a real channel.  A session
// protocol loops with no exit, so each session ends with a typed detach,
// which releases the handle that the session owns.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/channels/SwmrSession.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/HandleTraits.h>

#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace perm = ::foundation::permissions;
namespace ses = ::fixy::channels::swmr_session;
namespace s = ::fixy::session;

using FgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;

struct WriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};

// One call site mints every reader root of a session type, so every
// session of the type carries the brand of that one site.
[[nodiscard]] auto reader_root() noexcept { return perm::mint_permission_root<ReaderTag>(); }

// The same holds for the writer root.  The session type names the brand
// of this one site, so a writer root from another site is refused.
[[nodiscard]] auto writer_root() noexcept { return perm::mint_permission_root<WriterTag>(); }

using Swmr = ses::SwmrSession<int, WriterTag, ReaderTag, ::foundation::brand::brand_of_t<decltype(reader_root())>,
                              ::foundation::brand::brand_of_t<decltype(writer_root())>>;

struct PayloadTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct PayloadReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};

struct SnapshotPayload {
    std::uint64_t seq = 0;
    std::uint64_t checksum = ~std::uint64_t{0};

    [[nodiscard]] constexpr bool is_consistent() const noexcept { return checksum == ~seq; }
};

[[nodiscard]] auto payload_reader_root() noexcept { return perm::mint_permission_root<PayloadReaderTag>(); }
[[nodiscard]] auto payload_writer_root() noexcept { return perm::mint_permission_root<PayloadTag>(); }

using PayloadSwmr = ses::SwmrSession<SnapshotPayload, PayloadTag, PayloadReaderTag,
                                     ::foundation::brand::brand_of_t<decltype(payload_reader_root())>,
                                     ::foundation::brand::brand_of_t<decltype(payload_writer_root())>>;

[[nodiscard]] constexpr SnapshotPayload payload_at(std::uint64_t seq) noexcept {
    return SnapshotPayload{.seq = seq, .checksum = ~seq};
}

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_REQUIRE(cond)                                                                 \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
            ++total_failed;                                                                    \
            return;                                                                            \
        }                                                                                      \
    } while (0)

template <typename Body>
void run_test(char const* name, Body body) {
    std::fprintf(stderr, "  %s ... ", name);
    int const before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        std::fprintf(stderr, "OK\n");
    } else {
        std::fprintf(stderr, "FAILED\n");
    }
}

// A writer body ends its linear handle on each path, so it gives its checks
// back as values, and the test reads them after the frame.  A reader handle
// is not linear, so a reader body checks in place.

void test_writer_publish_reader_loads_latest() {
    Swmr swmr{reader_root(), 7};
    // The reader frame opens inside the frame of the writer.  A body takes no
    // linear value by reference, so the reader body takes the version of the
    // writer as a value.
    bool const is_lent = ses::with_writer(swmr, writer_root(), [&swmr](auto writer) noexcept {
        writer.publish(42);
        std::uint64_t const writer_version = writer.version();
        bool const is_reader_lent = ses::with_reader(kTest, swmr, [&swmr, writer_version](auto const reader) noexcept {
                                        CRUCIBLE_REQUIRE(reader.load(kTest) == 42);
                                        CRUCIBLE_REQUIRE(reader.version() == writer_version);
                                        CRUCIBLE_REQUIRE(swmr.version() == writer_version);
                                    }).is_some();
        ::foundation::linear::drop(std::move(writer).release());
        return is_reader_lent;
    });
    CRUCIBLE_REQUIRE(is_lent);
}

// True when the reader root came out of the pool of the session.  The root
// goes back to the pool at once.
[[nodiscard]] bool can_withdraw_reader_root(Swmr& swmr) noexcept {
    return swmr.try_withdraw_reader_root(kTest).match(
        [&swmr](auto root) noexcept {
            swmr.deposit_reader_root(std::move(root));
            return true;
        },
        []() noexcept { return false; });
}

void test_multiple_readers_track_pool_lifetime() {
    Swmr swmr{reader_root(), 1};

    // The second reader frame opens inside the first, and each reader gives
    // its share back at the end of its frame, the inner one first.
    auto const first = ses::with_reader(kTest, swmr, [&swmr](auto /*first_reader*/) noexcept {
        auto const second = ses::with_reader(kTest, swmr, [&swmr](auto /*second_reader*/) noexcept {
            CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 2);
            CRUCIBLE_REQUIRE(!can_withdraw_reader_root(swmr));
        });
        CRUCIBLE_REQUIRE(second.is_some());
        CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 1);
    });
    CRUCIBLE_REQUIRE(first.is_some());
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 0);

    // With no reader out, the root comes out, and the pool lends no reader
    // while it is out.
    auto root = swmr.try_withdraw_reader_root(kTest).expect("the root comes out with no reader out");
    bool const is_root_out = swmr.is_exclusive_active();
    bool const is_lend_refused = ses::with_reader(kTest, swmr, [](auto /*reader*/) noexcept {}).is_none();
    swmr.deposit_reader_root(std::move(root));
    CRUCIBLE_REQUIRE(is_root_out);
    CRUCIBLE_REQUIRE(is_lend_refused);
    CRUCIBLE_REQUIRE(!swmr.is_exclusive_active());
    CRUCIBLE_REQUIRE(ses::with_reader(kTest, swmr, [](auto /*reader*/) noexcept {}).is_some());
}

void test_late_reader_observes_latest_publish() {
    Swmr swmr{reader_root(), 3};

    // Each reader frame opens inside the frame of the writer.  Each reader
    // handle ends at the end of its frame, and it gives its share back there.
    auto [early, late] = ses::with_writer(swmr, writer_root(), [&swmr](auto writer) noexcept {
        auto early_load = ses::with_reader(kTest, swmr, [](auto const reader) noexcept { return reader.load(kTest); });
        writer.publish(99);
        auto late_load = ses::with_reader(kTest, swmr, [](auto const reader) noexcept { return reader.load(kTest); });
        ::foundation::linear::drop(std::move(writer).release());
        return ::fixy::Record{std::move(early_load), std::move(late_load)};
    });
    CRUCIBLE_REQUIRE(early.is_some());
    CRUCIBLE_REQUIRE(early == ::fixy::some(3));
    CRUCIBLE_REQUIRE(late.is_some());
    CRUCIBLE_REQUIRE(late == ::fixy::some(99));
}

// A moved-from handle keeps no binding, so the handle it moved to is the
// only one that reaches the channel.
void test_moved_handles_keep_their_channel() {
    Swmr swmr{reader_root(), 5};

    // The reader frame opens inside the frame of the writer, and it takes the
    // moved writer by move, so the second writer opens while the reader lives.
    auto seen = ses::with_writer(swmr, writer_root(), [&swmr](auto writer) noexcept {
        auto moved_writer = std::move(writer);
        moved_writer.publish(6);
        return ses::with_reader(
            kTest, swmr, [&swmr, moved_writer = std::move(moved_writer)](auto reader) mutable noexcept {
                auto const moved_reader = std::move(reader);
                int const first_load = moved_reader.load(kTest);
                std::uint64_t const outstanding = swmr.outstanding_readers();

                // The release door gives the writer role and its token back,
                // and the token opens a new writer handle with no second claim.
                auto [second_token, second_load] = ses::with_writer(
                    swmr, std::move(moved_writer).release(), [&moved_reader](auto second_writer) noexcept {
                        second_writer.publish(8);
                        int const load = moved_reader.load(kTest);
                        return ::fixy::Record{std::move(second_writer).release(), load};
                    });
                ::foundation::linear::drop(std::move(second_token));
                return ::fixy::Record{first_load, outstanding, second_load};
            });
    });
    CRUCIBLE_REQUIRE(seen.is_some());
    auto const [first_load, outstanding, second_load] = std::move(seen).expect("the pool lends a share");
    CRUCIBLE_REQUIRE(first_load == 6);
    CRUCIBLE_REQUIRE(outstanding == 1);
    CRUCIBLE_REQUIRE(second_load == 8);
}

void test_session_send_recv() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    Swmr swmr{reader_root(), 0};

    // Each session owns the handle of its frame, so it ends in that frame.
    // The reader frame opens inside the frame of the writer.
    auto const received = ses::with_writer(swmr, writer_root(), [&ctx, &swmr](auto writer) noexcept {
        auto writer_session = ses::mint_writer_runtime_session<Swmr>(ctx, std::move(writer));
        auto next_writer = std::move(writer_session).send(ctx, 91);
        // The read of a reader session takes Nondet, and the foreground
        // context holds no atom, so the reader session runs in the context of
        // the test.
        auto value = ses::with_reader(kTest, swmr, [](auto reader) noexcept {
            auto reader_session = ses::mint_reader_runtime_session<Swmr>(kTest, std::move(reader));
            auto [read, next_reader] = std::move(reader_session).recv(kTest);
            std::move(next_reader).detach(s::detach_reason::InfiniteLoopProtocol{});
            return read;
        });
        std::move(next_writer).detach(s::detach_reason::InfiniteLoopProtocol{});
        return value;
    });

    CRUCIBLE_REQUIRE(received.is_some());
    CRUCIBLE_REQUIRE(received == ::fixy::some(91));
}

// Opens Count reader frames, each inside the last, and calls check with a
// reference to each reader handle in the innermost frame.  It gives false
// when the pool lends no share to one of the frames.
template <std::size_t Count, class Session, class Check, class... Readers>
[[nodiscard]] bool with_nested_readers(Session& swmr, Check& check, Readers const&... readers) noexcept {
    if constexpr (Count == 0) {
        check(readers...);
        return true;
    } else {
        return ses::with_reader(kTest, swmr,
                                [&swmr, &check, &readers...](auto const reader) noexcept {
                                    return with_nested_readers<Count - 1>(swmr, check, readers..., reader);
                                })
            == ::fixy::some(true);
    }
}

void test_writer_publishes_1000_values_four_readers_observe_sequence() {
    constexpr std::uint64_t kPublishes = 1000;
    constexpr std::size_t kReaders = 4;

    PayloadSwmr swmr{payload_reader_root(), payload_at(0)};

    // The writer frame opens inside the four reader frames, so each reader
    // loads after each publish.  The writer body gives back whether each
    // reader saw each value.
    auto observe = [&swmr](auto const&... readers) noexcept {
        CRUCIBLE_REQUIRE(swmr.outstanding_readers() == kReaders);
        bool const is_sequence_seen =
            ses::with_writer(swmr, payload_writer_root(), [&readers...](auto writer) noexcept {
                bool is_seen = true;
                for (std::uint64_t seq = 1; seq <= kPublishes && is_seen; ++seq) {
                    writer.publish(payload_at(seq));
                    auto const sees = [seq](auto const& reader) noexcept {
                        SnapshotPayload const observed = reader.load(kTest);
                        return observed.is_consistent() && observed.seq == seq;
                    };
                    is_seen = (sees(readers) && ...);
                }
                ::foundation::linear::drop(std::move(writer).release());
                return is_seen;
            });
        CRUCIBLE_REQUIRE(is_sequence_seen);
    };
    CRUCIBLE_REQUIRE(with_nested_readers<kReaders>(swmr, observe));
}

void test_async_interleaving_never_observes_torn_or_reversed_state() {
    constexpr std::uint64_t kPublishes = 25000;
    constexpr std::size_t kReaders = 4;

    PayloadSwmr swmr{payload_reader_root(), payload_at(0)};

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::array<std::uint64_t, kReaders> final_seen{};
    std::array<std::jthread, kReaders> reader_threads{};

    // Each thread opens the frame of its reader, and the reader loads in that
    // frame.
    for (std::size_t idx = 0; idx < kReaders; ++idx) {
        reader_threads[idx] = std::jthread{[&, idx] {
            auto const lent = ses::with_reader(kTest, swmr, [&, idx](auto const reader) noexcept {
                while (!start.load(std::memory_order_acquire)) {
                    CRUCIBLE_SPIN_PAUSE;
                }
                std::uint64_t last = 0;
                while (!done.load(std::memory_order_acquire)) {
                    const SnapshotPayload observed = reader.load(kTest);
                    if (!observed.is_consistent() || observed.seq < last) {
                        failed.store(true, std::memory_order_release);
                        return;
                    }
                    last = observed.seq;
                    if ((idx & 1U) != 0U) CRUCIBLE_SPIN_PAUSE;
                }
                const SnapshotPayload tail = reader.load(kTest);
                if (!tail.is_consistent() || tail.seq < last) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
                final_seen[idx] = tail.seq;
            });
            if (lent.is_none()) failed.store(true, std::memory_order_release);
        }};
    }

    start.store(true, std::memory_order_release);
    auto writer_token = ses::with_writer(swmr, payload_writer_root(), [](auto writer) noexcept {
        for (std::uint64_t seq = 1; seq <= kPublishes; ++seq) {
            writer.publish(payload_at(seq));
            if ((seq & 0x3fU) == 0U) CRUCIBLE_SPIN_PAUSE;
        }
        return std::move(writer).release();
    });
    ::foundation::linear::drop(std::move(writer_token));
    done.store(true, std::memory_order_release);

    for (auto& reader : reader_threads)
        reader.join();

    CRUCIBLE_REQUIRE(!failed.load(std::memory_order_acquire));
    for (std::uint64_t seen : final_seen) {
        CRUCIBLE_REQUIRE(seen <= kPublishes);
    }
}

void test_reader_exit_and_rejoin_updates_pool_and_observes_current() {
    PayloadSwmr swmr{payload_reader_root(), payload_at(0)};

    // The reader leaves at the end of its frame and gives its share back.  The
    // writer frame opens inside the frame of the reader, so the reader loads
    // after each publish.  The writer gives its token back at the end of its
    // frame, and the token opens the writer of the next publishes.
    auto first = ses::with_reader(kTest, swmr, [&swmr](auto const reader) noexcept {
        bool const is_one_out = swmr.outstanding_readers() == 1;
        auto [token, is_each_seen] = ses::with_writer(swmr, payload_writer_root(), [&reader](auto writer) noexcept {
            bool is_seen = true;
            for (std::uint64_t seq = 1; seq <= 100 && is_seen; ++seq) {
                writer.publish(payload_at(seq));
                const SnapshotPayload observed = reader.load(kTest);
                is_seen = observed.is_consistent() && observed.seq == seq;
            }
            return ::fixy::Record{std::move(writer).release(), is_seen};
        });
        return ::fixy::Record{std::move(token), is_one_out, is_each_seen};
    });
    CRUCIBLE_REQUIRE(first.is_some());
    auto [writer_token, is_one_out, is_each_seen] = std::move(first).expect("the pool lends a share");
    bool const is_none_out = swmr.outstanding_readers() == 0;

    auto last_token = ses::with_writer(swmr, std::move(writer_token), [](auto writer) noexcept {
        for (std::uint64_t seq = 101; seq <= 150; ++seq) {
            writer.publish(payload_at(seq));
        }
        return std::move(writer).release();
    });
    ::foundation::linear::drop(std::move(last_token));
    CRUCIBLE_REQUIRE(is_one_out);
    CRUCIBLE_REQUIRE(is_each_seen);
    CRUCIBLE_REQUIRE(is_none_out);

    auto rejoined = ses::with_reader(kTest, swmr, [&swmr](auto const reader) noexcept {
        return ::fixy::Record{swmr.outstanding_readers(), reader.load(kTest)};
    });
    CRUCIBLE_REQUIRE(rejoined.is_some());
    auto const [outstanding, observed] = std::move(rejoined).expect("the pool lends a share");
    CRUCIBLE_REQUIRE(outstanding == 1);
    CRUCIBLE_REQUIRE(observed.is_consistent());
    CRUCIBLE_REQUIRE(observed.seq == 150);
}

void test_sixteen_readers_stress_latest_snapshot() {
    constexpr std::uint64_t kPublishes = 100000;
    constexpr std::size_t kReaders = 16;

    PayloadSwmr swmr{payload_reader_root(), payload_at(0)};

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::vector<std::jthread> readers(kReaders);

    // Each thread opens the frame of its reader, and the reader loads in that
    // frame.
    for (std::size_t idx = 0; idx < kReaders; ++idx) {
        readers[idx] = std::jthread([&, idx] {
            auto const lent = ses::with_reader(kTest, swmr, [&, idx](auto const reader) noexcept {
                while (!start.load(std::memory_order_acquire)) {
                    CRUCIBLE_SPIN_PAUSE;
                }
                std::uint64_t last = 0;
                for (std::uint64_t iter = 0; iter < kPublishes; ++iter) {
                    const SnapshotPayload observed = reader.load(kTest);
                    if (!observed.is_consistent() || observed.seq < last) {
                        failed.store(true, std::memory_order_release);
                        return;
                    }
                    last = observed.seq;
                    if ((idx + iter) % 4096 == 0) CRUCIBLE_SPIN_PAUSE;
                    if (done.load(std::memory_order_acquire) && observed.seq == kPublishes) {
                        break;
                    }
                }
            });
            if (lent.is_none()) failed.store(true, std::memory_order_release);
        });
    }

    start.store(true, std::memory_order_release);
    auto writer_token = ses::with_writer(swmr, payload_writer_root(), [](auto writer) noexcept {
        for (std::uint64_t seq = 1; seq <= kPublishes; ++seq) {
            writer.publish(payload_at(seq));
        }
        return std::move(writer).release();
    });
    ::foundation::linear::drop(std::move(writer_token));
    done.store(true, std::memory_order_release);

    for (auto& reader : readers)
        reader.join();

    CRUCIBLE_REQUIRE(!failed.load(std::memory_order_acquire));
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 0);
}

// The writer door takes a permission of the brand the session names, and a
// permission of another brand opens no writer.  The body gives the token
// back.
template <typename Session, typename Brand>
concept MintsWriterOfBrand = requires(Session& session) {
    ses::with_writer(session, std::declval<perm::Permission<typename Session::writer_tag, Brand>&&>(),
                     [](auto writer) noexcept { return std::move(writer).release(); });
};

// A scope brand that the checks name, and the frame handles on it.  No frame
// door gives a handle on it.
struct CheckScope {};

void test_static_shape_witnesses() {
    struct OtherBrand {};
    using WriterHandle = Swmr::writer_handle<CheckScope>;
    using ReaderHandle = Swmr::reader_handle<CheckScope>;

    static_assert(std::is_same_v<WriterHandle::brand_type, Swmr::writer_brand>,
                  "the writer handle carries the brand the session names");
    static_assert(MintsWriterOfBrand<Swmr, Swmr::writer_brand>);
    static_assert(!MintsWriterOfBrand<Swmr, OtherBrand>, "a second root makes no second writer");

    static_assert(::fixy::concurrent::is_swmr_writer_v<WriterHandle>);
    static_assert(::fixy::concurrent::is_swmr_reader_v<ReaderHandle>);
    static_assert(!::fixy::concurrent::is_swmr_reader_v<WriterHandle>);
    static_assert(!::fixy::concurrent::is_swmr_writer_v<ReaderHandle>);
    static_assert(std::is_same_v<::fixy::concurrent::swmr_writer_value_t<WriterHandle>, int>);
    static_assert(std::is_same_v<::fixy::concurrent::swmr_reader_value_t<ReaderHandle>, int>);

    static_assert(ses::CtxFitsSwmrWriterSession<FgCtx, Swmr, WriterHandle>);
    // The read of a reader session takes Nondet, so the foreground context,
    // which holds no atom, opens no reader session.
    static_assert(ses::CtxFitsSwmrReaderSession<::fixy::TestRunnerCtx, Swmr, ReaderHandle>);
    static_assert(!ses::CtxFitsSwmrReaderSession<FgCtx, Swmr, ReaderHandle>, "a reader session reads the schedule");
    static_assert(!ses::CtxFitsSwmrWriterSession<FgCtx, Swmr, ReaderHandle>, "a reader handle is not a writer");
    static_assert(!ses::CtxFitsSwmrReaderSession<::fixy::TestRunnerCtx, Swmr, WriterHandle>,
                  "a writer handle is not a reader");
    static_assert(!ses::CtxFitsSwmrWriterSession<FgCtx, int, WriterHandle>, "an int is not a channel");

    // The writer door gives its body the writer handle of the session.
    Swmr swmr{reader_root(), 0};
    auto token = ses::with_writer(swmr, writer_root(), [](auto writer) noexcept {
        static_assert(ses::SwmrWriterHandleOf<Swmr, decltype(writer)>);
        return std::move(writer).release();
    });
    ::foundation::linear::drop(std::move(token));

    CRUCIBLE_REQUIRE(true);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_concurrent_swmr_session]\n");
    run_test("writer_publish_reader_loads_latest", test_writer_publish_reader_loads_latest);
    run_test("multiple_readers_track_pool_lifetime", test_multiple_readers_track_pool_lifetime);
    run_test("late_reader_observes_latest_publish", test_late_reader_observes_latest_publish);
    run_test("moved_handles_keep_their_channel", test_moved_handles_keep_their_channel);
    run_test("session_send_recv", test_session_send_recv);
    run_test("writer_publishes_1000_values_four_readers_observe_sequence",
             test_writer_publishes_1000_values_four_readers_observe_sequence);
    run_test("async_interleaving_never_observes_torn_or_reversed_state",
             test_async_interleaving_never_observes_torn_or_reversed_state);
    run_test("reader_exit_and_rejoin_updates_pool_and_observes_current",
             test_reader_exit_and_rejoin_updates_pool_and_observes_current);
    run_test("sixteen_readers_stress_latest_snapshot", test_sixteen_readers_stress_latest_snapshot);
    run_test("static_shape_witnesses", test_static_shape_witnesses);

    ::fixy::check::pass(kTest, "\n{} passed, {} failed\n", total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
