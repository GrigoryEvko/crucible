// Cookie-fingerprint correctness for the concurrent primitives.
//
// ThreadSanitizer detects races.  This suite detects corruption.  Every
// concurrent payload carries an FNV-1a digest over its own fields, so a
// primitive that lets bytes from two epochs mix produces a cookie
// mismatch.  A false positive needs an accidental 64-bit hash collision,
// so a failure here is always a real bug.
//
// Each scheduler policy's queue_template resolves to one of the
// permissioned wrappers that the tests above it drive directly, but with
// a different UserTag.  The tag participates in the wrapper's permission
// tree, so the resulting types are genuinely distinct and a mis-wired
// policy alias survives the direct-instantiation tests.  The policy
// tests re-run the same workload through the alias to catch that.

#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/concurrent/ChaseLevDeque.h>
#include <crucible/concurrent/MpmcRing.h>
#include <crucible/concurrent/_MpscRing.h>
#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/_PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/concurrent/_SpscRing.h>
#include <crucible/concurrent/scheduler/Policies.h>
#include <crucible/Arena.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/safety/_OwnedRegion.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/safety/Workload.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <latch>
#include <memory>
#include <set>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace crucible;
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

inline effects::Alloc test_alloc_token_() noexcept { return effects::testing::test().alloc; }

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    std::fflush(stderr);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

// Spelled out here rather than reused from the project hash helpers, so
// that the fuzzer does not depend on a header it is not testing.
[[nodiscard]] constexpr std::uint64_t fnv1a64_(const void* data, std::size_t len) noexcept {
    constexpr std::uint64_t FNV_BASIS = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t FNV_PRIME = 0x100000001b3ULL;
    std::uint64_t h = FNV_BASIS;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        h ^= bytes[i];
        h *= FNV_PRIME;
    }
    return h;
}

// The multiplier and increment are the MMIX linear-congruential
// constants.  Successive bytes are uncorrelated enough that a payload
// assembled from two different seeds fails the recomputation below.
template <std::size_t N>
constexpr void derive_payload_(std::uint64_t seed, std::uint8_t (&out)[N]) noexcept {
    std::uint64_t state = seed;
    for (std::size_t i = 0; i < N; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        out[i] = static_cast<std::uint8_t>(state >> 56);
    }
}

struct CookieSnapshot {
    std::uint64_t epoch = 0;
    std::uint8_t payload[24] = {};
    std::uint64_t cookie = 0;
};
static_assert(std::is_trivially_copyable_v<CookieSnapshot>);
static_assert(sizeof(CookieSnapshot) == 40);

[[nodiscard]] CookieSnapshot make_cookie_snapshot_(std::uint64_t epoch) noexcept {
    CookieSnapshot s;
    s.epoch = epoch;
    derive_payload_(epoch, s.payload);
    std::uint64_t buf[2] = {epoch, 0};
    std::memcpy(buf, &s.epoch, sizeof(s.epoch));
    s.cookie = fnv1a64_(&s.epoch, sizeof(s.epoch)) ^ fnv1a64_(s.payload, sizeof(s.payload));
    (void)buf;
    return s;
}

// A torn read mixes bytes from two epochs, so the payload no longer
// matches the derivation from the observed epoch.  The cookie is a
// second check for the case where a torn pair is self consistent by
// coincidence.
[[nodiscard]] bool verify_cookie_snapshot_(CookieSnapshot const& s) noexcept {
    std::uint8_t expected_payload[sizeof(s.payload)] = {};
    derive_payload_(s.epoch, expected_payload);
    if (std::memcmp(s.payload, expected_payload, sizeof(s.payload)) != 0) return false;
    const std::uint64_t expected_cookie = fnv1a64_(&s.epoch, sizeof(s.epoch)) ^ fnv1a64_(s.payload, sizeof(s.payload));
    return s.cookie == expected_cookie;
}

