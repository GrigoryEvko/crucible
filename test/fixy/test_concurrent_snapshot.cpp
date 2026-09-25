// What fixy/concurrent/AtomicSnapshot.h claims, checked at run time.
//
// The stress cases put one writer against several readers.  A seqlock
// with the wrong memory ordering passes every single-threaded case, so
// only a case with real interleavings can catch it.

#include <fixy/concurrent/AtomicSnapshot.h>

#include <foundation/Lifetime.h>
#include <foundation/Platform.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using ::fixy::concurrent::AtomicSnapshot;
using ::fixy::concurrent::SnapshotValue;

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

// The three fields are redundant on purpose.  A reader that sees them
// disagree has caught the writer mid-update, which is a torn read.
//
// At three words the copy spans several stores, so a broken seqlock has
// a wide window in which to show itself.
struct TestPayload {
    std::uint64_t value;
    std::uint64_t value_xor_magic;
    std::uint64_t value_again;

    static constexpr std::uint64_t MAGIC = 0xCAFEBABEDEADBEEFULL;

    [[nodiscard]] bool is_consistent() const noexcept {
        return value == value_again && (value ^ MAGIC) == value_xor_magic;
    }

    [[nodiscard]] static TestPayload from(std::uint64_t v) noexcept { return TestPayload{v, v ^ MAGIC, v}; }
};

static_assert(std::is_trivially_copyable_v<TestPayload>);
static_assert(std::is_trivially_destructible_v<TestPayload>);
static_assert(sizeof(TestPayload) == 24);
static_assert(SnapshotValue<TestPayload>);

static_assert(!std::is_copy_constructible_v<AtomicSnapshot<TestPayload>>,
              "AtomicSnapshot must not be copyable (Pinned contract)");
static_assert(!std::is_copy_assignable_v<AtomicSnapshot<TestPayload>>);
static_assert(!std::is_move_constructible_v<AtomicSnapshot<TestPayload>>,
              "AtomicSnapshot must not be movable (interior atomics)");
static_assert(!std::is_move_assignable_v<AtomicSnapshot<TestPayload>>);

// A trivially copyable class that refuses a lifetime start over bytes is
// no snapshot value, because the read starts a T lifetime over a buffer.
struct [[=::foundation::lifetime::no_start_over_bytes{}]] MarkedCount {
    std::uint64_t count = 0;
};
static_assert(std::is_trivially_copyable_v<MarkedCount> && !SnapshotValue<MarkedCount>,
              "a class marked no_start_over_bytes must not be a snapshot value");

// The size cap and the empty payload.
struct Oversized {
    std::uint64_t words[33];
};
static_assert(!SnapshotValue<Oversized>, "a payload above 256 bytes is refused");
static_assert(SnapshotValue<std::uint8_t>);

void test_default_construction() {
    AtomicSnapshot<TestPayload> snap;

    const auto v = snap.load();
    CRUCIBLE_REQUIRE(v.value == 0);
    CRUCIBLE_REQUIRE(v.value_xor_magic == 0);
    CRUCIBLE_REQUIRE(v.value_again == 0);
    CRUCIBLE_REQUIRE(snap.version() == 0);

    const auto opt = snap.try_load();
    CRUCIBLE_REQUIRE(opt.has_value());
    CRUCIBLE_REQUIRE(opt->value == 0);
}

void test_initial_value_ctor() {
    const auto initial = TestPayload::from(42);
    AtomicSnapshot<TestPayload> snap{initial};

    const auto v = snap.load();
    CRUCIBLE_REQUIRE(v.value == 42);
    CRUCIBLE_REQUIRE(v.is_consistent());
    CRUCIBLE_REQUIRE(snap.version() == 1);  // the constructor counts as one publish
}

void test_roundtrip_single_thread() {
    AtomicSnapshot<TestPayload> snap;

    for (std::uint64_t i = 1; i <= 100; ++i) {
        snap.publish(TestPayload::from(i));

        const auto v = snap.load();
        CRUCIBLE_REQUIRE(v.is_consistent());
        CRUCIBLE_REQUIRE(v.value == i);
        CRUCIBLE_REQUIRE(snap.version() == i);

        const auto opt = snap.try_load();
        CRUCIBLE_REQUIRE(opt.has_value());
        CRUCIBLE_REQUIRE(opt->value == i);
    }
}

void test_version_monotonicity() {
    AtomicSnapshot<TestPayload> snap;

    std::uint64_t prev_version = snap.version();
    for (std::uint64_t i = 0; i < 1000; ++i) {
        snap.publish(TestPayload::from(i));
        const std::uint64_t new_version = snap.version();
        CRUCIBLE_REQUIRE(new_version > prev_version);
        prev_version = new_version;
    }
}

// The wait band pins the reader's strategy at the type and carries the
// same value that load returns.
void test_load_pinned_carries_the_value() {
    AtomicSnapshot<TestPayload> snap{TestPayload::from(7)};
    const auto pinned = snap.load_pinned();
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(pinned)>,
                                 ::fixy::Wait<::fixy::WaitStrategy_v::SpinPause, TestPayload>>);
    CRUCIBLE_REQUIRE(pinned.peek().value == 7);
    CRUCIBLE_REQUIRE(pinned.peek().is_consistent());
}

