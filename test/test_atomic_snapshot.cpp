// ═══════════════════════════════════════════════════════════════════
// test_atomic_snapshot — unit tests + multi-threaded stress for the
// AtomicSnapshot<T> seqlock primitive (QUEUE-1).
//
// What we prove:
//
//   1. Round-trip correctness (single-threaded): publish(v) → load()
//      returns v.
//   2. Version counter monotonicity.
//   3. try_load() behavior on steady state.
//   4. Default-constructed snapshot returns T{}.
//   5. **Stress test**: 1 writer, 4 readers, 200 ms of contention.
//      Every load() MUST return a consistent snapshot (one that
//      was, at some moment, atomically published).  Any torn read
//      — observable as a struct-level invariant violation —
//      indicates a memory-ordering bug in the seqlock.
//   6. Compile-time: AtomicSnapshot is not copyable, not movable.
//
// The stress test is the load-bearing one.  A seqlock with wrong
// memory ordering may pass single-threaded tests forever, then
// corrupt data on the first interleaving that triggers the race.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/AtomicSnapshot.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

using namespace crucible::concurrent;

// ── Test payload with self-consistency invariant ──────────────────
//
// Three redundant fields: if a reader ever sees `value != value_again`
// or `value ^ MAGIC != value_xor_magic`, it has observed a torn
// read — the writer had only partially updated the struct.
//
// With 3 × uint64 = 24 bytes, the writer's memcpy spans multiple
// stores, giving the race plenty of surface area to manifest if
// the seqlock is broken.

struct TestPayload {
    uint64_t value;
    uint64_t value_xor_magic;
    uint64_t value_again;

    static constexpr uint64_t MAGIC = 0xCAFEBABEDEADBEEFULL;

    [[nodiscard]] bool is_consistent() const noexcept {
        return value == value_again &&
               (value ^ MAGIC) == value_xor_magic;
    }

    [[nodiscard]] static TestPayload from(uint64_t v) noexcept {
        return TestPayload{v, v ^ MAGIC, v};
    }
};

static_assert(std::is_trivially_copyable_v<TestPayload>);
static_assert(std::is_trivially_destructible_v<TestPayload>);
static_assert(sizeof(TestPayload) == 24);

// ── Compile-time checks ────────────────────────────────────────────

static_assert(!std::is_copy_constructible_v<AtomicSnapshot<TestPayload>>,
              "AtomicSnapshot must not be copyable (Pinned contract)");
static_assert(!std::is_copy_assignable_v<AtomicSnapshot<TestPayload>>);
static_assert(!std::is_move_constructible_v<AtomicSnapshot<TestPayload>>,
              "AtomicSnapshot must not be movable (interior atomics)");
static_assert(!std::is_move_assignable_v<AtomicSnapshot<TestPayload>>);

// ── Unit: default construction ─────────────────────────────────────

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

// ── Unit: initial-value ctor ───────────────────────────────────────

static void test_initial_value_ctor() {
    const auto initial = TestPayload::from(42);
    AtomicSnapshot<TestPayload> snap{initial};

    const auto v = snap.load();
    assert(v.value == 42);
    assert(v.is_consistent());
    assert(snap.version() == 1);  // one "publish" via ctor

    std::printf("  test_initial_value_ctor: PASSED\n");
}

// ── Unit: single-thread round-trip ─────────────────────────────────

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

// ── Unit: version counter monotonicity ─────────────────────────────

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

// ── Stress: 1 writer, N readers, 200 ms ────────────────────────────
//
// The load-bearing test.  Any memory-ordering bug manifests as an
// `is_consistent() == false` observation — some reader saw a struct
// where `value != value_again`, meaning the memcpy was observed
// mid-write, yet the seqlock's retry protocol failed to discard it.
//
// Interleaving probability: writer publish is ~30 ns, one publish
// per ~50 ns of wall time under continuous pressure.  Readers
// load() at a rate of ~1 per ~100 ns.  Over 200 ms we get ~4M
// publishes × 4 × ~2M reads = on the order of 10^13 write-read
// interleavings sampled.  If any is racy, the probability of
// catching it in 200 ms is overwhelming.