void test_atomic_snapshot_cookie_fuzzer() {
    constexpr int NUM_READERS = 8;
    constexpr int NUM_PUBLISHES = 50000;
    // Each reader does this many loads before it consults writer_done.
    // The snapshot protocol guarantees correctness but not reader
    // liveness, so without a floor an aggressive scheduler can run the
    // writer to completion before any reader gets CPU time and the
    // total_loads assertion below becomes a coin flip.
    constexpr std::uint64_t MIN_READER_LOADS = 64;

    AtomicSnapshot<CookieSnapshot> snap{make_cookie_snapshot_(0)};
    std::atomic<int> publishes_done{0};
    std::atomic<bool> writer_done{false};
    std::atomic<int> torn_reads_observed{0};
    std::atomic<std::uint64_t> total_loads{0};
    std::atomic<std::uint64_t> max_observed_epoch{0};

    std::latch start_latch{NUM_READERS + 1};

    std::jthread writer_t([&](std::stop_token) noexcept {
        start_latch.arrive_and_wait();
        for (int i = 1; i <= NUM_PUBLISHES; ++i) {
            snap.publish(make_cookie_snapshot_(static_cast<std::uint64_t>(i)));
            publishes_done.fetch_add(1, std::memory_order_acq_rel);
        }
        writer_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> readers;
    for (int r = 0; r < NUM_READERS; ++r) {
        readers.emplace_back([&](std::stop_token) noexcept {
            start_latch.arrive_and_wait();
            std::uint64_t local_max = 0;
            std::uint64_t local_loads = 0;

            auto do_one_load = [&]() noexcept {
                const auto observed = snap.load();
                if (!verify_cookie_snapshot_(observed)) {
                    torn_reads_observed.fetch_add(1, std::memory_order_acq_rel);
                }
                if (observed.epoch > local_max) local_max = observed.epoch;
                ++local_loads;
            };

            while (local_loads < MIN_READER_LOADS) {
                do_one_load();
            }

            while (!writer_done.load(std::memory_order_acquire)) {
                do_one_load();
            }

            const auto final_load = snap.load();
            if (!verify_cookie_snapshot_(final_load)) {
                torn_reads_observed.fetch_add(1, std::memory_order_acq_rel);
            }
            if (final_load.epoch > local_max) local_max = final_load.epoch;
            ++local_loads;

            total_loads.fetch_add(local_loads, std::memory_order_relaxed);
            std::uint64_t prev = max_observed_epoch.load(std::memory_order_acquire);
            while (local_max > prev
                   && !max_observed_epoch.compare_exchange_weak(prev, local_max, std::memory_order_acq_rel)) {}
        });
    }

    writer_t.join();
    for (auto& r : readers)
        r.join();

    CRUCIBLE_TEST_REQUIRE(publishes_done.load() == NUM_PUBLISHES);
    // Zero torn reads means the seqlock retry protocol holds.
    CRUCIBLE_TEST_REQUIRE(torn_reads_observed.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_loads.load() >= static_cast<std::uint64_t>(NUM_READERS) * (MIN_READER_LOADS + 1));
    // Every reader loads once more after observing writer_done, and the
    // last publish happens before that flag is set, so this equality is
    // not a race.
    CRUCIBLE_TEST_REQUIRE(max_observed_epoch.load() == static_cast<std::uint64_t>(NUM_PUBLISHES));
}

// Every worker stamps its own slice index over its whole slice.  The
// post-join scan then proves the slices covered the buffer with no gap
// and no overlap, which is what catches an off-by-one in the chunking
// or a mispacked slice index.

struct DisjointWhole {};

template <std::size_t N>
void test_owned_region_disjoint_split_impl_() {
    Arena arena{1ULL << 22};
    auto perm = mint_permission_root<DisjointWhole>();
    constexpr std::size_t TOTAL_BYTES = 1ULL << 18;
    auto region =
        OwnedRegion<std::uint8_t, DisjointWhole>::adopt(test_alloc_token_(), arena, TOTAL_BYTES, std::move(perm));

    // The sentinel is outside the range of slice indices, so a byte no
    // worker touched fails the scan below.
    constexpr std::uint8_t SENTINEL = 0xFF;
    for (auto& b : region.span())
        b = SENTINEL;

    auto recombined = parallel_for_views<N>(std::move(region), [](auto sub) noexcept {
        using SubType = std::remove_cvref_t<decltype(sub)>;
        constexpr std::size_t I = SubType::tag_type::index;
        const std::uint8_t marker = static_cast<std::uint8_t>(I);
        for (auto& b : sub.span())
            b = marker;
    });

    const std::size_t chunk = (TOTAL_BYTES + N - 1) / N;

    for (std::size_t off = 0; off < TOTAL_BYTES; ++off) {
        const std::size_t expected_slice = off / chunk;
        const std::uint8_t expected = static_cast<std::uint8_t>(expected_slice);
        const std::uint8_t observed = recombined.cspan()[off];
        if (observed != expected) {
            std::fprintf(stderr,
                         "  mismatch @ off=%zu: expected slice %zu (byte=0x%02x), "
                         "observed byte=0x%02x [TOTAL=%zu, N=%zu, chunk=%zu]\n",
                         off, expected_slice, expected, observed, TOTAL_BYTES, N, chunk);
            CRUCIBLE_TEST_REQUIRE(false);
        }
    }
}

void test_owned_region_disjoint_split_n8() { test_owned_region_disjoint_split_impl_<8>(); }

void test_owned_region_disjoint_split_n16() { test_owned_region_disjoint_split_impl_<16>(); }

// N does not divide the byte count, so the last slice is short.  That
// is the case an off-by-one in the chunk length hides in.
void test_owned_region_disjoint_split_uneven() {
    Arena arena{1ULL << 20};
    auto perm = mint_permission_root<DisjointWhole>();
    constexpr std::size_t TOTAL_BYTES = 100003;  // prime, not divisible by 7
    auto region =
        OwnedRegion<std::uint8_t, DisjointWhole>::adopt(test_alloc_token_(), arena, TOTAL_BYTES, std::move(perm));

    constexpr std::uint8_t SENTINEL = 0xAB;
    for (auto& b : region.span())
        b = SENTINEL;

    auto recombined = parallel_for_views<7>(std::move(region), [](auto sub) noexcept {
        using SubType = std::remove_cvref_t<decltype(sub)>;
        constexpr std::size_t I = SubType::tag_type::index;
        for (auto& b : sub.span()) {
            b = static_cast<std::uint8_t>(I);
        }
    });

    const std::size_t chunk = (TOTAL_BYTES + 7 - 1) / 7;
    for (std::size_t off = 0; off < TOTAL_BYTES; ++off) {
        const std::size_t expected_slice = off / chunk;
        const std::uint8_t expected = static_cast<std::uint8_t>(expected_slice);
        if (recombined.cspan()[off] != expected) {
            std::fprintf(stderr, "  uneven mismatch @ off=%zu: expected %zu, got 0x%02x\n", off, expected_slice,
                         recombined.cspan()[off]);
            CRUCIBLE_TEST_REQUIRE(false);
        }
    }
}

struct MpscMsg {
    std::uint32_t producer_id = 0;
    std::uint32_t seq = 0;
    std::uint64_t cookie = 0;
};
static_assert(std::is_trivially_copyable_v<MpscMsg>);

[[nodiscard]] MpscMsg make_mpsc_msg_(std::uint32_t producer_id, std::uint32_t seq) noexcept {
    MpscMsg m;
    m.producer_id = producer_id;
    m.seq = seq;
    m.cookie = fnv1a64_(&m.producer_id, sizeof(m.producer_id)) ^ fnv1a64_(&m.seq, sizeof(m.seq));
    return m;
}

[[nodiscard]] bool verify_mpsc_msg_(MpscMsg const& m) noexcept {
    const std::uint64_t expected = fnv1a64_(&m.producer_id, sizeof(m.producer_id)) ^ fnv1a64_(&m.seq, sizeof(m.seq));
    return m.cookie == expected;
}

void test_mpsc_ring_exactly_once() {
    constexpr std::uint32_t NUM_PRODUCERS = 8;
    constexpr std::uint32_t MSGS_PER_PRODUCER = 5000;
    constexpr std::size_t QUEUE_CAP = 1024;

    MpscRing<MpscMsg, QUEUE_CAP> q;
    std::atomic<bool> producers_done{false};
    std::atomic<int> producers_finished{0};

    std::vector<std::vector<std::uint32_t>> per_producer_seqs(NUM_PRODUCERS);

    std::jthread consumer([&](std::stop_token) noexcept {
        std::uint32_t total_received = 0;
        const std::uint32_t expected_total = NUM_PRODUCERS * MSGS_PER_PRODUCER;
        while (total_received < expected_total) {
            auto opt = q.try_pop();
            if (!opt) {
                if (producers_done.load(std::memory_order_acquire)) {
                    while ((opt = q.try_pop()).has_value()) {
                        const auto& m = *opt;
                        if (!verify_mpsc_msg_(m)) [[unlikely]] {
                            std::fprintf(stderr, "  MPSC torn msg: pid=%u seq=%u\n", m.producer_id, m.seq);
                            std::abort();
                        }
                        if (m.producer_id < NUM_PRODUCERS) {
                            per_producer_seqs[m.producer_id].push_back(m.seq);
                            ++total_received;
                        }
                    }
                    break;
                }
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            const auto& m = *opt;
            if (!verify_mpsc_msg_(m)) [[unlikely]] {
                std::fprintf(stderr, "  MPSC torn msg: pid=%u seq=%u\n", m.producer_id, m.seq);
                std::abort();
            }
            if (m.producer_id < NUM_PRODUCERS) {
                per_producer_seqs[m.producer_id].push_back(m.seq);
                ++total_received;
            }
        }
    });

    std::vector<std::jthread> producers;
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        producers.emplace_back([&, pid](std::stop_token) noexcept {
            for (std::uint32_t s = 1; s <= MSGS_PER_PRODUCER; ++s) {
                while (!q.try_push(make_mpsc_msg_(pid, s))) {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            producers_finished.fetch_add(1, std::memory_order_acq_rel);
            if (producers_finished.load() == static_cast<int>(NUM_PRODUCERS)) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers)
        p.join();
    producers_done.store(true, std::memory_order_release);
    consumer.join();

    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        const auto& seqs = per_producer_seqs[pid];
        CRUCIBLE_TEST_REQUIRE(seqs.size() == MSGS_PER_PRODUCER);
        for (std::uint32_t i = 0; i < MSGS_PER_PRODUCER; ++i) {
            CRUCIBLE_TEST_REQUIRE(seqs[i] == i + 1);
        }
    }
}

void test_chaselev_deque_no_duplicate_steal() {
    constexpr std::size_t CAPACITY = 4096;
    constexpr std::int32_t NUM_ITEMS = 3000;
    constexpr int NUM_THIEVES = 4;

    ChaseLevDeque<std::int32_t, CAPACITY> deq;

    for (std::int32_t i = 1; i <= NUM_ITEMS; ++i) {
        const bool ok = deq.push_bottom(i);
        CRUCIBLE_TEST_REQUIRE(ok);
    }

    std::atomic<bool> stop_thieves{false};
    std::vector<std::vector<std::int32_t>> stolen(NUM_THIEVES);
    std::vector<std::int32_t> owner_popped;

    // Thieves start before the owner so that the two ends of the deque
    // contend from the first pop.
    std::vector<std::jthread> thieves;
    for (std::size_t t = 0; t < NUM_THIEVES; ++t) {
        thieves.emplace_back([&, t](std::stop_token) noexcept {
            while (!stop_thieves.load(std::memory_order_acquire)) {
                auto v = deq.steal_top();
                if (v) {
                    stolen[t].push_back(*v);
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
        });
    }

    std::jthread owner([&](std::stop_token) noexcept {
        for (;;) {
            auto v = deq.pop_bottom();
            if (!v) break;
            owner_popped.push_back(*v);
        }
    });
    owner.join();

    // Give thieves a bounded spin window to drain without parking.
    for (uint32_t spin = 0; spin < 1000000; ++spin) {
        CRUCIBLE_SPIN_PAUSE;
    }
    stop_thieves.store(true, std::memory_order_release);
    for (auto& t : thieves)
        t.join();

    std::set<std::int32_t> all_items;
    std::size_t total = owner_popped.size();
    for (auto v : owner_popped) {
        CRUCIBLE_TEST_REQUIRE(all_items.insert(v).second);
    }
    for (auto const& thief_loot : stolen) {
        total += thief_loot.size();
        for (auto v : thief_loot) {
            CRUCIBLE_TEST_REQUIRE(all_items.insert(v).second);
        }
    }
    CRUCIBLE_TEST_REQUIRE(total == NUM_ITEMS);
    CRUCIBLE_TEST_REQUIRE(all_items.size() == NUM_ITEMS);
    for (std::int32_t i = 1; i <= NUM_ITEMS; ++i) {
        CRUCIBLE_TEST_REQUIRE(all_items.contains(i));
    }
}

struct SpscMsg {
    std::uint32_t seq;
    std::uint64_t cookie;
};
static_assert(std::is_trivially_copyable_v<SpscMsg>);

[[nodiscard]] SpscMsg make_spsc_msg_(std::uint32_t seq) noexcept {
    SpscMsg m{seq, 0};
    m.cookie = fnv1a64_(&m.seq, sizeof(m.seq));
    return m;
}

[[nodiscard]] bool verify_spsc_msg_(SpscMsg const& m) noexcept { return m.cookie == fnv1a64_(&m.seq, sizeof(m.seq)); }

void test_spsc_ring_exactly_once() {
    constexpr std::uint32_t NUM_MSGS = 100000;
    constexpr std::size_t CAPACITY = 256;
    SpscRing<SpscMsg, CAPACITY> q;

    std::atomic<std::uint32_t> received_count{0};
    std::vector<std::uint32_t> received_seqs;
    received_seqs.reserve(NUM_MSGS);

    std::jthread consumer([&](std::stop_token) noexcept {
        while (received_count.load(std::memory_order_acquire) < NUM_MSGS) {
            auto opt = q.try_pop();
            if (opt) {
                if (!verify_spsc_msg_(*opt)) [[unlikely]] {
                    std::fprintf(stderr, "  SPSC torn msg: seq=%u\n", opt->seq);
                    std::abort();
                }
                received_seqs.push_back(opt->seq);
                received_count.fetch_add(1, std::memory_order_acq_rel);
            } else {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    });

    std::jthread producer([&](std::stop_token) noexcept {
        for (std::uint32_t i = 1; i <= NUM_MSGS; ++i) {
            while (!q.try_push(make_spsc_msg_(i))) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    });

    producer.join();
    consumer.join();

    CRUCIBLE_TEST_REQUIRE(received_count.load() == NUM_MSGS);
    CRUCIBLE_TEST_REQUIRE(received_seqs.size() == NUM_MSGS);
    for (std::uint32_t i = 0; i < NUM_MSGS; ++i) {
        CRUCIBLE_TEST_REQUIRE(received_seqs[i] == i + 1);
    }
}

struct ModeTrans {};

void test_pool_mode_transition_torture() {
    PermissionedSnapshot<CookieSnapshot, ModeTrans> snap{make_cookie_snapshot_(0)};

    auto writer_perm = mint_permission_root<snapshot_tag::Writer<ModeTrans>>();

    constexpr int NUM_READERS = 4;
    constexpr int DURATION_MS = 200;
    // Each reader does this many attempts before it consults `stop`,
    // for the same liveness reason as the unpermissioned snapshot
    // fuzzer above.
    constexpr std::uint64_t MIN_READER_LOADS = 32;

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> torn_observed{0};
    std::atomic<std::uint64_t> read_during_exclusive{0};
    std::atomic<std::uint64_t> exclusive_iterations{0};
    std::atomic<std::uint64_t> exclusive_blocked{0};
    std::atomic<std::uint64_t> total_loads{0};

    std::latch start_latch{NUM_READERS + 1};

    // A reader cannot assert "reader() returns nullopt exactly while an
    // exclusive is active": between the nullopt and the reader's own
    // check of the flag, the exclusive holder can already have released
    // it.  The checks that mean something run inside the exclusive body
    // below, where the lock is held.
    std::vector<std::jthread> readers;
    for (int i = 0; i < NUM_READERS; ++i) {
        readers.emplace_back([&](std::stop_token) noexcept {
            start_latch.arrive_and_wait();
            std::uint64_t local_loads = 0;

            auto do_one_load = [&]() noexcept {
                ++local_loads;
                auto r = snap.reader();
                if (!r) return;  // an exclusive holds the slot
                const auto observed = r->load();
                if (!verify_cookie_snapshot_(observed)) {
                    torn_observed.fetch_add(1, std::memory_order_acq_rel);
                }
            };

            while (local_loads < MIN_READER_LOADS) {
                do_one_load();
            }

            while (!stop.load(std::memory_order_acquire)) {
                do_one_load();
            }

            total_loads.fetch_add(local_loads, std::memory_order_relaxed);
        });
    }

    std::jthread coordinator([&](std::stop_token) noexcept {
        start_latch.arrive_and_wait();
        auto handle = snap.writer(std::move(writer_perm));
        std::uint64_t epoch = 1;
        const auto t_start = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            if ((now - t_start) > std::chrono::milliseconds(DURATION_MS)) break;

            for (int i = 0; i < 50; ++i) {
                handle.publish(make_cookie_snapshot_(epoch++));
            }

            // Inside the exclusive body the reader count must be zero
            // and the flag must be set.  The publish here is safe
            // because reader() hands out nothing while that flag holds.
            const bool ran = snap.with_drained_access([&]() noexcept {
                if (snap.outstanding_readers() != 0) {
                    torn_observed.fetch_add(1, std::memory_order_acq_rel);
                }
                if (!snap.is_exclusive_active()) {
                    torn_observed.fetch_add(1, std::memory_order_acq_rel);
                }
                handle.publish(make_cookie_snapshot_(epoch++));
                read_during_exclusive.fetch_add(1, std::memory_order_acq_rel);
            });
            if (ran) {
                exclusive_iterations.fetch_add(1, std::memory_order_acq_rel);
            } else {
                exclusive_blocked.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    });

    coordinator.join();
    stop.store(true, std::memory_order_release);
    for (auto& r : readers)
        r.join();

    CRUCIBLE_TEST_REQUIRE(torn_observed.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_loads.load() >= static_cast<std::uint64_t>(NUM_READERS) * MIN_READER_LOADS);
    // One coordinator iteration is 50 publishes and one attempted
    // exclusive, which is far shorter than DURATION_MS, so at least one
    // attempt happens.  Whether it drains or the readers block it is a
    // scheduling accident, so only the sum is deterministic.
    CRUCIBLE_TEST_REQUIRE(exclusive_iterations.load() + exclusive_blocked.load() > 0);
    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 0);
    CRUCIBLE_TEST_REQUIRE(!snap.is_exclusive_active());
}

struct RefCountTest {};

void test_pool_refcount_conservation() {
    constexpr int NUM_THREADS = 8;
    constexpr int OPS_PER_THREAD = 10000;

    PermissionedSnapshot<std::uint64_t, RefCountTest> snap{0};

    std::vector<std::jthread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&](std::stop_token) noexcept {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                auto r = snap.reader();
                if (!r) {
                    // No thread here takes an exclusive, so a refusal
                    // means the refcount leaked.
                    std::abort();
                }
                (void)r->load();
            }
        });
    }
    for (auto& t : threads)
        t.join();

    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 0);
    CRUCIBLE_TEST_REQUIRE(!snap.is_exclusive_active());
}

// Five successive forks over the same region.  Each round reads what
// the previous round wrote, so a join that rebuilds the parent from
// stale or mismatched slices shows up as a wrong final value rather
// than as a lost write.

struct NestedTest {};

void test_parallel_for_nested_integrity() {
    Arena arena{1ULL << 22};
    auto perm = mint_permission_root<NestedTest>();
    constexpr std::size_t N = 16384;
    auto region = OwnedRegion<std::uint64_t, NestedTest>::adopt(test_alloc_token_(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 0;

    for (int round = 0; round < 5; ++round) {
        region = parallel_for_views<4>(std::move(region), [round](auto sub) noexcept {
            using SubType = std::remove_cvref_t<decltype(sub)>;
            constexpr std::size_t I = SubType::tag_type::index;
            for (auto& v : sub.span()) {
                v = v * 2 + I + static_cast<std::uint64_t>(round);
            }
        });
    }

    const std::size_t chunk = (N + 4 - 1) / 4;
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t slice = i / chunk;
        std::uint64_t expected = 0;
        for (int round = 0; round < 5; ++round) {
            expected = expected * 2 + slice + static_cast<std::uint64_t>(round);
        }
        CRUCIBLE_TEST_REQUIRE(region.cspan()[i] == expected);
    }
}

struct PriorityMsg {
    std::uint32_t producer_id = 0;
    std::uint32_t seq = 0;
    std::uint64_t key = 0;
    std::uint64_t cookie = 0;
};
static_assert(std::is_trivially_copyable_v<PriorityMsg>);
static_assert(std::is_trivially_destructible_v<PriorityMsg>);
static_assert(sizeof(PriorityMsg) == 24);

[[nodiscard]] PriorityMsg make_priority_msg_(std::uint32_t pid, std::uint32_t seq, std::uint64_t key) noexcept {
    PriorityMsg m;
    m.producer_id = pid;
    m.seq = seq;
    m.key = key;
    m.cookie = fnv1a64_(&m.producer_id, sizeof(m.producer_id)) ^ fnv1a64_(&m.seq, sizeof(m.seq))
             ^ fnv1a64_(&m.key, sizeof(m.key));
    return m;
}

[[nodiscard]] bool verify_priority_msg_(PriorityMsg const& m) noexcept {
    const std::uint64_t expected = fnv1a64_(&m.producer_id, sizeof(m.producer_id)) ^ fnv1a64_(&m.seq, sizeof(m.seq))
                                 ^ fnv1a64_(&m.key, sizeof(m.key));
    return m.cookie == expected;
}

struct PriorityKey {
    static std::uint64_t key(const PriorityMsg& m) noexcept { return m.key; }
};

// The deque needs a lock-free atomic value type, which on x86_64
// without -mcx16 caps the payload at 8 bytes.  So the sequence number
// takes the upper half and a truncated digest the lower half, instead
// of the separate cookie field the other payloads here use.
[[nodiscard]] std::uint64_t make_lifo_msg_(std::uint32_t seq) noexcept {
    const std::uint32_t cookie32 = static_cast<std::uint32_t>(fnv1a64_(&seq, sizeof(seq)) & 0xFFFFFFFFULL);
    return (static_cast<std::uint64_t>(seq) << 32) | static_cast<std::uint64_t>(cookie32);
}

[[nodiscard]] bool verify_lifo_msg_(std::uint64_t v) noexcept {
    const std::uint32_t seq = static_cast<std::uint32_t>(v >> 32);
    const std::uint32_t got = static_cast<std::uint32_t>(v & 0xFFFFFFFFULL);
    const std::uint32_t expected = static_cast<std::uint32_t>(fnv1a64_(&seq, sizeof(seq)) & 0xFFFFFFFFULL);
    return got == expected;
}

[[nodiscard]] std::uint32_t lifo_msg_seq_(std::uint64_t v) noexcept { return static_cast<std::uint32_t>(v >> 32); }

// The ring's threshold counter and per-cell cycle bits must never let
// two producers claim one cell, nor let a consumer read a cell whose
// payload belongs to an earlier lap.  Either fault shows up as a cookie
// mismatch or as a duplicate sequence number.

void test_raw_mpmc_ring_cookie_fuzzer() {
    constexpr std::uint32_t NUM_PRODUCERS = 4;
    constexpr std::uint32_t NUM_CONSUMERS = 4;
    constexpr std::uint32_t MSGS_PER_PRODUCER = 3000;
    constexpr std::size_t QUEUE_CAP = 256;

    MpmcRing<MpscMsg, QUEUE_CAP> q;
    std::atomic<bool> producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::atomic<std::uint64_t> total_received{0};
    std::atomic<std::uint64_t> total_torn{0};

    std::vector<std::vector<std::vector<std::uint32_t>>> per_consumer_seqs(
        NUM_CONSUMERS, std::vector<std::vector<std::uint32_t>>(NUM_PRODUCERS));

    constexpr std::uint64_t EXPECTED_TOTAL = static_cast<std::uint64_t>(NUM_PRODUCERS) * MSGS_PER_PRODUCER;

    std::vector<std::jthread> consumers;
    for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
        consumers.emplace_back([&, cid](std::stop_token) noexcept {
            while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
                auto opt = q.try_pop();
                if (!opt) {
                    if (producers_done.load(std::memory_order_acquire) && total_received.load() >= EXPECTED_TOTAL)
                        break;
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                const auto& m = *opt;
                if (!verify_mpsc_msg_(m)) [[unlikely]] {
                    total_torn.fetch_add(1, std::memory_order_acq_rel);
                    continue;
                }
                if (m.producer_id < NUM_PRODUCERS) {
                    per_consumer_seqs[cid][m.producer_id].push_back(m.seq);
                    total_received.fetch_add(1, std::memory_order_acq_rel);
                }
            }
        });
    }

    std::vector<std::jthread> producers;
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        producers.emplace_back([&, pid](std::stop_token) noexcept {
            for (std::uint32_t s = 1; s <= MSGS_PER_PRODUCER; ++s) {
                while (!q.try_push(make_mpsc_msg_(pid, s))) {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1 == NUM_PRODUCERS) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers)
        p.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& c : consumers)
        c.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);

    // The ring preserves per-producer order: if a producer pushed N
    // before N+1, then a consumer that sees both sees them in that
    // order.  Which consumer gets which message is arbitrary, so the
    // claim is only monotonicity within one consumer's own list.
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
            const auto& seqs = per_consumer_seqs[cid][pid];
            for (std::size_t i = 1; i < seqs.size(); ++i) {
                CRUCIBLE_TEST_REQUIRE(seqs[i] > seqs[i - 1]);
            }
        }
    }

    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        std::vector<std::uint32_t> all_seqs;
        for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
            for (auto s : per_consumer_seqs[cid][pid])
                all_seqs.push_back(s);
        }
        CRUCIBLE_TEST_REQUIRE(all_seqs.size() == MSGS_PER_PRODUCER);
        std::sort(all_seqs.begin(), all_seqs.end());
        for (std::uint32_t i = 0; i < MSGS_PER_PRODUCER; ++i) {
            CRUCIBLE_TEST_REQUIRE(all_seqs[i] == i + 1);
        }
    }
}

// Every (producer, consumer) pair is an independent single-producer
// ring, so a routing bug that lands a message in the wrong cell is
// invisible to a plain count.  Tagging each message with its producer
// and checking it against the cell it arrived in makes it visible.

void test_raw_sharded_grid_cookie_fuzzer() {
    constexpr std::size_t M = 4;  // producers
    constexpr std::size_t N = 4;  // consumers
    constexpr std::size_t PER_CELL_CAP = 128;
    constexpr std::uint32_t MSGS_PER_PROD = 2000;

    ShardedSpscGrid<MpscMsg, M, N, PER_CELL_CAP> grid;
    std::atomic<bool> producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::atomic<std::uint64_t> total_torn{0};

    std::vector<std::vector<std::vector<std::uint32_t>>> per_cons_per_prod(N,
                                                                           std::vector<std::vector<std::uint32_t>>(M));

    constexpr std::uint64_t EXPECTED_TOTAL = static_cast<std::uint64_t>(M) * MSGS_PER_PROD;
    std::atomic<std::uint64_t> total_received{0};

    std::vector<std::jthread> consumers;
    for (std::size_t cid = 0; cid < N; ++cid) {
        consumers.emplace_back([&, cid](std::stop_token) noexcept {
            while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
                auto opt = grid.try_pop(cid);
                if (!opt) {
                    if (producers_done.load(std::memory_order_acquire) && total_received.load() >= EXPECTED_TOTAL)
                        break;
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                if (!verify_mpsc_msg_(*opt)) [[unlikely]] {
                    total_torn.fetch_add(1, std::memory_order_acq_rel);
                    continue;
                }
                if (opt->producer_id < M) {
                    per_cons_per_prod[cid][opt->producer_id].push_back(opt->seq);
                    total_received.fetch_add(1, std::memory_order_acq_rel);
                }
            }
        });
    }

    // The grid spreads each producer's messages across its own row of N
    // cells, so one producer's stream arrives split over all consumers.
    std::vector<std::jthread> producers;
    for (std::size_t pid = 0; pid < M; ++pid) {
        producers.emplace_back([&, pid](std::stop_token) noexcept {
            for (std::uint32_t s = 1; s <= MSGS_PER_PROD; ++s) {
                while (!grid.try_push(pid, make_mpsc_msg_(static_cast<std::uint32_t>(pid), s))) {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1 == M) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers)
        p.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& c : consumers)
        c.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);

    // One cell is one single-producer ring, so within a cell the order
    // is strict, not merely monotone by accident.
    for (std::size_t cid = 0; cid < N; ++cid) {
        for (std::size_t pid = 0; pid < M; ++pid) {
            const auto& seqs = per_cons_per_prod[cid][pid];
            for (std::size_t i = 1; i < seqs.size(); ++i) {
                CRUCIBLE_TEST_REQUIRE(seqs[i] > seqs[i - 1]);
            }
        }
    }
    for (std::size_t pid = 0; pid < M; ++pid) {
        std::vector<std::uint32_t> all_seqs;
        for (std::size_t cid = 0; cid < N; ++cid)
            for (auto s : per_cons_per_prod[cid][pid])
                all_seqs.push_back(s);
        CRUCIBLE_TEST_REQUIRE(all_seqs.size() == MSGS_PER_PROD);
        std::sort(all_seqs.begin(), all_seqs.end());
        for (std::uint32_t i = 0; i < MSGS_PER_PROD; ++i) {
            CRUCIBLE_TEST_REQUIRE(all_seqs[i] == i + 1);
        }
    }
}

// Producers and consumers both lend a handle per iteration rather than
// holding one for the run.  Holding one would exercise the queue but
// leave the shared-permission refcount untouched, which is half of what
// the wrapper is for.

template <typename Channel>
void drive_pmpmc_cookie_(Channel& ch) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, MpscMsg>, "drive_pmpmc_cookie_ expects MpscMsg payload");

    constexpr std::uint32_t NUM_PRODUCERS = 4;
    constexpr std::uint32_t NUM_CONSUMERS = 4;
    constexpr std::uint32_t MSGS_PER_PRODUCER = 1500;
    constexpr std::uint64_t EXPECTED_TOTAL = static_cast<std::uint64_t>(NUM_PRODUCERS) * MSGS_PER_PRODUCER;

    std::atomic<bool> producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::atomic<std::uint64_t> total_received{0};
    std::atomic<std::uint64_t> total_torn{0};
    std::vector<std::vector<std::vector<std::uint32_t>>> per_cons_per_prod(
        NUM_CONSUMERS, std::vector<std::vector<std::uint32_t>>(NUM_PRODUCERS));

    std::vector<std::jthread> consumers;
    for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
        consumers.emplace_back([&, cid](std::stop_token) noexcept {
            while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
                auto h_opt = ch.consumer();
                if (!h_opt) {
                    if (producers_done.load() && total_received.load() >= EXPECTED_TOTAL) break;
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                auto opt = h_opt->try_pop();
                if (!opt) {
                    if (producers_done.load() && total_received.load() >= EXPECTED_TOTAL) break;
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                if (!verify_mpsc_msg_(*opt)) [[unlikely]] {
                    total_torn.fetch_add(1, std::memory_order_acq_rel);
                    continue;
                }
                if (opt->producer_id < NUM_PRODUCERS) {
                    per_cons_per_prod[cid][opt->producer_id].push_back(opt->seq);
                    total_received.fetch_add(1, std::memory_order_acq_rel);
                }
            }
        });
    }

    std::vector<std::jthread> producers;
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        producers.emplace_back([&, pid](std::stop_token) noexcept {
            for (std::uint32_t s = 1; s <= MSGS_PER_PRODUCER; ++s) {
                for (;;) {
                    auto h_opt = ch.producer();
                    if (!h_opt) {
                        CRUCIBLE_SPIN_PAUSE;
                        continue;
                    }
                    if (h_opt->try_push(make_mpsc_msg_(pid, s))) break;
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1 == NUM_PRODUCERS) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers)
        p.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& c : consumers)
        c.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);

    // Per-producer order survives the handle boundary: within one
    // consumer's own list a producer's sequence numbers stay ascending.
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
            const auto& seqs = per_cons_per_prod[cid][pid];
            for (std::size_t i = 1; i < seqs.size(); ++i) {
                CRUCIBLE_TEST_REQUIRE(seqs[i] > seqs[i - 1]);
            }
        }
    }
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        std::vector<std::uint32_t> all_seqs;
        for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid)
            for (auto s : per_cons_per_prod[cid][pid])
                all_seqs.push_back(s);
        CRUCIBLE_TEST_REQUIRE(all_seqs.size() == MSGS_PER_PRODUCER);
        std::sort(all_seqs.begin(), all_seqs.end());
        for (std::uint32_t i = 0; i < MSGS_PER_PRODUCER; ++i)
            CRUCIBLE_TEST_REQUIRE(all_seqs[i] == i + 1);
    }
}

