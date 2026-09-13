#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <thread>
#include <type_traits>
#include <vector>

using namespace crucible::concurrent;
using namespace crucible::safety;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

namespace {

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

struct Metrics {
    std::uint64_t requests = 0;
    std::uint64_t errors = 0;
    std::uint64_t latency_ns = 0;
};
static_assert(std::is_trivially_copyable_v<Metrics>);

struct AppMetrics {};
struct LatencyTrack {};
struct ConfigBcast {};

void test_compile_time_properties() {
    using Snap = PermissionedSnapshot<Metrics, AppMetrics>;

    // The atomic state is the channel identity, so a copy or a move
    // would produce a second channel claiming to be the first.
    static_assert(!std::is_copy_constructible_v<Snap>);
    static_assert(!std::is_move_constructible_v<Snap>);

    using W = Snap::WriterHandle;
    using R = Snap::ReaderHandle;
    static_assert(!std::is_copy_constructible_v<W>);
    static_assert(std::is_move_constructible_v<W>);
    static_assert(!std::is_copy_constructible_v<R>);
    static_assert(std::is_move_constructible_v<R>);

    static_assert(sizeof(W) == sizeof(void*), "WriterHandle EBO must collapse Permission<Writer> to 0 bytes");

    // The split of the tag tree is derived, not hand-written.
    static_assert(splits_into_v<snapshot_tag::Whole<AppMetrics>, snapshot_tag::Writer<AppMetrics>,
                                snapshot_tag::Reader<AppMetrics>>);
}

void test_single_thread_publish_and_load() {
    PermissionedSnapshot<Metrics, AppMetrics> snap{};

    auto writer_perm = mint_permission_root<snapshot_tag::Writer<AppMetrics>>();

    auto writer = snap.writer(std::move(writer_perm));
    auto reader_opt = snap.reader();
    CRUCIBLE_TEST_REQUIRE(reader_opt.has_value());

    const auto initial_version = writer.version();

    writer.publish(Metrics{42, 7, 1234});
    CRUCIBLE_TEST_REQUIRE(writer.version() == initial_version + 1);

    const auto loaded = reader_opt->load();
    CRUCIBLE_TEST_REQUIRE(loaded.requests == 42);
    CRUCIBLE_TEST_REQUIRE(loaded.errors == 7);
    CRUCIBLE_TEST_REQUIRE(loaded.latency_ns == 1234);

    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 1);
    CRUCIBLE_TEST_REQUIRE(!snap.is_exclusive_active());

    // The wrapped companions observe the same value, but their type
    // records that the staleness bound is unbounded.  A caller has to
    // peek through the wrapper, which is the acknowledgement that the
    // reading was never synchronized.
    const auto readers_stale = snap.outstanding_readers_stale();
    CRUCIBLE_TEST_REQUIRE(readers_stale.peek() == 1);
    CRUCIBLE_TEST_REQUIRE(readers_stale.is_infinite());
    const auto exclusive_stale = snap.is_exclusive_active_stale();
    CRUCIBLE_TEST_REQUIRE(exclusive_stale.peek() == false);
    CRUCIBLE_TEST_REQUIRE(exclusive_stale.is_infinite());
    static_assert(std::is_same_v<decltype(snap.outstanding_readers_stale()), crucible::safety::Stale<std::uint64_t>>,
                  "outstanding_readers_stale must return Stale<uint64_t>");
    static_assert(std::is_same_v<decltype(snap.is_exclusive_active_stale()), crucible::safety::Stale<bool>>,
                  "is_exclusive_active_stale must return Stale<bool>");
}

void test_multiple_readers_coexist() {
    PermissionedSnapshot<Metrics, AppMetrics> snap{Metrics{1, 2, 3}};

    auto r1 = snap.reader();
    auto r2 = snap.reader();
    auto r3 = snap.reader();

    CRUCIBLE_TEST_REQUIRE(r1.has_value());
    CRUCIBLE_TEST_REQUIRE(r2.has_value());
    CRUCIBLE_TEST_REQUIRE(r3.has_value());
    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 3);

    CRUCIBLE_TEST_REQUIRE(r1->load().requests == 1);
    CRUCIBLE_TEST_REQUIRE(r2->load().errors == 2);
    CRUCIBLE_TEST_REQUIRE(r3->load().latency_ns == 3);
}

void test_reader_handle_destruction_decrements() {
    PermissionedSnapshot<Metrics, AppMetrics> snap{};

    {
        auto r = snap.reader();
        CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 1);
    }
    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 0);
}

void test_with_drained_access_succeeds_when_idle() {
    PermissionedSnapshot<Metrics, AppMetrics> snap{Metrics{0, 0, 0}};
    auto writer = snap.writer(mint_permission_root<snapshot_tag::Writer<AppMetrics>>());

    bool body_ran = false;
    const bool ok = snap.with_drained_access([&] {
        body_ran = true;
        writer.publish(Metrics{99, 99, 99});
    });

    CRUCIBLE_TEST_REQUIRE(ok);
    CRUCIBLE_TEST_REQUIRE(body_ran);

    auto r = snap.reader();
    CRUCIBLE_TEST_REQUIRE(r.has_value());
    CRUCIBLE_TEST_REQUIRE(r->load().requests == 99);
}

