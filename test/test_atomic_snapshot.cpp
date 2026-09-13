#include <crucible/concurrent/AtomicSnapshot.h>

#include <atomic>
#include "test_assert.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

using namespace crucible::concurrent;

// The three fields are redundant on purpose.  A reader that sees them
// disagree has caught the writer mid-update, which is a torn read.
//
// At three words the copy spans several stores, so a broken seqlock has
// a wide window in which to show itself.

struct TestPayload {
    uint64_t value;
    uint64_t value_xor_magic;
    uint64_t value_again;

    static constexpr uint64_t MAGIC = 0xCAFEBABEDEADBEEFULL;

    [[nodiscard]] bool is_consistent() const noexcept {
        return value == value_again && (value ^ MAGIC) == value_xor_magic;
    }

    [[nodiscard]] static TestPayload from(uint64_t v) noexcept { return TestPayload{v, v ^ MAGIC, v}; }
};

static_assert(std::is_trivially_copyable_v<TestPayload>);
static_assert(std::is_trivially_destructible_v<TestPayload>);
static_assert(sizeof(TestPayload) == 24);

static_assert(!std::is_copy_constructible_v<AtomicSnapshot<TestPayload>>,
              "AtomicSnapshot must not be copyable (Pinned contract)");
static_assert(!std::is_copy_assignable_v<AtomicSnapshot<TestPayload>>);
static_assert(!std::is_move_constructible_v<AtomicSnapshot<TestPayload>>,
              "AtomicSnapshot must not be movable (interior atomics)");
static_assert(!std::is_move_assignable_v<AtomicSnapshot<TestPayload>>);

static void test_default_construction() {
    AtomicSnapshot<TestPayload> snap;

    const auto v = snap.load();
    assert(v.value == 0);
    assert(v.value_xor_magic == 0);
    assert(v.value_again == 0);
    assert(snap.version() == 0);

    const auto opt = snap.try_load();
    assert(opt.has_value());
    assert(opt->value == 0);

    std::printf("  test_default_construction: PASSED\n");
}

static void test_initial_value_ctor() {
    const auto initial = TestPayload::from(42);
    AtomicSnapshot<TestPayload> snap{initial};

    const auto v = snap.load();
    assert(v.value == 42);
    assert(v.is_consistent());
    assert(snap.version() == 1);  // one "publish" via ctor

    std::printf("  test_initial_value_ctor: PASSED\n");
}

static void test_roundtrip_single_thread() {
    AtomicSnapshot<TestPayload> snap;

    for (uint64_t i = 1; i <= 100; ++i) {
        snap.publish(TestPayload::from(i));

        const auto v = snap.load();
        assert(v.is_consistent());
        assert(v.value == i);
        assert(snap.version() == i);

        const auto opt = snap.try_load();
        assert(opt.has_value());
        assert(opt->value == i);
    }

    std::printf("  test_roundtrip_single_thread: PASSED\n");
}

static void test_version_monotonicity() {
    AtomicSnapshot<TestPayload> snap;

    uint64_t prev_version = snap.version();
    for (uint64_t i = 0; i < 1000; ++i) {
        snap.publish(TestPayload::from(i));
        const uint64_t new_version = snap.version();
        assert(new_version > prev_version);
        prev_version = new_version;
    }

    std::printf("  test_version_monotonicity: PASSED\n");
}

// A seqlock with the wrong memory ordering passes every single-threaded
// case forever, then corrupts data on the first interleaving that hits
// the race.  Only this case can catch that.  A bug shows up here as
// an inconsistent payload: a reader saw the copy mid-write and the retry
// protocol failed to discard it.
//
// The window is sized so that continuous pressure from one writer and
// four readers samples a very large number of interleavings.