// The two ends are deliberately asymmetric.  The single consumer holds
// one linear permission for the whole run, while the producers lend
// from a pool per iteration, so one driver covers both ownership modes.

template <typename Channel>
void drive_pmpsc_cookie_(Channel& ch) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, MpscMsg>);

    constexpr std::uint32_t NUM_PRODUCERS = 4;
    constexpr std::uint32_t MSGS_PER_PRODUCER = 1500;
    constexpr std::uint64_t EXPECTED_TOTAL = static_cast<std::uint64_t>(NUM_PRODUCERS) * MSGS_PER_PRODUCER;

    auto cons_perm = mint_permission_root<typename Channel::consumer_tag>();
    auto consumer = ch.consumer(std::move(cons_perm));

    std::atomic<bool> producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::vector<std::vector<std::uint32_t>> per_prod_seqs(NUM_PRODUCERS);
    std::atomic<std::uint64_t> total_torn{0};

    std::jthread cons_t([&](std::stop_token) noexcept {
        std::uint64_t received = 0;
        while (received < EXPECTED_TOTAL) {
            auto opt = consumer.try_pop();
            if (!opt) {
                if (producers_done.load() && received >= EXPECTED_TOTAL) break;
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            if (!verify_mpsc_msg_(*opt)) [[unlikely]] {
                total_torn.fetch_add(1, std::memory_order_acq_rel);
                continue;
            }
            if (opt->producer_id < NUM_PRODUCERS) {
                per_prod_seqs[opt->producer_id].push_back(opt->seq);
                ++received;
            }
        }
    });

    std::vector<std::jthread> producers;
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        producers.emplace_back([&, pid](std::stop_token) noexcept {
            for (std::uint32_t s = 1; s <= MSGS_PER_PRODUCER; ++s) {
                for (;;) {
                    auto h_opt = ch.producer();
                    if (!h_opt) {
                        CRUCIBLE_SPIN_PAUSE;
                        continue;
                    }
                    if (h_opt->try_push(make_mpsc_msg_(pid, s))) break;
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
            if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1 == NUM_PRODUCERS) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers)
        p.join();
    producers_done.store(true, std::memory_order_release);
    cons_t.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    // With one consumer, per-producer order is exact rather than merely
    // ascending: the whole run of a producer must arrive as 1..N.
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        const auto& seqs = per_prod_seqs[pid];
        CRUCIBLE_TEST_REQUIRE(seqs.size() == MSGS_PER_PRODUCER);
        for (std::uint32_t i = 0; i < MSGS_PER_PRODUCER; ++i)
            CRUCIBLE_TEST_REQUIRE(seqs[i] == i + 1);
    }
}

// One linear owner pops from the bottom while pool thieves steal from
// the top.  The claim is that every value appears exactly once across
// both paths, which is where a mishandled steal race would show.

template <typename Channel>
void drive_pchase_lev_cookie_(Channel& deq) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, std::uint64_t>);

    // The whole batch has to fit before any pop starts, and the capacity
    // differs between the instantiations that use this driver, so the
    // count is derived from the channel rather than fixed.  The headroom
    // keeps the push loop clear of the full-queue boundary.
    const std::uint32_t NUM_ITEMS = static_cast<std::uint32_t>(Channel::capacity() - 16);
    constexpr int NUM_THIEVES = 4;

    auto owner_perm = mint_permission_root<typename Channel::owner_tag>();
    auto owner = deq.owner(std::move(owner_perm));

    for (std::uint32_t i = 1; i <= NUM_ITEMS; ++i) {
        const bool ok = owner.try_push(make_lifo_msg_(i));
        CRUCIBLE_TEST_REQUIRE(ok);
    }

    std::atomic<bool> stop_thieves{false};
    std::atomic<std::uint64_t> total_torn{0};
    std::vector<std::vector<std::uint32_t>> stolen(NUM_THIEVES);
    std::vector<std::uint32_t> owner_popped;

    std::vector<std::jthread> thieves;
    for (std::size_t t = 0; t < static_cast<std::size_t>(NUM_THIEVES); ++t) {
        thieves.emplace_back([&, t](std::stop_token) noexcept {
            while (!stop_thieves.load(std::memory_order_acquire)) {
                auto h_opt = deq.thief();
                if (!h_opt) {
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                auto v = h_opt->try_steal();
                if (v) {
                    if (!verify_lifo_msg_(*v)) [[unlikely]] {
                        total_torn.fetch_add(1, std::memory_order_acq_rel);
                        continue;
                    }
                    stolen[t].push_back(lifo_msg_seq_(*v));
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                }
            }
        });
    }

    std::jthread owner_t([&](std::stop_token) noexcept {
        for (;;) {
            auto v = owner.try_pop();
            if (!v) break;
            if (!verify_lifo_msg_(*v)) [[unlikely]] {
                total_torn.fetch_add(1, std::memory_order_acq_rel);
                continue;
            }
            owner_popped.push_back(lifo_msg_seq_(*v));
        }
    });
    owner_t.join();

    for (uint32_t spin = 0; spin < 1000000; ++spin) {
        CRUCIBLE_SPIN_PAUSE;
    }
    stop_thieves.store(true, std::memory_order_release);
    for (auto& t : thieves)
        t.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    std::set<std::uint32_t> all_items;
    std::size_t total = owner_popped.size();
    for (auto v : owner_popped) {
        CRUCIBLE_TEST_REQUIRE(all_items.insert(v).second);
    }
    for (auto const& thief_loot : stolen) {
        total += thief_loot.size();
        for (auto v : thief_loot) {
            CRUCIBLE_TEST_REQUIRE(all_items.insert(v).second);
        }
    }
    CRUCIBLE_TEST_REQUIRE(total == NUM_ITEMS);
    CRUCIBLE_TEST_REQUIRE(all_items.size() == NUM_ITEMS);
    for (std::uint32_t i = 1; i <= NUM_ITEMS; ++i) {
        CRUCIBLE_TEST_REQUIRE(all_items.contains(i));
    }
}