void test_with_drained_access_fails_when_readers_present() {
    PermissionedSnapshot<Metrics, AppMetrics> snap{};
    auto reader = snap.reader();
    CRUCIBLE_TEST_REQUIRE(reader.has_value());
    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 1);

    bool body_ran = false;
    const bool ok = snap.with_drained_access([&] { body_ran = true; });

    CRUCIBLE_TEST_REQUIRE(!ok);
    CRUCIBLE_TEST_REQUIRE(!body_ran);
    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 1);
}

// The writer only ever publishes a pair whose halves are equal, so a
// reader that observes lo != hi has observed a torn read.  The payload
// carries no atomics of its own: every ordering guarantee under test
// comes from the snapshot.
struct CounterPair {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
};
static_assert(std::is_trivially_copyable_v<CounterPair>);

// The seqlock copies the payload while a publish may be in flight and
// relies on its sequence retry to reject an incoherent result, rather
// than preventing the racing copy.  The lo == hi check after every load
// is therefore the thing that actually proves the protocol sound at the
// user-visible boundary.  A race detector flags the inner copy whatever
// the protocol does, and the suite carries a suppression for it that
// applies when the tests run under the thread-sanitizer preset.

void test_swmr_under_load() {
    constexpr int NUM_READERS = 8;
    constexpr int NUM_PUBLISHES = 10000;

    PermissionedSnapshot<CounterPair, LatencyTrack> snap{CounterPair{0, 0}};

    auto writer_perm = mint_permission_root<snapshot_tag::Writer<LatencyTrack>>();

    std::atomic<int> publishes_done{0};
    std::atomic<bool> writer_done{false};
    std::atomic<int> torn_reads_observed{0};
    std::atomic<std::uint64_t> total_loads{0};

    // Without this barrier, on a contended host the writer can finish
    // its whole publish loop before any reader thread reaches its own
    // loop.  Every reader then sees writer_done on its first check,
    // exits, and the total_loads check at the end fails on a run that
    // proved nothing.  All readers and the writer rendezvous here, so
    // at least one load overlaps at least one publish.  The gate
    // synchronizes start of life only: correctness during the loop
    // comes from the pool refcount and the seqlock retry.
    std::latch start_gate{NUM_READERS + 1};

    std::jthread writer_t([&snap, &writer_perm, &publishes_done, &writer_done, &start_gate](std::stop_token) noexcept {
        auto handle = snap.writer(std::move(writer_perm));
        start_gate.arrive_and_wait();
        for (int i = 1; i <= NUM_PUBLISHES; ++i) {
            const std::uint64_t v = static_cast<std::uint64_t>(i);
            handle.publish(CounterPair{v, v});
            publishes_done.fetch_add(1, std::memory_order_acq_rel);
        }
        writer_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> readers;
    for (int i = 0; i < NUM_READERS; ++i) {
        readers.emplace_back(
            [&snap, &writer_done, &torn_reads_observed, &total_loads, &start_gate](std::stop_token) noexcept {
                start_gate.arrive_and_wait();
                while (!writer_done.load(std::memory_order_acquire)) {
                    total_loads.fetch_add(1, std::memory_order_relaxed);

                    auto r = snap.reader();
                    if (!r) {
                        // Only exclusive mode denies a reader, and nothing
                        // in this test enters it.
                        CRUCIBLE_SPIN_PAUSE;
                        continue;
                    }
                    const auto pair = r->load();
                    if (pair.lo != pair.hi) {
                        torn_reads_observed.fetch_add(1, std::memory_order_acq_rel);
                    }
                }
            });
    }

    writer_t.join();
    for (auto& r : readers)
        r.join();

    CRUCIBLE_TEST_REQUIRE(publishes_done.load() == NUM_PUBLISHES);
    CRUCIBLE_TEST_REQUIRE(torn_reads_observed.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_loads.load() > 0);
    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 0);
}

void test_mint_permission_fork_integration() {
    PermissionedSnapshot<std::uint64_t, ConfigBcast> snap{0};

    // Only the writer permission is minted here.  The reader side of
    // the split is parked in the pool by the snapshot's constructor.
    auto writer_perm = mint_permission_root<snapshot_tag::Writer<ConfigBcast>>();

    std::atomic<int> reader_observations{0};

    {
        auto writer = snap.writer(std::move(writer_perm));
        for (std::uint64_t v = 100; v <= 110; ++v) {
            writer.publish(v);
        }
    }

    // The writer's permission is consumed when its scope ends, and the
    // snapshot outlives it.
    auto r = snap.reader();
    CRUCIBLE_TEST_REQUIRE(r.has_value());
    CRUCIBLE_TEST_REQUIRE(r->load() == 110);
    reader_observations.fetch_add(1, std::memory_order_acq_rel);

    CRUCIBLE_TEST_REQUIRE(reader_observations.load() == 1);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_permissioned_snapshot:\n");

    test_compile_time_properties();  // pure compile-time

    run_test("test_single_thread_publish_and_load", test_single_thread_publish_and_load);
    run_test("test_multiple_readers_coexist", test_multiple_readers_coexist);
    run_test("test_reader_handle_destruction_decrements", test_reader_handle_destruction_decrements);
    run_test("test_with_drained_access_succeeds_when_idle", test_with_drained_access_succeeds_when_idle);
    run_test("test_with_drained_access_fails_when_readers_present",
             test_with_drained_access_fails_when_readers_present);
    run_test("test_swmr_under_load", test_swmr_under_load);
    run_test("test_mint_permission_fork_integration", test_mint_permission_fork_integration);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