// The window is sized so that continuous pressure from one writer and
// four readers samples a very large number of interleavings.
void test_stress_multithread() {
    // The readers must start against a consistent value.  An all-zero
    // payload fails the check, because zero exclusive-or the magic is
    // not zero, so a reader that arrives before the first publish would
    // report the legitimate default state as a torn read.
    AtomicSnapshot<TestPayload> snap{TestPayload::from(0)};
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> total_loads{0};
    std::atomic<std::uint64_t> torn_reads{0};
    std::atomic<std::uint64_t> stale_reads{0};

    std::jthread writer([&] {
        std::uint64_t i = 1;
        while (!stop.load(std::memory_order_acquire)) {
            snap.publish(TestPayload::from(i));
            ++i;
        }
    });

    // The writer only publishes increasing values, so each reader must
    // never see a value below the highest it has seen itself.
    constexpr int kReaders = 4;
    std::vector<std::jthread> readers(kReaders);
    for (auto& reader : readers) {
        reader = std::jthread([&] {
            std::uint64_t last_value = 0;
            while (!stop.load(std::memory_order_acquire)) {
                // The parity of the counter alternates the two read
                // paths, so both are exercised.
                if ((total_loads.fetch_add(1, std::memory_order_acq_rel) & 1u) != 0u) {
                    const auto v = snap.load();
                    if (!v.is_consistent()) torn_reads.fetch_add(1, std::memory_order_acq_rel);
                    if (v.value < last_value) {
                        stale_reads.fetch_add(1, std::memory_order_acq_rel);
                    } else {
                        last_value = v.value;
                    }
                } else if (const auto opt = snap.try_load(); opt.has_value()) {
                    if (!opt->is_consistent()) torn_reads.fetch_add(1, std::memory_order_acq_rel);
                    if (opt->value < last_value) {
                        stale_reads.fetch_add(1, std::memory_order_acq_rel);
                    } else {
                        last_value = opt->value;
                    }
                }
            }
        });
    }

    constexpr std::uint64_t kMinStressLoads = 200000;
    const auto stress_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (total_loads.load(std::memory_order_acquire) < kMinStressLoads
           && std::chrono::steady_clock::now() < stress_deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    stop.store(true, std::memory_order_release);
    // A jthread joins in its destructor, and these are destroyed here so
    // the counters below are final.
    readers.clear();
    writer = std::jthread{};

    CRUCIBLE_REQUIRE(torn_reads.load(std::memory_order_acquire) == 0);
    CRUCIBLE_REQUIRE(stale_reads.load(std::memory_order_acquire) == 0);
    CRUCIBLE_REQUIRE(snap.version() > 0);
    CRUCIBLE_REQUIRE(total_loads.load(std::memory_order_acquire) > 1000);
}

// One reader polls the non-blocking read in a tight loop while the
// writer publishes as fast as it can.  The claim is that no read comes
// back inconsistent.  The rate at which reads are refused is not
// asserted: on a fast machine the write window may never overlap a read.
void test_try_load_rejects_in_progress() {
    // The payload sits at the 256-byte maximum, which stretches the
    // writer's copy window and so raises the refusal rate.
    struct LargePayload {
        std::uint64_t header;
        std::uint64_t body[30];
        std::uint64_t trailer;

        [[nodiscard]] bool is_consistent() const noexcept {
            if (header != trailer) return false;
            for (int i = 0; i < 30; ++i) {
                if (body[i] != header + static_cast<std::uint64_t>(i)) return false;
            }
            return true;
        }

        [[nodiscard]] static LargePayload from(std::uint64_t v) noexcept {
            LargePayload p{};
            p.header = v;
            for (int i = 0; i < 30; ++i) {
                p.body[i] = v + static_cast<std::uint64_t>(i);
            }
            p.trailer = v;
            return p;
        }
    };
    static_assert(sizeof(LargePayload) == 256);
    static_assert(SnapshotValue<LargePayload>);

    AtomicSnapshot<LargePayload> snap{LargePayload::from(0)};
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> total{0};
    std::atomic<std::uint64_t> torn{0};

    std::jthread writer([&] {
        std::uint64_t i = 1;
        while (!stop.load(std::memory_order_acquire)) {
            snap.publish(LargePayload::from(i));
            ++i;
        }
    });

    std::jthread reader([&] {
        while (!stop.load(std::memory_order_acquire)) {
            total.fetch_add(1, std::memory_order_acq_rel);
            const auto opt = snap.try_load();
            if (opt.has_value() && !opt->is_consistent()) torn.fetch_add(1, std::memory_order_acq_rel);
        }
    });

    constexpr std::uint64_t kMinLargeLoads = 100000;
    const auto large_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (total.load(std::memory_order_acquire) < kMinLargeLoads
           && std::chrono::steady_clock::now() < large_deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    stop.store(true, std::memory_order_release);
    writer = std::jthread{};
    reader = std::jthread{};

    CRUCIBLE_REQUIRE(torn.load(std::memory_order_acquire) == 0);
    CRUCIBLE_REQUIRE(total.load(std::memory_order_acquire) > 100);
}

}  // namespace

int main() {
    std::fprintf(stderr, "[test_concurrent_snapshot]\n");
    run_test("default_construction", test_default_construction);
    run_test("initial_value_ctor", test_initial_value_ctor);
    run_test("roundtrip_single_thread", test_roundtrip_single_thread);
    run_test("version_monotonicity", test_version_monotonicity);
    run_test("load_pinned_carries_the_value", test_load_pinned_carries_the_value);
    run_test("stress_multithread", test_stress_multithread);
    run_test("try_load_rejects_in_progress", test_try_load_rejects_in_progress);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