// Handles here are statically indexed, so the driver has to name each
// one and cannot loop over them.  The 4 by 4 shape is fixed to match
// the locality-aware policy's default shard count.

template <typename Channel>
void drive_psharded_grid_cookie_(Channel& grid) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, MpscMsg>);
    constexpr std::size_t M = Channel::num_producers;
    constexpr std::size_t N = Channel::num_consumers;
    static_assert(M == 4 && N == 4, "drive_psharded_grid_cookie_ hardcodes M=N=4");

    constexpr std::uint32_t MSGS_PER_PROD = 1500;
    constexpr std::uint64_t EXPECTED_TOTAL = static_cast<std::uint64_t>(M) * MSGS_PER_PROD;

    using WT = typename Channel::whole_tag;
    auto whole = mint_permission_root<WT>();
    auto perms = mint_grid_permissions<WT, M, N>(std::move(whole));

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
    auto c3 = grid.template consumer<3>(std::move(std::get<3>(perms.consumers)));

    std::atomic<bool> producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::atomic<std::uint64_t> total_received{0};
    std::atomic<std::uint64_t> total_torn{0};
    auto seen_seq = std::make_unique<std::atomic<std::uint8_t>[]>(M * (MSGS_PER_PROD + 1));

    auto run_producer = [&](auto& handle, std::uint32_t pid) {
        for (std::uint32_t s = 1; s <= MSGS_PER_PROD; ++s) {
            while (!handle.try_push(make_mpsc_msg_(pid, s))) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1 == M) {
            producers_done.store(true, std::memory_order_release);
        }
    };

    auto run_consumer = [&](auto& handle) {
        while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
            auto opt = handle.try_pop();
            if (!opt) {
                if (producers_done.load() && total_received.load() >= EXPECTED_TOTAL) break;
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            if (!verify_mpsc_msg_(*opt)) [[unlikely]] {
                total_torn.fetch_add(1, std::memory_order_acq_rel);
                continue;
            }
            if (opt->producer_id < M && opt->seq >= 1 && opt->seq <= MSGS_PER_PROD) {
                const std::uint64_t idx = static_cast<std::uint64_t>(opt->producer_id) * (MSGS_PER_PROD + 1) + opt->seq;
                const auto prior = seen_seq[idx].fetch_add(1, std::memory_order_acq_rel);
                if (prior != 0) {
                    total_torn.fetch_add(1, std::memory_order_acq_rel);
                    continue;
                }
                total_received.fetch_add(1, std::memory_order_acq_rel);
            } else {
                total_torn.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    };

    std::jthread t_p0([&](std::stop_token) { run_producer(p0, 0); });
    std::jthread t_p1([&](std::stop_token) { run_producer(p1, 1); });
    std::jthread t_p2([&](std::stop_token) { run_producer(p2, 2); });
    std::jthread t_p3([&](std::stop_token) { run_producer(p3, 3); });
    std::jthread t_c0([&](std::stop_token) { run_consumer(c0); });
    std::jthread t_c1([&](std::stop_token) { run_consumer(c1); });
    std::jthread t_c2([&](std::stop_token) { run_consumer(c2); });
    std::jthread t_c3([&](std::stop_token) { run_consumer(c3); });

    t_p0.join();
    t_p1.join();
    t_p2.join();
    t_p3.join();
    producers_done.store(true, std::memory_order_release);
    t_c0.join();
    t_c1.join();
    t_c2.join();
    t_c3.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);
    for (std::uint32_t pid = 0; pid < M; ++pid) {
        for (std::uint32_t seq = 1; seq <= MSGS_PER_PROD; ++seq) {
            const std::uint64_t idx = static_cast<std::uint64_t>(pid) * (MSGS_PER_PROD + 1) + seq;
            CRUCIBLE_TEST_REQUIRE(seen_seq[idx].load(std::memory_order_relaxed) == 1);
        }
    }
}

