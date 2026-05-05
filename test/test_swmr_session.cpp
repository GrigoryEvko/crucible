// SwmrSession.h integration test (GAPS-019 / GAPS-020).
//
// Runtime coverage for the session-shaped facade over AtomicSnapshot
// + SharedPermissionPool: writer publication, multiple outstanding
// readers, late-reader visibility, drained exclusive access, and
// PermissionedSessionHandle send/recv wrappers.

#include <crucible/permissions/Permission.h>
#include <crucible/safety/IsSwmrHandle.h>
#include <crucible/sessions/SwmrSession.h>

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

namespace safety = ::crucible::safety;
namespace ses = ::crucible::safety::proto::swmr_session;
namespace proto = ::crucible::safety::proto;
namespace extract = ::crucible::safety::extract;

struct WriterTag {};
struct ReaderTag {};

using Swmr = ses::SwmrSession<int, WriterTag, ReaderTag>;

struct PayloadTag {};
struct PayloadReaderTag {};

struct SnapshotPayload {
    std::uint64_t seq = 0;
    std::uint64_t checksum = ~std::uint64_t{0};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return checksum == ~seq;
    }
};

using PayloadSwmr =
    ses::SwmrSession<SnapshotPayload, PayloadTag, PayloadReaderTag>;

[[nodiscard]] constexpr SnapshotPayload payload_at(std::uint64_t seq) noexcept {
    return SnapshotPayload{.seq = seq, .checksum = ~seq};
}

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_REQUIRE(cond)                                            \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "  REQUIRE FAILED: %s @ %s:%d\n",        \
                         #cond, __FILE__, __LINE__);                      \
            ++total_failed;                                               \
            return;                                                       \
        }                                                                 \
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
    Swmr swmr{7};
    auto writer_perm = safety::mint_permission_root<Swmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, std::move(writer_perm));

    writer.publish(42);

    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(reader.has_value());
    CRUCIBLE_REQUIRE(reader->load() == 42);
    CRUCIBLE_REQUIRE(reader->version() == writer.version());
    CRUCIBLE_REQUIRE(swmr.version() == writer.version());
}

void test_multiple_readers_track_pool_lifetime() {
    Swmr swmr{1};

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
    Swmr swmr{3};
    auto writer_perm = safety::mint_permission_root<Swmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, std::move(writer_perm));

    auto early = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(early.has_value());
    CRUCIBLE_REQUIRE(early->load() == 3);
    early.reset();

    writer.publish(99);

    auto late = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(late.has_value());
    CRUCIBLE_REQUIRE(late->load() == 99);
}

void test_reader_mint_accepts_matching_shared_permission_proof() {
    Swmr swmr{12};
    auto bootstrap = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(bootstrap.has_value());

    auto reader = ses::mint_swmr_reader<Swmr>(swmr, bootstrap->token());
    CRUCIBLE_REQUIRE(reader.has_value());
    CRUCIBLE_REQUIRE(reader->load() == 12);
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 2);
}

void test_typed_session_send_recv() {
    using proto::detach_reason::TestInstrumentation;

    Swmr swmr{0};
    auto writer_perm = safety::mint_permission_root<Swmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, std::move(writer_perm));
    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(reader.has_value());

    auto writer_psh = ses::mint_writer_session<Swmr>(writer);
    auto reader_psh = ses::mint_reader_session<Swmr>(*reader);

    auto next_writer = std::move(writer_psh).send(77, ses::publish_value);
    auto [borrowed, next_reader] =
        std::move(reader_psh).recv(ses::load_borrowed_value);

    CRUCIBLE_REQUIRE(borrowed.value == 77);

    std::move(next_writer).detach(TestInstrumentation{});
    std::move(next_reader).detach(TestInstrumentation{});
}