static void test_stress_multithread() {
    std::printf("  test_stress_multithread: running 200ms @ 4 readers...\n");

    // The readers must start against a consistent value.  An all-zero
    // payload fails the check, because zero exclusive-or the magic is
    // not zero, so a reader that arrives before the first publish would
    // report the legitimate default state as a torn read.  The writer
    // starts at one, so the seed is zero.
    AtomicSnapshot<TestPayload> snap{TestPayload::from(0)};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total_loads{0};
    std::atomic<uint64_t> torn_reads{0};
    std::atomic<uint64_t> stale_reads{0};  // value not monotonic
    std::atomic<uint64_t> total_try_loads{0};
    std::atomic<uint64_t> nullopt_try_loads{0};

    std::jthread writer([&](std::stop_token /*st*/) {
        uint64_t i = 1;
        while (!stop.load(std::memory_order_acquire)) {
            snap.publish(TestPayload::from(i));
            ++i;
        }
    });

    // The writer only publishes increasing values, so each reader must
    // never see a value below the highest it has seen itself.  The bound
    // is per reader, not global.
    constexpr int kReaders = 4;
    std::vector<std::jthread> readers;
    for (int r = 0; r < kReaders; ++r) {
        readers.emplace_back([&](std::stop_token /*st*/) {
            uint64_t last_value = 0;
            while (!stop.load(std::memory_order_acquire)) {
                // The parity of the counter alternates the two read
                // paths, so both are exercised.
                if (((total_loads.fetch_add(1, std::memory_order_relaxed)) & 1u) != 0u) {
                    const auto v = snap.load();
                    if (!v.is_consistent()) {
                        torn_reads.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (v.value < last_value) {
                        stale_reads.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        last_value = v.value;
                    }
                } else {
                    total_try_loads.fetch_add(1, std::memory_order_relaxed);
                    const auto opt = snap.try_load();
                    if (opt.has_value()) {
                        if (!opt->is_consistent()) {
                            torn_reads.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (opt->value < last_value) {
                            stale_reads.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            last_value = opt->value;
                        }
                    } else {
                        nullopt_try_loads.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }

    constexpr uint64_t kMinStressLoads = 200'000;
    const auto stress_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (total_loads.load(std::memory_order_acquire) < kMinStressLoads
           && std::chrono::steady_clock::now() < stress_deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    stop.store(true, std::memory_order_release);
    // A jthread joins in its destructor, and these are destroyed here
    // so the counters below are final.
    readers.clear();
    writer = std::jthread{};

    const uint64_t loads = total_loads.load(std::memory_order_relaxed);
    const uint64_t try_loads = total_try_loads.load(std::memory_order_relaxed);
    const uint64_t torn = torn_reads.load(std::memory_order_relaxed);
    const uint64_t stale = stale_reads.load(std::memory_order_relaxed);
    const uint64_t nullopt_cnt = nullopt_try_loads.load(std::memory_order_relaxed);
    const uint64_t final_version = snap.version();

    std::printf("    published: %llu (version=%llu)\n"
                "    total load() + try_load(): %llu\n"
                "    try_load()=nullopt: %llu / %llu (%.2f%% on conflicts)\n"
                "    torn reads observed: %llu  ← MUST be 0\n"
                "    non-monotonic reads: %llu  ← MUST be 0\n",
                static_cast<unsigned long long>(final_version), static_cast<unsigned long long>(final_version),
                static_cast<unsigned long long>(loads), static_cast<unsigned long long>(nullopt_cnt),
                static_cast<unsigned long long>(try_loads),
                try_loads > 0 ? 100.0 * static_cast<double>(nullopt_cnt) / static_cast<double>(try_loads) : 0.0,
                static_cast<unsigned long long>(torn), static_cast<unsigned long long>(stale));

    assert(torn == 0 && "torn read observed — seqlock memory ordering is broken");
    assert(stale == 0 && "non-monotonic read observed — writer uniqueness violated?");
    assert(final_version > 0 && "writer never published anything?");
    assert(loads > 1000 && "not enough reader activity for meaningful coverage");

    std::printf("  test_stress_multithread: PASSED\n");
}

// One reader polls the non-blocking read in a tight loop while the
// writer publishes as fast as it can.  The claim is that no read comes
// back inconsistent.  The rate at which reads are refused is reported
// but not asserted.

static void test_try_load_rejects_in_progress() {
    std::printf("  test_try_load_rejects_in_progress: running 100ms...\n");

    // The payload sits at the 256-byte maximum, which stretches the
    // writer's copy window and so raises the refusal rate.  A local
    // class may not hold a constexpr static member, so the consistency
    // check reads the header and trailer bytes instead of a magic
    // constant.
    struct LargePayload {
        uint64_t header;
        uint64_t body[30];  // total 31 × 8 = 248 B
        uint64_t trailer;

        [[nodiscard]] bool is_consistent() const noexcept {
            if (header != trailer) return false;
            for (int i = 0; i < 30; ++i) {
                if (body[i] != header + static_cast<uint64_t>(i)) return false;
            }
            return true;
        }

        [[nodiscard]] static LargePayload from(uint64_t v) noexcept {
            LargePayload p{};
            p.header = v;
            for (int i = 0; i < 30; ++i) {
                p.body[i] = v + static_cast<uint64_t>(i);
            }
            p.trailer = v;
            return p;
        }
    };
    static_assert(sizeof(LargePayload) == 256);
    static_assert(std::is_trivially_copyable_v<LargePayload>);

    // Seeded with a consistent value for the same reason as the case
    // above: a read that arrives before the first publish must not look
    // torn.
    AtomicSnapshot<LargePayload> snap{LargePayload::from(0)};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<uint64_t> torn{0};

    std::jthread writer([&](std::stop_token /*st*/) {
        uint64_t i = 1;
        while (!stop.load(std::memory_order_acquire)) {
            snap.publish(LargePayload::from(i));
            ++i;
        }
    });

    std::jthread reader([&](std::stop_token /*st*/) {
        while (!stop.load(std::memory_order_acquire)) {
            total.fetch_add(1, std::memory_order_relaxed);
            const auto opt = snap.try_load();
            if (!opt.has_value()) {
                rejected.fetch_add(1, std::memory_order_relaxed);
            } else if (!opt->is_consistent()) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    constexpr uint64_t kMinLargeLoads = 100'000;
    const auto large_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (total.load(std::memory_order_acquire) < kMinLargeLoads
           && std::chrono::steady_clock::now() < large_deadline) {
        CRUCIBLE_SPIN_PAUSE;
    }
    stop.store(true, std::memory_order_release);
    writer = std::jthread{};
    reader = std::jthread{};

    const uint64_t t = total.load(std::memory_order_relaxed);
    const uint64_t r = rejected.load(std::memory_order_relaxed);
    const uint64_t trn = torn.load(std::memory_order_relaxed);

    std::printf("    try_load() calls: %llu, rejected: %llu (%.3f%%), torn: %llu\n", static_cast<unsigned long long>(t),
                static_cast<unsigned long long>(r),
                t > 0 ? 100.0 * static_cast<double>(r) / static_cast<double>(t) : 0.0,
                static_cast<unsigned long long>(trn));

    assert(trn == 0 && "torn read from try_load — protocol broken");
    assert(t > 100 && "not enough reader activity");
    // The refusal count is not asserted.  On a fast enough machine the
    // writer's mid-publish window may never overlap a reader's sample.
    // The absence of a torn read is the claim here.

    std::printf("  test_try_load_rejects_in_progress: PASSED\n");
}

int main() {
    std::printf("test_atomic_snapshot:\n");

    test_default_construction();
    test_initial_value_ctor();
    test_roundtrip_single_thread();
    test_version_monotonicity();
    test_stress_multithread();
    test_try_load_rejects_in_progress();

    std::printf("test_atomic_snapshot: ALL PASSED\n");
    return 0;
}