// This driver checks cookie integrity and exactly-once delivery, and
// deliberately does not check per-producer order.  The bucket clamp
// forces a late message into the current bucket regardless of its
// nominal priority, so under a consumer that drains slower than the
// producers push, bucket order is preserved but per-producer order is
// not.  Asserting order here would be asserting a property the
// structure does not offer.

template <typename Channel>
void drive_pcalendar_cookie_(Channel& grid) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, PriorityMsg>);
    constexpr std::size_t M = Channel::num_producers;
    static_assert(M == 4, "drive_pcalendar_cookie_ hardcodes M=4");

    constexpr std::uint32_t MSGS_PER_PROD = 800;
    constexpr std::uint32_t EXPECTED_TOTAL = static_cast<std::uint32_t>(M) * MSGS_PER_PROD;

    using WT = typename Channel::whole_tag;
    auto whole = mint_permission_root<WT>();
    auto perms = mint_grid_permissions<WT, M, 1>(std::move(whole));

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));

    std::atomic<bool> producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::atomic<std::uint64_t> total_received{0};
    std::atomic<std::uint64_t> total_torn{0};
    // Each (producer, seq) pair maps to one index, so a second arrival
    // of the same message raises the count past one.  A plain total
    // would not distinguish a duplicate from a missing message.
    auto seen = std::make_unique<std::atomic<std::uint8_t>[]>(EXPECTED_TOTAL);

    auto run_producer = [&](auto& handle, std::uint32_t pid) {
        // The stride spreads keys over many buckets while staying inside
        // one calendar window.  Crossing the window wraps the bucket
        // index and collapses priority delivery into plain arrival
        // order, which would make the run test nothing.
        std::uint64_t key = pid * 100ULL;
        for (std::uint32_t s = 1; s <= MSGS_PER_PROD; ++s) {
            key += 1000;
            while (!handle.try_push(make_priority_msg_(pid, s, key))) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1 == M) {
            producers_done.store(true, std::memory_order_release);
        }
    };

    std::jthread t_p0([&](std::stop_token) { run_producer(p0, 0); });
    std::jthread t_p1([&](std::stop_token) { run_producer(p1, 1); });
    std::jthread t_p2([&](std::stop_token) { run_producer(p2, 2); });
    std::jthread t_p3([&](std::stop_token) { run_producer(p3, 3); });

    std::jthread cons_t([&](std::stop_token) noexcept {
        while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
            auto opt = cons.try_pop();
            if (!opt) {
                if (producers_done.load() && total_received.load() >= EXPECTED_TOTAL) break;
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            if (!verify_priority_msg_(*opt)) [[unlikely]] {
                total_torn.fetch_add(1, std::memory_order_acq_rel);
                continue;
            }
            if (opt->producer_id < M && opt->seq >= 1 && opt->seq <= MSGS_PER_PROD) {
                const std::uint32_t idx = opt->producer_id * MSGS_PER_PROD + (opt->seq - 1);
                const auto prior = seen[idx].fetch_add(1, std::memory_order_acq_rel);
                if (prior != 0) {
                    // Duplicate delivery.
                    total_torn.fetch_add(1, std::memory_order_acq_rel);
                    continue;
                }
                total_received.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    });

    t_p0.join();
    t_p1.join();
    t_p2.join();
    t_p3.join();
    producers_done.store(true, std::memory_order_release);
    cons_t.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);
    for (std::uint32_t i = 0; i < EXPECTED_TOTAL; ++i) {
        CRUCIBLE_TEST_REQUIRE(seen[i].load(std::memory_order_relaxed) == 1);
    }
}

