// What fixy/concurrent/SwmrSession.h claims, checked at run time.
//
// Each check moves real values through a real channel.  A session
// protocol loops with no exit, so each session ends with a typed detach,
// which releases the handle that the session owns.

#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/SwmrSession.h>

#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace perm = ::foundation::permissions;
namespace ses = ::fixy::concurrent::swmr_session;
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

void test_writer_publish_reader_loads_latest() {
    Swmr swmr{reader_root(), 7};
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, writer_root());

    writer.publish(42);

    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(reader.has_value());
    CRUCIBLE_REQUIRE(reader->load() == 42);
    CRUCIBLE_REQUIRE(reader->version() == writer.version());
    CRUCIBLE_REQUIRE(swmr.version() == writer.version());
}

void test_multiple_readers_track_pool_lifetime() {
    Swmr swmr{reader_root(), 1};

    auto r1 = ses::mint_swmr_reader<Swmr>(swmr);
    auto r2 = ses::mint_swmr_reader<Swmr>(swmr);

    CRUCIBLE_REQUIRE(r1.has_value());
    CRUCIBLE_REQUIRE(r2.has_value());
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 2);
    CRUCIBLE_REQUIRE(!swmr.with_drained_access([] {}));

    r1.reset();
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 1);

    r2.reset();
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 0);

    bool ran = false;
    CRUCIBLE_REQUIRE(swmr.with_drained_access([&ran] { ran = true; }));
    CRUCIBLE_REQUIRE(ran);
}

void test_late_reader_observes_latest_publish() {
    Swmr swmr{reader_root(), 3};
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, writer_root());

    auto early = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(early.has_value());
    CRUCIBLE_REQUIRE(early->load() == 3);
    early.reset();

    writer.publish(99);

    auto late = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(late.has_value());
    CRUCIBLE_REQUIRE(late->load() == 99);
}

// A moved-from handle keeps no binding, so the handle it moved to is the
// only one that reaches the channel.
void test_moved_handles_keep_their_channel() {
    Swmr swmr{reader_root(), 5};
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, writer_root());
    auto moved_writer = std::move(writer);
    moved_writer.publish(6);

    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(reader.has_value());
    Swmr::ReaderHandle moved_reader = std::move(*reader);
    CRUCIBLE_REQUIRE(moved_reader.load() == 6);
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 1);
}

void test_session_send_recv() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    Swmr swmr{reader_root(), 0};
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, writer_root());
    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(reader.has_value());

    auto writer_session = ses::mint_writer_runtime_session<Swmr>(ctx, std::move(writer));
    auto reader_session = ses::mint_reader_runtime_session<Swmr>(ctx, std::move(*reader));

    auto next_writer = std::move(writer_session).send(91, ses::publish_value);
    auto [value, next_reader] = std::move(reader_session).recv(ses::load_value);

    CRUCIBLE_REQUIRE(value == 91);

    std::move(next_writer).detach(s::detach_reason::InfiniteLoopProtocol{});
    std::move(next_reader).detach(s::detach_reason::InfiniteLoopProtocol{});
}

void test_writer_publishes_1000_values_four_readers_observe_sequence() {
    constexpr std::uint64_t kPublishes = 1000;
    constexpr std::size_t kReaders = 4;

    PayloadSwmr swmr{payload_reader_root(), payload_at(0)};
    auto writer = ses::mint_swmr_writer<PayloadSwmr>(swmr, payload_writer_root());

    std::array<std::optional<PayloadSwmr::ReaderHandle>, kReaders> readers{};
    for (auto& reader : readers) {
        auto minted = ses::mint_swmr_reader<PayloadSwmr>(swmr);
        CRUCIBLE_REQUIRE(minted.has_value());
        reader.emplace(std::move(*minted));
    }
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == kReaders);

    for (std::uint64_t seq = 1; seq <= kPublishes; ++seq) {
        writer.publish(payload_at(seq));
        for (auto const& reader : readers) {
            const SnapshotPayload observed = reader->load();
            CRUCIBLE_REQUIRE(observed.is_consistent());
            CRUCIBLE_REQUIRE(observed.seq == seq);
        }
    }
}