void test_writer_publishes_1000_values_four_readers_observe_sequence() {
    constexpr std::uint64_t kPublishes = 1000;
    constexpr std::size_t kReaders = 4;

    PayloadSwmr swmr{payload_at(0)};
    auto writer_perm = safety::mint_permission_root<PayloadSwmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<PayloadSwmr>(
        swmr, std::move(writer_perm));

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
            CRUCIBLE_REQUIRE(observed.valid());
            CRUCIBLE_REQUIRE(observed.seq == seq);
        }
    }
}

void test_async_interleaving_never_observes_torn_or_reversed_state() {
    constexpr std::uint64_t kPublishes = 25'000;
    constexpr std::size_t kReaders = 4;

    PayloadSwmr swmr{payload_at(0)};
    auto writer_perm = safety::mint_permission_root<PayloadSwmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<PayloadSwmr>(
        swmr, std::move(writer_perm));

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::array<std::uint64_t, kReaders> final_seen{};
    std::array<std::thread, kReaders> reader_threads{};

    for (std::size_t idx = 0; idx < kReaders; ++idx) {
        reader_threads[idx] = std::thread{[&, idx] {
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
                if (!observed.valid() || observed.seq < last) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
                last = observed.seq;
                if ((idx & 1U) != 0U) std::this_thread::yield();
            }
            const SnapshotPayload tail = reader->load();
            if (!tail.valid() || tail.seq < last) {
                failed.store(true, std::memory_order_release);
                return;
            }
            final_seen[idx] = tail.seq;
        }};
    }

    start.store(true, std::memory_order_release);
    for (std::uint64_t seq = 1; seq <= kPublishes; ++seq) {
        writer.publish(payload_at(seq));
        if ((seq & 0x3fU) == 0U) std::this_thread::yield();
    }
    done.store(true, std::memory_order_release);

    for (auto& reader : reader_threads) reader.join();

    CRUCIBLE_REQUIRE(!failed.load(std::memory_order_acquire));
    for (std::uint64_t seen : final_seen) {
        CRUCIBLE_REQUIRE(seen <= kPublishes);
    }
}

void test_late_reader_after_500_publishes_gets_latest_value() {
    PayloadSwmr swmr{payload_at(0)};
    auto writer_perm = safety::mint_permission_root<PayloadSwmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<PayloadSwmr>(
        swmr, std::move(writer_perm));

    for (std::uint64_t seq = 1; seq <= 500; ++seq) {
        writer.publish(payload_at(seq));
    }

    auto late = ses::mint_swmr_reader<PayloadSwmr>(swmr);
    CRUCIBLE_REQUIRE(late.has_value());
    const SnapshotPayload observed = late->load();
    CRUCIBLE_REQUIRE(observed.valid());
    CRUCIBLE_REQUIRE(observed.seq == 500);
}

void test_reader_exit_and_rejoin_updates_pool_and_observes_current() {
    PayloadSwmr swmr{payload_at(0)};
    auto writer_perm = safety::mint_permission_root<PayloadSwmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<PayloadSwmr>(
        swmr, std::move(writer_perm));

    auto reader = ses::mint_swmr_reader<PayloadSwmr>(swmr);
    CRUCIBLE_REQUIRE(reader.has_value());
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 1);

    for (std::uint64_t seq = 1; seq <= 100; ++seq) {
        writer.publish(payload_at(seq));
        const SnapshotPayload observed = reader->load();
        CRUCIBLE_REQUIRE(observed.valid());
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
    CRUCIBLE_REQUIRE(observed.valid());
    CRUCIBLE_REQUIRE(observed.seq == 150);
}