template <typename Channel>
void drive_pspsc_cookie_(Channel& ch) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, MpscMsg>);

    constexpr std::uint32_t NUM_MSGS = 5000;

    auto whole = mint_permission_root<typename Channel::whole_tag>();
    auto [pp, cp] =
        mint_permission_split<typename Channel::producer_tag, typename Channel::consumer_tag>(std::move(whole));
    auto producer = ch.producer(std::move(pp));
    auto consumer = ch.consumer(std::move(cp));

    std::atomic<std::uint32_t> received{0};
    std::vector<std::uint32_t> received_seqs;
    received_seqs.reserve(NUM_MSGS);
    std::atomic<std::uint64_t> total_torn{0};

    std::jthread cons_t([&](std::stop_token) noexcept {
        while (received.load(std::memory_order_acquire) < NUM_MSGS) {
            auto opt = consumer.try_pop();
            if (!opt) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            if (!verify_mpsc_msg_(*opt)) [[unlikely]] {
                total_torn.fetch_add(1, std::memory_order_acq_rel);
                continue;
            }
            received_seqs.push_back(opt->seq);
            received.fetch_add(1, std::memory_order_acq_rel);
        }
    });

    std::jthread prod_t([&](std::stop_token) noexcept {
        for (std::uint32_t s = 1; s <= NUM_MSGS; ++s) {
            while (!producer.try_push(make_mpsc_msg_(/*pid=*/0, s))) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    });

    prod_t.join();
    cons_t.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(received_seqs.size() == NUM_MSGS);
    for (std::uint32_t i = 0; i < NUM_MSGS; ++i)
        CRUCIBLE_TEST_REQUIRE(received_seqs[i] == i + 1);
}