void test_async_interleaving_never_observes_torn_or_reversed_state() {
    constexpr std::uint64_t kPublishes = 25000;
    constexpr std::size_t kReaders = 4;

    PayloadSwmr swmr{payload_reader_root(), payload_at(0)};
    auto writer = ses::mint_swmr_writer<PayloadSwmr>(swmr, payload_writer_root());

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::array<std::uint64_t, kReaders> final_seen{};
    std::array<std::jthread, kReaders> reader_threads{};

    for (std::size_t idx = 0; idx < kReaders; ++idx) {
        reader_threads[idx] = std::jthread{[&, idx] {
            auto reader = ses::mint_swmr_reader<PayloadSwmr>(swmr);
            if (!reader) {
                failed.store(true, std::memory_order_release);
                return;
            }
            while (!start.load(std::memory_order_acquire)) {
                CRUCIBLE_SPIN_PAUSE;
            }
            std::uint64_t last = 0;
            while (!done.load(std::memory_order_acquire)) {
                const SnapshotPayload observed = reader->load();
                if (!observed.is_consistent() || observed.seq < last) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
                last = observed.seq;
                if ((idx & 1U) != 0U) CRUCIBLE_SPIN_PAUSE;
            }
            const SnapshotPayload tail = reader->load();
            if (!tail.is_consistent() || tail.seq < last) {
                failed.store(true, std::memory_order_release);
                return;
            }
            final_seen[idx] = tail.seq;
        }};
    }

    start.store(true, std::memory_order_release);
    for (std::uint64_t seq = 1; seq <= kPublishes; ++seq) {
        writer.publish(payload_at(seq));
        if ((seq & 0x3fU) == 0U) CRUCIBLE_SPIN_PAUSE;
    }
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
    auto writer = ses::mint_swmr_writer<PayloadSwmr>(swmr, payload_writer_root());

    auto reader = ses::mint_swmr_reader<PayloadSwmr>(swmr);
    CRUCIBLE_REQUIRE(reader.has_value());
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 1);

    for (std::uint64_t seq = 1; seq <= 100; ++seq) {
        writer.publish(payload_at(seq));
        const SnapshotPayload observed = reader->load();
        CRUCIBLE_REQUIRE(observed.is_consistent());
        CRUCIBLE_REQUIRE(observed.seq == seq);
    }

    reader.reset();
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 0);

    for (std::uint64_t seq = 101; seq <= 150; ++seq) {
        writer.publish(payload_at(seq));
    }

    auto rejoined = ses::mint_swmr_reader<PayloadSwmr>(swmr);
    CRUCIBLE_REQUIRE(rejoined.has_value());
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 1);

    const SnapshotPayload observed = rejoined->load();
    CRUCIBLE_REQUIRE(observed.is_consistent());
    CRUCIBLE_REQUIRE(observed.seq == 150);
}

void test_sixteen_readers_stress_latest_snapshot() {
    constexpr std::uint64_t kPublishes = 100000;
    constexpr std::size_t kReaders = 16;

    PayloadSwmr swmr{payload_reader_root(), payload_at(0)};
    auto writer = ses::mint_swmr_writer<PayloadSwmr>(swmr, payload_writer_root());

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::vector<std::jthread> readers(kReaders);

    for (std::size_t idx = 0; idx < kReaders; ++idx) {
        readers[idx] = std::jthread([&, idx] {
            auto reader = ses::mint_swmr_reader<PayloadSwmr>(swmr);
            if (!reader) {
                failed.store(true, std::memory_order_release);
                return;
            }
            while (!start.load(std::memory_order_acquire)) {
                CRUCIBLE_SPIN_PAUSE;
            }
            std::uint64_t last = 0;
            for (std::uint64_t iter = 0; iter < kPublishes; ++iter) {
                const SnapshotPayload observed = reader->load();
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
    }

    start.store(true, std::memory_order_release);
    for (std::uint64_t seq = 1; seq <= kPublishes; ++seq) {
        writer.publish(payload_at(seq));
    }
    done.store(true, std::memory_order_release);

    for (auto& reader : readers)
        reader.join();

    CRUCIBLE_REQUIRE(!failed.load(std::memory_order_acquire));
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 0);
}

// The writer takes a permission of the brand the session names, and a
// permission of another brand makes no writer.
template <typename Session, typename Brand>
concept MintsWriterOfBrand = requires(Session& session) {
    ses::mint_swmr_writer<Session>(session, std::declval<perm::Permission<typename Session::writer_tag, Brand>&&>());
};

void test_static_shape_witnesses() {
    struct OtherBrand {};
    using WriterHandle = Swmr::WriterHandle;
    using ReaderHandle = Swmr::ReaderHandle;

    static_assert(std::is_same_v<WriterHandle::brand_type, Swmr::writer_brand>,
                  "the writer handle carries the brand the session names");
    static_assert(std::is_same_v<decltype(ses::mint_swmr_writer<Swmr>(
                                     std::declval<Swmr&>(),
                                     std::declval<perm::Permission<Swmr::writer_tag, Swmr::writer_brand>&&>())),
                                 WriterHandle>);
    static_assert(MintsWriterOfBrand<Swmr, Swmr::writer_brand>);
    static_assert(!MintsWriterOfBrand<Swmr, OtherBrand>, "a second root makes no second writer");

    static_assert(::fixy::concurrent::is_swmr_writer_v<WriterHandle>);
    static_assert(::fixy::concurrent::is_swmr_reader_v<ReaderHandle>);
    static_assert(!::fixy::concurrent::is_swmr_reader_v<WriterHandle>);
    static_assert(!::fixy::concurrent::is_swmr_writer_v<ReaderHandle>);
    static_assert(std::is_same_v<::fixy::concurrent::swmr_writer_value_t<WriterHandle>, int>);
    static_assert(std::is_same_v<::fixy::concurrent::swmr_reader_value_t<ReaderHandle>, int>);

    static_assert(ses::CtxFitsSwmrWriterSession<FgCtx, Swmr, WriterHandle>);
    static_assert(ses::CtxFitsSwmrReaderSession<FgCtx, Swmr, ReaderHandle>);
    static_assert(!ses::CtxFitsSwmrWriterSession<FgCtx, Swmr, ReaderHandle>, "a reader handle is not a writer");
    static_assert(!ses::CtxFitsSwmrReaderSession<FgCtx, Swmr, WriterHandle>, "a writer handle is not a reader");
    static_assert(!ses::CtxFitsSwmrWriterSession<FgCtx, int, WriterHandle>, "an int is not a channel");

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

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