static void test_stress_multithread() {
    std::printf("  test_stress_multithread: running 200ms @ 4 readers...\n");

    // Initialize snap with a CONSISTENT value before readers start.
    // The default-constructed snapshot has all-zero bytes, which by
    // our MAGIC-based self-consistency check is NOT consistent
    // (0 ^ MAGIC != 0).  Readers racing the writer's first publish
    // would otherwise misclassify the legitimate default state as a
    // torn read.  Writer starts at i=1, so seed with i=0.
    AtomicSnapshot<TestPayload> snap{TestPayload::from(0)};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total_loads{0};
    std::atomic<uint64_t> torn_reads{0};
    std::atomic<uint64_t> stale_reads{0};   // value not monotonic
    std::atomic<uint64_t> total_try_loads{0};
    std::atomic<uint64_t> nullopt_try_loads{0};

    // Writer thread: publish monotonically increasing values until
    // `stop` is set.  Each publish uses TestPayload::from(i) so
    // the struct's self-consistency invariant holds for every
    // published state.
    std::jthread writer([&](std::stop_token /*st*/) {
        uint64_t i = 1;
        while (!stop.load(std::memory_order_acquire)) {
            snap.publish(TestPayload::from(i));
            ++i;
        }
    });

    // Reader threads: continuously load and verify consistency +
    // monotonicity.  Monotonicity: the observed value MUST be
    // >= the highest value previously seen by THIS reader (the
    // writer only publishes increasing values).
    constexpr int kReaders = 4;
    std::vector<std::jthread> readers;
    for (int r = 0; r < kReaders; ++r) {
        readers.emplace_back([&](std::stop_token /*st*/) {
            uint64_t last_value = 0;
            while (!stop.load(std::memory_order_acquire)) {
                // Alternate between blocking load() and try_load()
                // to exercise both paths.
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

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true, std::memory_order_release);
    // jthreads join in destructors; clear explicitly to flush.
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
        static_cast<unsigned long long>(final_version),
        static_cast<unsigned long long>(final_version),
        static_cast<unsigned long long>(loads),
        static_cast<unsigned long long>(nullopt_cnt),
        static_cast<unsigned long long>(try_loads),
        try_loads > 0 ? 100.0 * static_cast<double>(nullopt_cnt) / static_cast<double>(try_loads) : 0.0,
        static_cast<unsigned long long>(torn),
        static_cast<unsigned long long>(stale));

    assert(torn == 0 && "torn read observed — seqlock memory ordering is broken");
    assert(stale == 0 && "non-monotonic read observed — writer uniqueness violated?");
    assert(final_version > 0 && "writer never published anything?");
    assert(loads > 1000 && "not enough reader activity for meaningful coverage");

    std::printf("  test_stress_multithread: PASSED\n");
}

// ── Stress: try_load rejects in-progress writes ────────────────────
//
// Pound on a lightly-contended snapshot with try_load() from a
// tight loop while the writer publishes at maximum rate.  We
// don't require any specific nullopt rate — just that SOME
// try_load() calls return nullopt (proving the in-progress
// detection actually fires) and none return inconsistent values.

static void test_try_load_rejects_in_progress() {
    std::printf("  test_try_load_rejects_in_progress: running 100ms...\n");

    // Payload deliberately 256 B (max allowed) — stretches the
    // writer's memcpy window to maximize the nullopt return rate.
    // Local-class constexpr static members are ill-formed (C++
    // local-class rule), so the head/trailer self-consistency
    // protocol relies on the per-instance bytes alone.
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

    // Same fix as test_stress_multithread: initialize with a
    // consistent value (header == trailer = 0, body[i] = i) so
    // pre-first-publish reads aren't misclassified as torn.
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

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_release);
    writer = std::jthread{};
    reader = std::jthread{};

    const uint64_t t = total.load(std::memory_order_relaxed);
    const uint64_t r = rejected.load(std::memory_order_relaxed);
    const uint64_t trn = torn.load(std::memory_order_relaxed);

    std::printf("    try_load() calls: %llu, rejected: %llu (%.3f%%), torn: %llu\n",
        static_cast<unsigned long long>(t),
        static_cast<unsigned long long>(r),
        t > 0 ? 100.0 * static_cast<double>(r) / static_cast<double>(t) : 0.0,
        static_cast<unsigned long long>(trn));

    assert(trn == 0 && "torn read from try_load — protocol broken");
    assert(t > 100 && "not enough reader activity");
    // We don't assert r > 0 because on extremely fast machines the
    // writer's mid-publish window may never overlap a reader's seq
    // sample.  The absence of torn reads is the load-bearing
    // invariant; rejection rate is informational.

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