struct PSpscCookieTag {};
struct PMpscCookieTag {};
struct PMpmcCookieTag {};
struct PShardedCookieTag {};
struct PChaseLevCookieTag {};
struct PCalCookieTag {};

void test_permissioned_spsc_cookie_fuzzer() {
    PermissionedSpscChannel<MpscMsg, 256, PSpscCookieTag> ch;
    drive_pspsc_cookie_(ch);
}

void test_permissioned_mpsc_cookie_fuzzer() {
    PermissionedMpscChannel<MpscMsg, 1024, PMpscCookieTag> ch;
    drive_pmpsc_cookie_(ch);
}

void test_permissioned_mpmc_cookie_fuzzer() {
    PermissionedMpmcChannel<MpscMsg, 1024, PMpmcCookieTag> ch;
    drive_pmpmc_cookie_(ch);
}

void test_permissioned_chase_lev_cookie_fuzzer() {
    PermissionedChaseLevDeque<std::uint64_t, 4096, PChaseLevCookieTag> deq;
    drive_pchase_lev_cookie_(deq);
}

void test_permissioned_sharded_grid_cookie_fuzzer() {
    PermissionedShardedGrid<MpscMsg, 4, 4, 256, PShardedCookieTag> grid;
    drive_psharded_grid_cookie_(grid);
}