void test_sixteen_readers_stress_latest_snapshot() {
    constexpr std::uint64_t kPublishes = 100'000;
    constexpr std::size_t kReaders = 16;

    PayloadSwmr swmr{payload_at(0)};
    auto writer_perm = safety::mint_permission_root<PayloadSwmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<PayloadSwmr>(
        swmr, std::move(writer_perm));

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::vector<std::thread> readers;
    readers.reserve(kReaders);

    for (std::size_t idx = 0; idx < kReaders; ++idx) {
        readers.emplace_back([&, idx] {
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
                if (!observed.valid() || observed.seq < last) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
                last = observed.seq;
                if ((idx + iter) % 4096 == 0) std::this_thread::yield();
                if (done.load(std::memory_order_acquire) &&
                    observed.seq == kPublishes) {
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

    for (auto& reader : readers) reader.join();

    CRUCIBLE_REQUIRE(!failed.load(std::memory_order_acquire));
    CRUCIBLE_REQUIRE(swmr.outstanding_readers() == 0);
}

void test_runtime_session_aliases_remain_available() {
    using proto::detach_reason::TestInstrumentation;

    Swmr swmr{0};
    auto writer_perm = safety::mint_permission_root<Swmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, std::move(writer_perm));
    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    CRUCIBLE_REQUIRE(reader.has_value());

    auto writer_psh = ses::mint_writer_runtime_session<Swmr>(writer);
    auto reader_psh = ses::mint_reader_runtime_session<Swmr>(*reader);

    auto next_writer = std::move(writer_psh).send(91, ses::publish_value);
    auto [value, next_reader] = std::move(reader_psh).recv(ses::load_value);

    CRUCIBLE_REQUIRE(value == 91);

    std::move(next_writer).detach(TestInstrumentation{});
    std::move(next_reader).detach(TestInstrumentation{});
}

void test_static_shape_witnesses() {
    using WriterHandle = Swmr::WriterHandle;
    using ReaderHandle = Swmr::ReaderHandle;

    static_assert(extract::is_swmr_writer_v<WriterHandle>);
    static_assert(extract::is_swmr_reader_v<ReaderHandle>);
    static_assert(!extract::is_swmr_reader_v<WriterHandle>);
    static_assert(!extract::is_swmr_writer_v<ReaderHandle>);
    static_assert(std::is_same_v<extract::swmr_writer_value_t<WriterHandle>, int>);
    static_assert(std::is_same_v<extract::swmr_reader_value_t<ReaderHandle>, int>);

    static_assert(std::is_same_v<
        ses::WriterProto<int>,
        proto::Loop<proto::Send<proto::ContentAddressed<int>, proto::Continue>>>);
    static_assert(std::is_same_v<
        ses::ReaderProto<int, ReaderTag>,
        proto::Loop<proto::Recv<proto::Borrowed<int, ReaderTag>, proto::Continue>>>);
    static_assert(std::is_same_v<ses::WriterRuntimeProto<int>,
                                 proto::Loop<proto::Send<int, proto::Continue>>>);
    static_assert(std::is_same_v<ses::ReaderRuntimeProto<int>,
                                 proto::Loop<proto::Recv<int, proto::Continue>>>);

    CRUCIBLE_REQUIRE(true);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_swmr_session]\n");
    run_test("writer_publish_reader_loads_latest",
             test_writer_publish_reader_loads_latest);
    run_test("multiple_readers_track_pool_lifetime",
             test_multiple_readers_track_pool_lifetime);
    run_test("late_reader_observes_latest_publish",
             test_late_reader_observes_latest_publish);
    run_test("reader_mint_accepts_matching_shared_permission_proof",
             test_reader_mint_accepts_matching_shared_permission_proof);
    run_test("typed_session_send_recv", test_typed_session_send_recv);
    run_test("writer_publishes_1000_values_four_readers_observe_sequence",
             test_writer_publishes_1000_values_four_readers_observe_sequence);
    run_test("async_interleaving_never_observes_torn_or_reversed_state",
             test_async_interleaving_never_observes_torn_or_reversed_state);
    run_test("late_reader_after_500_publishes_gets_latest_value",
             test_late_reader_after_500_publishes_gets_latest_value);
    run_test("reader_exit_and_rejoin_updates_pool_and_observes_current",
             test_reader_exit_and_rejoin_updates_pool_and_observes_current);
    run_test("sixteen_readers_stress_latest_snapshot",
             test_sixteen_readers_stress_latest_snapshot);
    run_test("runtime_session_aliases_remain_available",
             test_runtime_session_aliases_remain_available);
    run_test("static_shape_witnesses", test_static_shape_witnesses);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