void test_permissioned_calendar_cookie_fuzzer() {
    // Bucket count times quantum gives a window of 1024000 key units,
    // and the driver strides 1000 per message, so a producer walks
    // successive buckets without wrapping.
    //
    // The grid is heap allocated because four rings of 1024 buckets by
    // 128 slots do not fit the default stack.
    using Grid = PermissionedCalendarGrid<PriorityMsg, 4, 1024, 128, PriorityKey, /*QuantumNs=*/1000ULL, PCalCookieTag>;
    auto grid_ptr = std::make_unique<Grid>();
    drive_pcalendar_cookie_(*grid_ptr);
}

namespace cs = crucible::concurrent::scheduler;

// These match the calendar grid configuration used above, so the policy
// runs get the same non-wrapping key window.
using SchedDeadline = cs::Deadline<PriorityKey, 4, 1024, 128, 1000ULL>;
using SchedCfs = cs::Cfs<PriorityKey, 4, 1024, 128, 1000ULL>;
using SchedEevdf = cs::Eevdf<PriorityKey, 4, 1024, 128, 1000ULL>;

void test_scheduler_fifo_cookie_fuzzer() {
    typename cs::Fifo::template queue_template<MpscMsg> ch;
    drive_pmpmc_cookie_(ch);
}

void test_scheduler_lifo_cookie_fuzzer() {
    typename cs::Lifo::template queue_template<std::uint64_t> deq;
    drive_pchase_lev_cookie_(deq);
}

void test_scheduler_round_robin_cookie_fuzzer() {
    typename cs::RoundRobin::template queue_template<MpscMsg> ch;
    drive_pmpsc_cookie_(ch);
}

void test_scheduler_locality_aware_cookie_fuzzer() {
    typename cs::LocalityAware::template queue_template<MpscMsg> grid;
    drive_psharded_grid_cookie_(grid);
}

void test_scheduler_deadline_cookie_fuzzer() {
    using QT = typename SchedDeadline::template queue_template<PriorityMsg>;
    auto grid = std::make_unique<QT>();
    drive_pcalendar_cookie_(*grid);
}

void test_scheduler_cfs_cookie_fuzzer() {
    using QT = typename SchedCfs::template queue_template<PriorityMsg>;
    auto grid = std::make_unique<QT>();
    drive_pcalendar_cookie_(*grid);
}

void test_scheduler_eevdf_cookie_fuzzer() {
    using QT = typename SchedEevdf::template queue_template<PriorityMsg>;
    auto grid = std::make_unique<QT>();
    drive_pcalendar_cookie_(*grid);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_concurrency_collision_fuzzer:\n");

    run_test("AtomicSnapshot cookie-payload SWMR fuzzer", test_atomic_snapshot_cookie_fuzzer);
    run_test("OwnedRegion split-disjointness N=8", test_owned_region_disjoint_split_n8);
    run_test("OwnedRegion split-disjointness N=16", test_owned_region_disjoint_split_n16);
    run_test("OwnedRegion split-disjointness uneven (N=7)", test_owned_region_disjoint_split_uneven);
    run_test("MpscRing exactly-once delivery (8 producers)", test_mpsc_ring_exactly_once);
    run_test("ChaseLevDeque no-duplicate steal (4 thieves)", test_chaselev_deque_no_duplicate_steal);
    run_test("SpscRing exactly-once delivery", test_spsc_ring_exactly_once);
    run_test("PermissionedSnapshot mode-transition torture", test_pool_mode_transition_torture);
    run_test("Pool refcount conservation (8 threads)", test_pool_refcount_conservation);
    run_test("parallel_for_views nested integrity (5 rounds)", test_parallel_for_nested_integrity);

    run_test("Raw MpmcRing (SCQ) cookie fuzzer (4P × 4C)", test_raw_mpmc_ring_cookie_fuzzer);
    run_test("Raw ShardedSpscGrid cookie fuzzer (4P × 4C)", test_raw_sharded_grid_cookie_fuzzer);

    run_test("PermissionedSpscChannel cookie fuzzer", test_permissioned_spsc_cookie_fuzzer);
    run_test("PermissionedMpscChannel cookie fuzzer (4P × 1C linear)", test_permissioned_mpsc_cookie_fuzzer);
    run_test("PermissionedMpmcChannel cookie fuzzer (4P × 4C pool churn)", test_permissioned_mpmc_cookie_fuzzer);
    run_test("PermissionedChaseLevDeque cookie fuzzer (1 owner + 4 thieves)",
             test_permissioned_chase_lev_cookie_fuzzer);
    run_test("PermissionedShardedGrid cookie fuzzer (4×4 grid)", test_permissioned_sharded_grid_cookie_fuzzer);
    run_test("PermissionedCalendarGrid cookie fuzzer (4P × 1C priority)", test_permissioned_calendar_cookie_fuzzer);

    run_test("scheduler::Fifo queue_template cookie fuzzer", test_scheduler_fifo_cookie_fuzzer);
    run_test("scheduler::Lifo queue_template cookie fuzzer", test_scheduler_lifo_cookie_fuzzer);
    run_test("scheduler::RoundRobin queue_template cookie fuzzer", test_scheduler_round_robin_cookie_fuzzer);
    run_test("scheduler::LocalityAware queue_template cookie fuzzer", test_scheduler_locality_aware_cookie_fuzzer);
    run_test("scheduler::Deadline<K> queue_template cookie fuzzer", test_scheduler_deadline_cookie_fuzzer);
    run_test("scheduler::Cfs<K> queue_template cookie fuzzer", test_scheduler_cfs_cookie_fuzzer);
    run_test("scheduler::Eevdf<K> queue_template cookie fuzzer", test_scheduler_eevdf_cookie_fuzzer);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
