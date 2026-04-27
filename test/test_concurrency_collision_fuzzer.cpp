// ═══════════════════════════════════════════════════════════════════
// test_concurrency_collision_fuzzer — cookie-fingerprint correctness
//
// A safety net stronger than TSan.  ThreadSanitizer detects RACES;
// this suite detects CORRUPTION via cryptographic-cookie fingerprints
// embedded in every concurrent payload.  If a primitive lets bytes
// from two epochs mix, the recomputed cookie will mismatch with
// overwhelming probability (FNV-1a sensitivity ≈ 1-2^-64 per byte
// change).  False positives are statistically impossible; every
// failure indicates a real bug.
//
// ─── Coverage map (three sections) ─────────────────────────────────
//
// SECTION I — Raw concurrent primitives (the lock-free substrate):
//
//   1. AtomicSnapshot      — cookie-payload SWMR fuzzer with
//                            seed-derived payload (every byte must
//                            round-trip the FNV-1a digest)
//   2. OwnedRegion         — split-disjointness via parallel_for_views
//                            (N=8 / 16 / 7 uneven; off-by-one,
//                             slice-index packing bugs)
//   3. MpscRing            — N producers exactly-once delivery,
//                            per-producer FIFO preserved
//   4. ChaseLevDeque       — owner pops + N thieves steal; every
//                            item appears exactly once
//   5. SpscRing            — single-producer FIFO + cookie verify
//   6. MpmcRing (SCQ)      — N producers × M consumers; per-producer
//                            FIFO + global exactly-once
//   7. ShardedSpscGrid     — M×N independent SpscRings, all routed
//                            via cookie-tagged messages
//   8. CalendarGrid        — priority-keyed M-producer × 1-consumer;
//                            cookie + bucket-clamp invariant verified
//
// SECTION II — Permissioned-wrapper handle-boundary stress (CSL):
//
//   9. PermissionedSnapshot mode-transition torture (legacy)
//  10. Pool refcount conservation (legacy)
//  11. PermissionedSpscChannel  — linear×linear endpoints
//  12. PermissionedMpscChannel  — N pool producers × 1 linear consumer
//  13. PermissionedMpmcChannel  — N pool prods × M pool cons (TWO pools)
//  14. PermissionedShardedGrid  — M×N statically-indexed handles
//  15. PermissionedChaseLevDeque — 1 linear owner + N pool thieves
//  16. PermissionedCalendarGrid  — M static prods × 1 lin cons priority
//
// SECTION III — Scheduler policy queue_template stress (the
// dispatcher boundary):
//
//  17. scheduler::Fifo          → PermissionedMpmcChannel<UserTag=Fifo>
//  18. scheduler::Lifo          → PermissionedChaseLevDeque
//  19. scheduler::RoundRobin    → PermissionedMpscChannel
//  20. scheduler::LocalityAware → PermissionedShardedGrid
//  21. scheduler::Deadline<K>   → PermissionedCalendarGrid (deadline)
//  22. scheduler::Cfs<K>        → PermissionedCalendarGrid (vruntime)
//  23. scheduler::Eevdf<K>      → PermissionedCalendarGrid (vdeadline)
//
// ─── Section-III rationale ─────────────────────────────────────────
//
// Each scheduler policy's queue_template<Job> resolves to one of the
// Section-II Permissioned wrappers — but with a DIFFERENT UserTag.
// Distinct UserTags ripple down through the wrapper's permission tree
// and produce DISTINCT TYPES even when the underlying topology is the
// same.  Section III drives the policy's queue_template at the SAME
// adversarial workload as Section II, exercising the policy → wrapper
// boundary end-to-end.  If the policy alias is wrong (e.g. wires Fifo
// to Lifo by mistake) the test catches it.
//
// ─── Discipline ────────────────────────────────────────────────────
//
// Each test prints PASSED/FAILED.  Runs under both default and tsan
// presets; the tsan preset uses test/tsan-suppressions.txt to silence
// documented UB-adjacency in primitives that intentionally read
// uncommitted bytes (the AtomicSnapshot seqlock retry pattern).
//
// References:
//   THREADING.md §17 (cookie-fingerprint methodology)
//   misc/27_04_2026.md §1.4 (CSL discipline at every channel boundary)
//   SEPLOG-H3 (#329) for the scheduler-policy slice
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/concurrent/ChaseLevDeque.h>
#include <crucible/concurrent/MpmcRing.h>
#include <crucible/concurrent/MpscRing.h>
#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/concurrent/SpscRing.h>
#include <crucible/concurrent/scheduler/Policies.h>
#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Workload.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace crucible;
using namespace crucible::concurrent;
using namespace crucible::safety;

// ── Test harness ─────────────────────────────────────────────────

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                          \
    do {                                                                    \
        if (!(__VA_ARGS__)) [[unlikely]] {                                  \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                      \
                         #__VA_ARGS__, __FILE__, __LINE__);                 \
            throw TestFailure{};                                            \
        }                                                                   \
    } while (0)

namespace {

// effects::Test exposes an Alloc capability member; copy it for use in
// arena allocations.  Same pattern as test_owned_region.cpp.
inline effects::Alloc test_alloc_token_() noexcept {
    return effects::Test{}.alloc;
}

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

// ═════════════════════════════════════════════════════════════════
// Cookie-fingerprint primitives
// ═════════════════════════════════════════════════════════════════

// FNV-1a 64-bit — fast, byte-sensitive, no collision concerns at the
// scales we test (millions of payloads).  Identical implementation to
// crucible::detail::fmix64 in Reflect.h but local here to avoid a
// transitive header dependency.
[[nodiscard]] constexpr std::uint64_t fnv1a64_(
    const void* data, std::size_t len) noexcept
{
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

// Derive a deterministic byte payload from a 64-bit seed.  Uses
// linear-congruential mixing (Knuth's MMIX constants); the output is
// uncorrelated enough that any bit-level corruption changes the hash
// with overwhelming probability.
template <std::size_t N>
constexpr void derive_payload_(std::uint64_t seed,
                                std::uint8_t (&out)[N]) noexcept {
    std::uint64_t state = seed;
    for (std::size_t i = 0; i < N; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        out[i] = static_cast<std::uint8_t>(state >> 56);
    }
}

// ═════════════════════════════════════════════════════════════════
// 1. AtomicSnapshot cookie-payload SWMR fuzzer
// ═════════════════════════════════════════════════════════════════

struct CookieSnapshot {
    std::uint64_t epoch        = 0;     // monotonic per writer
    std::uint8_t  payload[24]  = {};    // derived from epoch
    std::uint64_t cookie       = 0;     // FNV-1a(epoch, payload)
};
static_assert(std::is_trivially_copyable_v<CookieSnapshot>);
static_assert(sizeof(CookieSnapshot) == 40);

[[nodiscard]] CookieSnapshot make_cookie_snapshot_(std::uint64_t epoch) noexcept {
    CookieSnapshot s;
    s.epoch = epoch;
    derive_payload_(epoch, s.payload);
    // Cookie covers epoch + payload — any byte-mix detected.
    std::uint64_t buf[2] = {epoch, 0};
    std::memcpy(buf, &s.epoch, sizeof(s.epoch));
    s.cookie = fnv1a64_(&s.epoch, sizeof(s.epoch))
             ^ fnv1a64_(s.payload, sizeof(s.payload));
    (void)buf;
    return s;
}

[[nodiscard]] bool verify_cookie_snapshot_(CookieSnapshot const& s) noexcept {
    // Recompute payload from observed epoch — if torn, payload bytes
    // came from a different epoch and won't match this derivation.
    std::uint8_t expected_payload[sizeof(s.payload)] = {};
    derive_payload_(s.epoch, expected_payload);
    if (std::memcmp(s.payload, expected_payload, sizeof(s.payload)) != 0)
        return false;
    // Recompute cookie from observed (epoch, payload) — covers the
    // case where torn epoch+payload happened to be self-consistent
    // by coincidence (unlikely but the cookie kills that).
    const std::uint64_t expected_cookie =
        fnv1a64_(&s.epoch, sizeof(s.epoch))
      ^ fnv1a64_(s.payload, sizeof(s.payload));
    return s.cookie == expected_cookie;
}

void test_atomic_snapshot_cookie_fuzzer() {
    constexpr int NUM_READERS    = 8;
    constexpr int NUM_PUBLISHES  = 50'000;

    AtomicSnapshot<CookieSnapshot> snap{make_cookie_snapshot_(0)};
    std::atomic<int>          publishes_done{0};
    std::atomic<bool>         writer_done{false};
    std::atomic<int>          torn_reads_observed{0};
    std::atomic<std::uint64_t> total_loads{0};
    std::atomic<std::uint64_t> max_observed_epoch{0};

    std::jthread writer_t([&](std::stop_token) noexcept {
        for (int i = 1; i <= NUM_PUBLISHES; ++i) {
            snap.publish(make_cookie_snapshot_(static_cast<std::uint64_t>(i)));
            publishes_done.fetch_add(1, std::memory_order_acq_rel);
        }
        writer_done.store(true, std::memory_order_release);
    });

    std::vector<std::jthread> readers;
    for (int r = 0; r < NUM_READERS; ++r) {
        readers.emplace_back([&](std::stop_token) noexcept {
            std::uint64_t local_max = 0;
            while (!writer_done.load(std::memory_order_acquire)) {
                total_loads.fetch_add(1, std::memory_order_relaxed);
                const auto observed = snap.load();
                if (!verify_cookie_snapshot_(observed)) {
                    torn_reads_observed.fetch_add(1,
                        std::memory_order_acq_rel);
                }
                if (observed.epoch > local_max) local_max = observed.epoch;
            }
            // Final consistency: post-writer-done load must observe
            // the final epoch.
            const auto final_load = snap.load();
            if (!verify_cookie_snapshot_(final_load)) {
                torn_reads_observed.fetch_add(1, std::memory_order_acq_rel);
            }
            if (final_load.epoch > local_max) local_max = final_load.epoch;
            // Update global max via atomic max.
            std::uint64_t prev = max_observed_epoch.load(std::memory_order_acquire);
            while (local_max > prev &&
                   !max_observed_epoch.compare_exchange_weak(
                       prev, local_max, std::memory_order_acq_rel)) {}
        });
    }

    writer_t.join();
    for (auto& r : readers) r.join();

    CRUCIBLE_TEST_REQUIRE(publishes_done.load() == NUM_PUBLISHES);
    // The cookie-fingerprint catches ANY byte corruption from torn
    // reads.  Zero observed = seqlock retry protocol works.
    CRUCIBLE_TEST_REQUIRE(torn_reads_observed.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_loads.load() > 0);
    // At least one reader must have observed the final epoch.
    CRUCIBLE_TEST_REQUIRE(
        max_observed_epoch.load() == static_cast<std::uint64_t>(NUM_PUBLISHES));
}

// ═════════════════════════════════════════════════════════════════
// 2. OwnedRegion split-disjointness verifier
// ═════════════════════════════════════════════════════════════════
//
// Each parallel_for_views worker writes its slice ID throughout its
// slice's bytes.  Post-join byte-by-byte scan verifies the entire
// arena buffer matches the expected slice-ID pattern with NO gaps and
// NO overlaps.  Detects off-by-one bugs in chunk_range_ and slice
// index packing.

struct DisjointWhole {};

template <std::size_t N>
void test_owned_region_disjoint_split_impl_() {
    Arena arena{1ULL << 22};
    auto perm = permission_root_mint<DisjointWhole>();
    constexpr std::size_t TOTAL_BYTES = 1ULL << 18;  // 256 KB
    auto region = OwnedRegion<std::uint8_t, DisjointWhole>::adopt(
        test_alloc_token_(), arena, TOTAL_BYTES, std::move(perm));

    // Initialize to a sentinel so unwritten bytes show up as wrong.
    constexpr std::uint8_t SENTINEL = 0xFF;
    for (auto& b : region.span()) b = SENTINEL;

    // Each worker writes its slice's I as every byte.  After join,
    // we scan and verify every byte equals the slice_id of the chunk
    // it falls into.
    auto recombined = parallel_for_views<N>(
        std::move(region),
        [](auto sub) noexcept {
            // sub is OwnedRegion<uint8_t, Slice<DisjointWhole, I>> for
            // some compile-time I — extract via parent_type chain.
            using SubType = std::remove_cvref_t<decltype(sub)>;
            constexpr std::size_t I = SubType::tag_type::index;
            const std::uint8_t marker = static_cast<std::uint8_t>(I);
            for (auto& b : sub.span()) b = marker;
        });

    // Compute expected chunking: ceil(TOTAL_BYTES / N).
    const std::size_t chunk = (TOTAL_BYTES + N - 1) / N;

    // Verify every byte matches its expected slice index.
    for (std::size_t off = 0; off < TOTAL_BYTES; ++off) {
        const std::size_t expected_slice = off / chunk;
        const std::uint8_t expected = static_cast<std::uint8_t>(expected_slice);
        const std::uint8_t observed = recombined.cspan()[off];
        if (observed != expected) {
            std::fprintf(stderr,
                "  mismatch @ off=%zu: expected slice %zu (byte=0x%02x), "
                "observed byte=0x%02x [TOTAL=%zu, N=%zu, chunk=%zu]\n",
                off, expected_slice, expected, observed,
                TOTAL_BYTES, N, chunk);
            CRUCIBLE_TEST_REQUIRE(false);
        }
    }
}

void test_owned_region_disjoint_split_n8() {
    test_owned_region_disjoint_split_impl_<8>();
}

void test_owned_region_disjoint_split_n16() {
    test_owned_region_disjoint_split_impl_<16>();
}

// Edge case: N that doesn't evenly divide TOTAL_BYTES.  The last
// slice's chunk_range_ length is bound by total - (N-1)*chunk.
void test_owned_region_disjoint_split_uneven() {
    // Constructed so TOTAL_BYTES % 7 != 0.
    Arena arena{1ULL << 20};
    auto perm = permission_root_mint<DisjointWhole>();
    constexpr std::size_t TOTAL_BYTES = 100'003;  // prime, not divisible by 7
    auto region = OwnedRegion<std::uint8_t, DisjointWhole>::adopt(
        test_alloc_token_(), arena, TOTAL_BYTES, std::move(perm));

    constexpr std::uint8_t SENTINEL = 0xAB;
    for (auto& b : region.span()) b = SENTINEL;

    auto recombined = parallel_for_views<7>(
        std::move(region),
        [](auto sub) noexcept {
            using SubType = std::remove_cvref_t<decltype(sub)>;
            constexpr std::size_t I = SubType::tag_type::index;
            for (auto& b : sub.span()) {
                b = static_cast<std::uint8_t>(I);
            }
        });

    const std::size_t chunk = (TOTAL_BYTES + 7 - 1) / 7;
    for (std::size_t off = 0; off < TOTAL_BYTES; ++off) {
        const std::size_t expected_slice = off / chunk;
        const std::uint8_t expected =
            static_cast<std::uint8_t>(expected_slice);
        if (recombined.cspan()[off] != expected) {
            std::fprintf(stderr,
                "  uneven mismatch @ off=%zu: expected %zu, got 0x%02x\n",
                off, expected_slice, recombined.cspan()[off]);
            CRUCIBLE_TEST_REQUIRE(false);
        }
    }
}

// ═════════════════════════════════════════════════════════════════
// 3. MpscRing exactly-once delivery fuzzer
// ═════════════════════════════════════════════════════════════════

struct MpscMsg {
    std::uint32_t producer_id = 0;
    std::uint32_t seq         = 0;
    std::uint64_t cookie      = 0;
};
static_assert(std::is_trivially_copyable_v<MpscMsg>);

[[nodiscard]] MpscMsg make_mpsc_msg_(std::uint32_t producer_id,
                                       std::uint32_t seq) noexcept {
    MpscMsg m;
    m.producer_id = producer_id;
    m.seq         = seq;
    m.cookie = fnv1a64_(&m.producer_id, sizeof(m.producer_id))
             ^ fnv1a64_(&m.seq, sizeof(m.seq));
    return m;
}

[[nodiscard]] bool verify_mpsc_msg_(MpscMsg const& m) noexcept {
    const std::uint64_t expected = fnv1a64_(&m.producer_id, sizeof(m.producer_id))
                                  ^ fnv1a64_(&m.seq, sizeof(m.seq));
    return m.cookie == expected;
}

void test_mpsc_ring_exactly_once() {
    constexpr std::uint32_t NUM_PRODUCERS = 8;
    constexpr std::uint32_t MSGS_PER_PRODUCER = 5'000;
    constexpr std::size_t QUEUE_CAP = 1024;

    MpscRing<MpscMsg, QUEUE_CAP> q;
    std::atomic<bool> producers_done{false};
    std::atomic<int>  producers_finished{0};

    // Per-producer FIFO state observed by consumer.
    std::vector<std::vector<std::uint32_t>> per_producer_seqs(NUM_PRODUCERS);

    std::jthread consumer([&](std::stop_token) noexcept {
        std::uint32_t total_received = 0;
        const std::uint32_t expected_total = NUM_PRODUCERS * MSGS_PER_PRODUCER;
        // Drain until we've seen the expected total OR producers
        // signaled done with empty queue.
        while (total_received < expected_total) {
            auto opt = q.try_pop();
            if (!opt) {
                if (producers_done.load(std::memory_order_acquire)) {
                    // One last drain attempt to clean residue.
                    while ((opt = q.try_pop()).has_value()) {
                        const auto& m = *opt;
                        if (!verify_mpsc_msg_(m)) [[unlikely]] {
                            std::fprintf(stderr,
                                "  MPSC torn msg: pid=%u seq=%u\n",
                                m.producer_id, m.seq);
                            std::abort();
                        }
                        if (m.producer_id < NUM_PRODUCERS) {
                            per_producer_seqs[m.producer_id].push_back(m.seq);
                            ++total_received;
                        }
                    }
                    break;
                }
                std::this_thread::yield();
                continue;
            }
            const auto& m = *opt;
            if (!verify_mpsc_msg_(m)) [[unlikely]] {
                std::fprintf(stderr,
                    "  MPSC torn msg: pid=%u seq=%u\n", m.producer_id, m.seq);
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
                    std::this_thread::yield();
                }
            }
            producers_finished.fetch_add(1, std::memory_order_acq_rel);
            if (producers_finished.load() ==
                static_cast<int>(NUM_PRODUCERS)) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers) p.join();
    producers_done.store(true, std::memory_order_release);
    consumer.join();

    // Verify per-producer:
    //   - Exactly MSGS_PER_PRODUCER messages received
    //   - Sequence numbers are exactly 1..MSGS_PER_PRODUCER (FIFO)
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        const auto& seqs = per_producer_seqs[pid];
        CRUCIBLE_TEST_REQUIRE(seqs.size() == MSGS_PER_PRODUCER);
        for (std::uint32_t i = 0; i < MSGS_PER_PRODUCER; ++i) {
            CRUCIBLE_TEST_REQUIRE(seqs[i] == i + 1);
        }
    }
}

// ═════════════════════════════════════════════════════════════════
// 4. ChaseLevDeque steal correctness
// ═════════════════════════════════════════════════════════════════

void test_chaselev_deque_no_duplicate_steal() {
    constexpr std::size_t CAPACITY    = 4096;
    constexpr std::int32_t NUM_ITEMS  = 3000;
    constexpr int NUM_THIEVES         = 4;

    ChaseLevDeque<std::int32_t, CAPACITY> deq;

    // Owner pushes all items first.
    for (std::int32_t i = 1; i <= NUM_ITEMS; ++i) {
        const bool ok = deq.push_bottom(i);
        CRUCIBLE_TEST_REQUIRE(ok);
    }

    std::atomic<bool> stop_thieves{false};
    // Per-thief and owner accumulators.
    std::vector<std::vector<std::int32_t>> stolen(NUM_THIEVES);
    std::vector<std::int32_t> owner_popped;

    // Spawn thieves first so they race the owner.
    std::vector<std::jthread> thieves;
    for (std::size_t t = 0; t < NUM_THIEVES; ++t) {
        thieves.emplace_back([&, t](std::stop_token) noexcept {
            while (!stop_thieves.load(std::memory_order_acquire)) {
                auto v = deq.steal_top();
                if (v) {
                    stolen[t].push_back(*v);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Owner pops in parallel.
    std::jthread owner([&](std::stop_token) noexcept {
        for (;;) {
            auto v = deq.pop_bottom();
            if (!v) break;
            owner_popped.push_back(*v);
        }
    });
    owner.join();

    // Give thieves a moment to drain.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop_thieves.store(true, std::memory_order_release);
    for (auto& t : thieves) t.join();

    // Verify: total = NUM_ITEMS, and every item appears exactly once.
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
    // Every value 1..NUM_ITEMS must be present.
    for (std::int32_t i = 1; i <= NUM_ITEMS; ++i) {
        CRUCIBLE_TEST_REQUIRE(all_items.contains(i));
    }
}

// ═════════════════════════════════════════════════════════════════
// 5. SpscRing exactly-once delivery
// ═════════════════════════════════════════════════════════════════

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

[[nodiscard]] bool verify_spsc_msg_(SpscMsg const& m) noexcept {
    return m.cookie == fnv1a64_(&m.seq, sizeof(m.seq));
}

void test_spsc_ring_exactly_once() {
    constexpr std::uint32_t NUM_MSGS = 100'000;
    constexpr std::size_t   CAPACITY = 256;
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
                std::this_thread::yield();
            }
        }
    });

    std::jthread producer([&](std::stop_token) noexcept {
        for (std::uint32_t i = 1; i <= NUM_MSGS; ++i) {
            while (!q.try_push(make_spsc_msg_(i))) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    CRUCIBLE_TEST_REQUIRE(received_count.load() == NUM_MSGS);
    CRUCIBLE_TEST_REQUIRE(received_seqs.size() == NUM_MSGS);
    // SPSC = strict FIFO; sequence must be 1, 2, 3, ...
    for (std::uint32_t i = 0; i < NUM_MSGS; ++i) {
        CRUCIBLE_TEST_REQUIRE(received_seqs[i] == i + 1);
    }
}

// ═════════════════════════════════════════════════════════════════
// 6. PermissionedSnapshot mode-transition torture
// ═════════════════════════════════════════════════════════════════

struct ModeTrans {};

void test_pool_mode_transition_torture() {
    PermissionedSnapshot<CookieSnapshot, ModeTrans> snap{
        make_cookie_snapshot_(0)};

    auto writer_perm =
        permission_root_mint<snapshot_tag::Writer<ModeTrans>>();

    constexpr int NUM_READERS = 4;
    constexpr int DURATION_MS = 200;

    std::atomic<bool>          stop{false};
    std::atomic<std::uint64_t> torn_observed{0};
    std::atomic<std::uint64_t> read_during_exclusive{0};
    std::atomic<std::uint64_t> exclusive_iterations{0};
    std::atomic<std::uint64_t> exclusive_blocked{0};
    std::atomic<std::uint64_t> total_loads{0};

    // Continuous reader threads.  We can't reliably check
    // "nullopt iff exclusive_active" from the reader's perspective
    // without serialization — between snap.reader() returning nullopt
    // and reader's is_exclusive_active() check, the exclusive holder
    // could have called deposit_exclusive (clearing the bit).  The
    // load-bearing checks are inside the with_drained_access body
    // (where the lock is held): outstanding_readers == 0, etc.
    std::vector<std::jthread> readers;
    for (int i = 0; i < NUM_READERS; ++i) {
        readers.emplace_back([&](std::stop_token) noexcept {
            while (!stop.load(std::memory_order_acquire)) {
                total_loads.fetch_add(1, std::memory_order_relaxed);
                auto r = snap.reader();
                if (!r) {
                    // Exclusive mode was active at lend time.  Yield
                    // and retry; we cannot race-verify the contract
                    // from this side (see comment above).
                    std::this_thread::yield();
                    continue;
                }
                const auto observed = r->load();
                if (!verify_cookie_snapshot_(observed)) {
                    torn_observed.fetch_add(1,
                        std::memory_order_acq_rel);
                }
            }
        });
    }

    // Writer + exclusive scheduler.
    std::jthread coordinator([&](std::stop_token) noexcept {
        auto handle = snap.writer(std::move(writer_perm));
        std::uint64_t epoch = 1;
        const auto t_start = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            if ((now - t_start) >
                std::chrono::milliseconds(DURATION_MS)) break;

            // Publish a few cookie snapshots.
            for (int i = 0; i < 50; ++i) {
                handle.publish(make_cookie_snapshot_(epoch++));
            }

            // Try a mode transition.  Body verifies invariants:
            //   * outstanding_readers must be 0 inside exclusive
            //   * writer can publish during exclusive with no torn
            //     observable to readers (because lend returns nullopt)
            const bool ran = snap.with_drained_access([&]() noexcept {
                if (snap.outstanding_readers() != 0) {
                    torn_observed.fetch_add(1,
                        std::memory_order_acq_rel);
                }
                if (!snap.is_exclusive_active()) {
                    torn_observed.fetch_add(1,
                        std::memory_order_acq_rel);
                }
                handle.publish(make_cookie_snapshot_(epoch++));
                read_during_exclusive.fetch_add(1,
                    std::memory_order_acq_rel);
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
    for (auto& r : readers) r.join();

    CRUCIBLE_TEST_REQUIRE(torn_observed.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_loads.load() > 0);
    // We should observe at least a handful of successful exclusive
    // entries OR at least proof that the system was actively
    // attempting them (blocked counter > 0).
    CRUCIBLE_TEST_REQUIRE(
        exclusive_iterations.load() + exclusive_blocked.load() > 0);
    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 0);
    CRUCIBLE_TEST_REQUIRE(!snap.is_exclusive_active());
}

// ═════════════════════════════════════════════════════════════════
// 7. Pool refcount conservation
// ═════════════════════════════════════════════════════════════════

struct RefCountTest {};

void test_pool_refcount_conservation() {
    constexpr int NUM_THREADS         = 8;
    constexpr int OPS_PER_THREAD      = 10'000;

    PermissionedSnapshot<std::uint64_t, RefCountTest> snap{0};

    std::vector<std::jthread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&](std::stop_token) noexcept {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                auto r = snap.reader();
                if (!r) {
                    // Shouldn't happen — no exclusive in this test.
                    std::abort();
                }
                // Briefly hold then release.
                (void)r->load();
            }
        });
    }
    for (auto& t : threads) t.join();

    // After all threads release, refcount must be 0.
    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 0);
    CRUCIBLE_TEST_REQUIRE(!snap.is_exclusive_active());
}

// ═════════════════════════════════════════════════════════════════
// 8. parallel_for_views nested integrity
// ═════════════════════════════════════════════════════════════════
//
// Outer fork into 4 children; each child writes a unique pattern to
// its slice.  Tests that the structural-join invariant holds end-to-
// end (no permission smuggling, no torn parent rebuild).

struct NestedTest {};

void test_parallel_for_nested_integrity() {
    Arena arena{1ULL << 22};
    auto perm = permission_root_mint<NestedTest>();
    constexpr std::size_t N = 16'384;
    auto region = OwnedRegion<std::uint64_t, NestedTest>::adopt(
        test_alloc_token_(), arena, N, std::move(perm));

    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 0;

    // Run 5 rounds of parallel_for_views<4> on the same region —
    // each round multiplies by 2 then adds the slice index.  Final
    // values are deterministic per-element.
    for (int round = 0; round < 5; ++round) {
        region = parallel_for_views<4>(
            std::move(region),
            [round](auto sub) noexcept {
                using SubType = std::remove_cvref_t<decltype(sub)>;
                constexpr std::size_t I = SubType::tag_type::index;
                for (auto& v : sub.span()) {
                    v = v * 2 + I + static_cast<std::uint64_t>(round);
                }
            });
    }

    // Compute expected per-byte values.
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

// ═════════════════════════════════════════════════════════════════
// Shared helpers for the new sections (I.MpmcRing/Sharded + II + III)
// ═════════════════════════════════════════════════════════════════

// PriorityMsg — cookie-tagged payload with a priority key.  Used for
// CalendarGrid + Deadline/Cfs/Eevdf coverage.  Cookie covers all four
// non-cookie fields so any byte mix is caught.
struct PriorityMsg {
    std::uint32_t producer_id = 0;
    std::uint32_t seq         = 0;
    std::uint64_t key         = 0;
    std::uint64_t cookie      = 0;
};
static_assert(std::is_trivially_copyable_v<PriorityMsg>);
static_assert(std::is_trivially_destructible_v<PriorityMsg>);
static_assert(sizeof(PriorityMsg) == 24);

[[nodiscard]] PriorityMsg make_priority_msg_(std::uint32_t pid,
                                              std::uint32_t seq,
                                              std::uint64_t key) noexcept {
    PriorityMsg m;
    m.producer_id = pid;
    m.seq         = seq;
    m.key         = key;
    m.cookie = fnv1a64_(&m.producer_id, sizeof(m.producer_id))
             ^ fnv1a64_(&m.seq,         sizeof(m.seq))
             ^ fnv1a64_(&m.key,         sizeof(m.key));
    return m;
}

[[nodiscard]] bool verify_priority_msg_(PriorityMsg const& m) noexcept {
    const std::uint64_t expected =
        fnv1a64_(&m.producer_id, sizeof(m.producer_id))
      ^ fnv1a64_(&m.seq,         sizeof(m.seq))
      ^ fnv1a64_(&m.key,         sizeof(m.key));
    return m.cookie == expected;
}

// KeyExtractor for PriorityMsg — used by CalendarGrid and the three
// priority-keyed scheduler policies (Deadline / Cfs / Eevdf).
struct PriorityKey {
    static std::uint64_t key(const PriorityMsg& m) noexcept { return m.key; }
};

// Lifo cookie helper.  ChaseLevDeque requires lock-free atomic value
// type → max 8 bytes on x86_64 without -mcx16.  We pack a 32-bit seq
// into the upper half of a uint64 and put a 32-bit FNV-1a digest in
// the lower half.  Lifo / scheduler::Lifo coverage uses this.
[[nodiscard]] std::uint64_t make_lifo_msg_(std::uint32_t seq) noexcept {
    const std::uint32_t cookie32 =
        static_cast<std::uint32_t>(fnv1a64_(&seq, sizeof(seq)) & 0xFFFFFFFFULL);
    return (static_cast<std::uint64_t>(seq) << 32)
         | static_cast<std::uint64_t>(cookie32);
}

[[nodiscard]] bool verify_lifo_msg_(std::uint64_t v) noexcept {
    const std::uint32_t seq = static_cast<std::uint32_t>(v >> 32);
    const std::uint32_t got = static_cast<std::uint32_t>(v & 0xFFFFFFFFULL);
    const std::uint32_t expected =
        static_cast<std::uint32_t>(fnv1a64_(&seq, sizeof(seq)) & 0xFFFFFFFFULL);
    return got == expected;
}

[[nodiscard]] std::uint32_t lifo_msg_seq_(std::uint64_t v) noexcept {
    return static_cast<std::uint32_t>(v >> 32);
}

// ═════════════════════════════════════════════════════════════════
// SECTION I — Raw queues missing from the original fuzzer
// ═════════════════════════════════════════════════════════════════

// 8. Raw MpmcRing (Nikolaev SCQ) cookie-fingerprint stress
// ─────────────────────────────────────────────────────────
//
// N producers × M consumers; per-producer FIFO + global exactly-once.
// SCQ's threshold-counter livelock-prevention + per-cell cycle bits
// must not let two producers overwrite the same cell or let a
// consumer mis-attribute a payload — the cookie verifies neither
// happens.

void test_raw_mpmc_ring_cookie_fuzzer() {
    constexpr std::uint32_t NUM_PRODUCERS     = 4;
    constexpr std::uint32_t NUM_CONSUMERS     = 4;
    constexpr std::uint32_t MSGS_PER_PRODUCER = 3'000;
    constexpr std::size_t   QUEUE_CAP         = 256;

    MpmcRing<MpscMsg, QUEUE_CAP> q;
    std::atomic<bool>            producers_done{false};
    std::atomic<std::uint32_t>   producers_finished{0};
    std::atomic<std::uint64_t>   total_received{0};
    std::atomic<std::uint64_t>   total_torn{0};

    // Each consumer collects per-producer sequence lists — for FIFO check.
    std::vector<std::vector<std::vector<std::uint32_t>>> per_consumer_seqs(
        NUM_CONSUMERS,
        std::vector<std::vector<std::uint32_t>>(NUM_PRODUCERS));

    constexpr std::uint64_t EXPECTED_TOTAL =
        static_cast<std::uint64_t>(NUM_PRODUCERS) * MSGS_PER_PRODUCER;

    std::vector<std::jthread> consumers;
    for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
        consumers.emplace_back([&, cid](std::stop_token) noexcept {
            while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
                auto opt = q.try_pop();
                if (!opt) {
                    if (producers_done.load(std::memory_order_acquire) &&
                        total_received.load() >= EXPECTED_TOTAL) break;
                    std::this_thread::yield();
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
                    std::this_thread::yield();
                }
            }
            if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1
                == NUM_PRODUCERS) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers) p.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& c : consumers) c.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);

    // Per-producer FIFO check: when we collect each producer's seqs
    // (across all consumers, in arrival order per consumer), then
    // sort by consumer-arrival and verify the per-producer subsequence
    // is monotone.  SCQ guarantees per-producer FIFO (if producer P
    // pushed seq=N before seq=N+1, then ANY consumer that observes
    // both sees seq=N before seq=N+1).
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
            const auto& seqs = per_consumer_seqs[cid][pid];
            for (std::size_t i = 1; i < seqs.size(); ++i) {
                CRUCIBLE_TEST_REQUIRE(seqs[i] > seqs[i - 1]);
            }
        }
    }

    // Exactly-once: union of per-consumer lists across all consumers
    // for a given producer must be exactly {1..MSGS_PER_PRODUCER}.
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        std::vector<std::uint32_t> all_seqs;
        for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
            for (auto s : per_consumer_seqs[cid][pid]) all_seqs.push_back(s);
        }
        CRUCIBLE_TEST_REQUIRE(all_seqs.size() == MSGS_PER_PRODUCER);
        std::sort(all_seqs.begin(), all_seqs.end());
        for (std::uint32_t i = 0; i < MSGS_PER_PRODUCER; ++i) {
            CRUCIBLE_TEST_REQUIRE(all_seqs[i] == i + 1);
        }
    }
}

// 9. Raw ShardedSpscGrid cookie-fingerprint stress
// ────────────────────────────────────────────────
//
// M producers each push to its own (producer_id, consumer_id) cell —
// each cell is an independent SpscRing.  Test: N consumers drain
// across all M producers' columns; verify total + cookie + per-cell
// FIFO.  Catches any cross-cell smuggling in the routing logic.

void test_raw_sharded_grid_cookie_fuzzer() {
    constexpr std::size_t   M              = 4;  // producers
    constexpr std::size_t   N              = 4;  // consumers
    constexpr std::size_t   PER_CELL_CAP   = 128;
    constexpr std::uint32_t MSGS_PER_PROD  = 2'000;

    ShardedSpscGrid<MpscMsg, M, N, PER_CELL_CAP> grid;
    std::atomic<bool>          producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::atomic<std::uint64_t> total_torn{0};

    // Each consumer accumulates per-producer sequences for its column.
    std::vector<std::vector<std::vector<std::uint32_t>>> per_cons_per_prod(
        N, std::vector<std::vector<std::uint32_t>>(M));

    constexpr std::uint64_t EXPECTED_TOTAL =
        static_cast<std::uint64_t>(M) * MSGS_PER_PROD;
    std::atomic<std::uint64_t> total_received{0};

    std::vector<std::jthread> consumers;
    for (std::size_t cid = 0; cid < N; ++cid) {
        consumers.emplace_back([&, cid](std::stop_token) noexcept {
            while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
                auto opt = grid.try_pop(cid);
                if (!opt) {
                    if (producers_done.load(std::memory_order_acquire) &&
                        total_received.load() >= EXPECTED_TOTAL) break;
                    std::this_thread::yield();
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

    // Each producer round-robins its messages across its row of N cells.
    std::vector<std::jthread> producers;
    for (std::size_t pid = 0; pid < M; ++pid) {
        producers.emplace_back([&, pid](std::stop_token) noexcept {
            for (std::uint32_t s = 1; s <= MSGS_PER_PROD; ++s) {
                while (!grid.try_push(pid,
                       make_mpsc_msg_(static_cast<std::uint32_t>(pid), s))) {
                    std::this_thread::yield();
                }
            }
            if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1
                == M) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers) p.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& c : consumers) c.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);

    // Per-cell FIFO: within (cid, pid), seqs must be monotone (each
    // cell is an SpscRing, strict FIFO).
    for (std::size_t cid = 0; cid < N; ++cid) {
        for (std::size_t pid = 0; pid < M; ++pid) {
            const auto& seqs = per_cons_per_prod[cid][pid];
            for (std::size_t i = 1; i < seqs.size(); ++i) {
                CRUCIBLE_TEST_REQUIRE(seqs[i] > seqs[i - 1]);
            }
        }
    }
    // Exactly-once per producer (union across consumers is exactly
    // {1..MSGS_PER_PROD}).
    for (std::size_t pid = 0; pid < M; ++pid) {
        std::vector<std::uint32_t> all_seqs;
        for (std::size_t cid = 0; cid < N; ++cid)
            for (auto s : per_cons_per_prod[cid][pid]) all_seqs.push_back(s);
        CRUCIBLE_TEST_REQUIRE(all_seqs.size() == MSGS_PER_PROD);
        std::sort(all_seqs.begin(), all_seqs.end());
        for (std::uint32_t i = 0; i < MSGS_PER_PROD; ++i) {
            CRUCIBLE_TEST_REQUIRE(all_seqs[i] == i + 1);
        }
    }
}

// ═════════════════════════════════════════════════════════════════
// SECTION II — Permissioned-wrapper handle-boundary stress
// ═════════════════════════════════════════════════════════════════
//
// These are templated drivers — same body works for the Section II
// wrapper instantiation AND the Section III scheduler queue_template
// instantiation, because the policy's queue_template IS one of these
// wrapper types (with a different UserTag).  Catches policy → wrapper
// alias bugs end-to-end.

// ── Driver: Permissioned MPMC ─────────────────────────────────────
//
// Shape: N pool producers × M pool consumers, both lend handles per
// stress iteration to exercise the SharedPermissionPool refcount
// under continuous churn.  Every msg is FNV-1a cookied.

template <typename Channel>
void drive_pmpmc_cookie_(Channel& ch) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, MpscMsg>,
                  "drive_pmpmc_cookie_ expects MpscMsg payload");

    constexpr std::uint32_t NUM_PRODUCERS     = 4;
    constexpr std::uint32_t NUM_CONSUMERS     = 4;
    constexpr std::uint32_t MSGS_PER_PRODUCER = 1'500;
    constexpr std::uint64_t EXPECTED_TOTAL =
        static_cast<std::uint64_t>(NUM_PRODUCERS) * MSGS_PER_PRODUCER;

    std::atomic<bool>          producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::atomic<std::uint64_t> total_received{0};
    std::atomic<std::uint64_t> total_torn{0};
    std::vector<std::vector<std::vector<std::uint32_t>>> per_cons_per_prod(
        NUM_CONSUMERS,
        std::vector<std::vector<std::uint32_t>>(NUM_PRODUCERS));

    std::vector<std::jthread> consumers;
    for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
        consumers.emplace_back([&, cid](std::stop_token) noexcept {
            while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
                auto h_opt = ch.consumer();   // lend per-iter — pool churn
                if (!h_opt) {
                    if (producers_done.load() &&
                        total_received.load() >= EXPECTED_TOTAL) break;
                    std::this_thread::yield();
                    continue;
                }
                auto opt = h_opt->try_pop();
                if (!opt) {
                    if (producers_done.load() &&
                        total_received.load() >= EXPECTED_TOTAL) break;
                    std::this_thread::yield();
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
                    auto h_opt = ch.producer();   // lend per-iter
                    if (!h_opt) { std::this_thread::yield(); continue; }
                    if (h_opt->try_push(make_mpsc_msg_(pid, s))) break;
                    std::this_thread::yield();
                }
            }
            if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1
                == NUM_PRODUCERS) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers) p.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& c : consumers) c.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);

    // Per-producer FIFO across consumers (SCQ guarantees per-producer
    // ordering between any two consumers that both observe the messages).
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid) {
            const auto& seqs = per_cons_per_prod[cid][pid];
            for (std::size_t i = 1; i < seqs.size(); ++i) {
                CRUCIBLE_TEST_REQUIRE(seqs[i] > seqs[i - 1]);
            }
        }
    }
    // Exactly-once.
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        std::vector<std::uint32_t> all_seqs;
        for (std::uint32_t cid = 0; cid < NUM_CONSUMERS; ++cid)
            for (auto s : per_cons_per_prod[cid][pid]) all_seqs.push_back(s);
        CRUCIBLE_TEST_REQUIRE(all_seqs.size() == MSGS_PER_PRODUCER);
        std::sort(all_seqs.begin(), all_seqs.end());
        for (std::uint32_t i = 0; i < MSGS_PER_PRODUCER; ++i)
            CRUCIBLE_TEST_REQUIRE(all_seqs[i] == i + 1);
    }
}

// ── Driver: Permissioned MPSC ─────────────────────────────────────
//
// Shape: N pool producers × 1 linear consumer.  Consumer holds a
// linear Permission for the lifetime of the test; producers lend per-
// iter to exercise the producer pool.

template <typename Channel>
void drive_pmpsc_cookie_(Channel& ch) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, MpscMsg>);

    constexpr std::uint32_t NUM_PRODUCERS     = 4;
    constexpr std::uint32_t MSGS_PER_PRODUCER = 1'500;
    constexpr std::uint64_t EXPECTED_TOTAL =
        static_cast<std::uint64_t>(NUM_PRODUCERS) * MSGS_PER_PRODUCER;

    auto cons_perm = permission_root_mint<typename Channel::consumer_tag>();
    auto consumer  = ch.consumer(std::move(cons_perm));

    std::atomic<bool>          producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::vector<std::vector<std::uint32_t>> per_prod_seqs(NUM_PRODUCERS);
    std::atomic<std::uint64_t> total_torn{0};

    std::jthread cons_t([&](std::stop_token) noexcept {
        std::uint64_t received = 0;
        while (received < EXPECTED_TOTAL) {
            auto opt = consumer.try_pop();
            if (!opt) {
                if (producers_done.load() && received >= EXPECTED_TOTAL) break;
                std::this_thread::yield();
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
                    if (!h_opt) { std::this_thread::yield(); continue; }
                    if (h_opt->try_push(make_mpsc_msg_(pid, s))) break;
                    std::this_thread::yield();
                }
            }
            if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1
                == NUM_PRODUCERS) {
                producers_done.store(true, std::memory_order_release);
            }
        });
    }

    for (auto& p : producers) p.join();
    producers_done.store(true, std::memory_order_release);
    cons_t.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    // Per-producer FIFO (single consumer, MPSC strict per-producer ordering).
    for (std::uint32_t pid = 0; pid < NUM_PRODUCERS; ++pid) {
        const auto& seqs = per_prod_seqs[pid];
        CRUCIBLE_TEST_REQUIRE(seqs.size() == MSGS_PER_PRODUCER);
        for (std::uint32_t i = 0; i < MSGS_PER_PRODUCER; ++i)
            CRUCIBLE_TEST_REQUIRE(seqs[i] == i + 1);
    }
}

// ── Driver: Permissioned ChaseLevDeque (Lifo policy) ──────────────
//
// Shape: 1 linear owner pushing K items, then owner pops while N
// pool thieves steal.  Every value 1..K appears exactly once across
// owner pops + thief steals.  Cookie packed into the uint64 because
// ChaseLev requires lock-free atomic value type.

template <typename Channel>
void drive_pchase_lev_cookie_(Channel& deq) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, std::uint64_t>);

    // Owner pushes all items first then drains in parallel with thieves;
    // we must fit the entire batch in the deque's capacity.  Channel::
    // capacity() varies between Section II (4096) and the scheduler::Lifo
    // policy default (1024); take a safe headroom of 16.
    const std::uint32_t NUM_ITEMS =
        static_cast<std::uint32_t>(Channel::capacity() - 16);
    constexpr int       NUM_THIEVES = 4;

    auto owner_perm = permission_root_mint<typename Channel::owner_tag>();
    auto owner = deq.owner(std::move(owner_perm));

    // Owner pushes all items first.
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
                if (!h_opt) { std::this_thread::yield(); continue; }
                auto v = h_opt->try_steal();
                if (v) {
                    if (!verify_lifo_msg_(*v)) [[unlikely]] {
                        total_torn.fetch_add(1, std::memory_order_acq_rel);
                        continue;
                    }
                    stolen[t].push_back(lifo_msg_seq_(*v));
                } else {
                    std::this_thread::yield();
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

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop_thieves.store(true, std::memory_order_release);
    for (auto& t : thieves) t.join();

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

// ── Driver: Permissioned ShardedGrid (LocalityAware policy) ───────
//
// Shape: M static producers × N static consumers (M=N=4 fixed for
// this driver — matches scheduler::LocalityAware's default shards).
// Each producer round-robins its msgs across its row of N cells.

template <typename Channel>
void drive_psharded_grid_cookie_(Channel& grid) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, MpscMsg>);
    constexpr std::size_t M = Channel::num_producers;
    constexpr std::size_t N = Channel::num_consumers;
    static_assert(M == 4 && N == 4,
                  "drive_psharded_grid_cookie_ hardcodes M=N=4");

    constexpr std::uint32_t MSGS_PER_PROD = 1'500;
    constexpr std::uint64_t EXPECTED_TOTAL =
        static_cast<std::uint64_t>(M) * MSGS_PER_PROD;

    using WT = typename Channel::whole_tag;
    auto whole = permission_root_mint<WT>();
    auto perms = split_grid<WT, M, N>(std::move(whole));

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto c0 = grid.template consumer<0>(std::move(std::get<0>(perms.consumers)));
    auto c1 = grid.template consumer<1>(std::move(std::get<1>(perms.consumers)));
    auto c2 = grid.template consumer<2>(std::move(std::get<2>(perms.consumers)));
    auto c3 = grid.template consumer<3>(std::move(std::get<3>(perms.consumers)));

    std::atomic<bool>          producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::atomic<std::uint64_t> total_received{0};
    std::atomic<std::uint64_t> total_torn{0};
    std::vector<std::vector<std::uint32_t>> per_prod_collected(M);
    std::mutex collected_mu;

    auto run_producer = [&](auto& handle, std::uint32_t pid) {
        for (std::uint32_t s = 1; s <= MSGS_PER_PROD; ++s) {
            while (!handle.try_push(make_mpsc_msg_(pid, s))) {
                std::this_thread::yield();
            }
        }
        if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1
            == M) {
            producers_done.store(true, std::memory_order_release);
        }
    };

    auto run_consumer = [&](auto& handle) {
        while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
            auto opt = handle.try_pop();
            if (!opt) {
                if (producers_done.load() &&
                    total_received.load() >= EXPECTED_TOTAL) break;
                std::this_thread::yield();
                continue;
            }
            if (!verify_mpsc_msg_(*opt)) [[unlikely]] {
                total_torn.fetch_add(1, std::memory_order_acq_rel);
                continue;
            }
            if (opt->producer_id < M) {
                {
                    std::scoped_lock lk{collected_mu};
                    per_prod_collected[opt->producer_id].push_back(opt->seq);
                }
                total_received.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    };

    std::jthread t_p0([&](std::stop_token){ run_producer(p0, 0); });
    std::jthread t_p1([&](std::stop_token){ run_producer(p1, 1); });
    std::jthread t_p2([&](std::stop_token){ run_producer(p2, 2); });
    std::jthread t_p3([&](std::stop_token){ run_producer(p3, 3); });
    std::jthread t_c0([&](std::stop_token){ run_consumer(c0); });
    std::jthread t_c1([&](std::stop_token){ run_consumer(c1); });
    std::jthread t_c2([&](std::stop_token){ run_consumer(c2); });
    std::jthread t_c3([&](std::stop_token){ run_consumer(c3); });

    t_p0.join(); t_p1.join(); t_p2.join(); t_p3.join();
    producers_done.store(true, std::memory_order_release);
    t_c0.join(); t_c1.join(); t_c2.join(); t_c3.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);
    for (std::uint32_t pid = 0; pid < M; ++pid) {
        auto& seqs = per_prod_collected[pid];
        CRUCIBLE_TEST_REQUIRE(seqs.size() == MSGS_PER_PROD);
        std::sort(seqs.begin(), seqs.end());
        for (std::uint32_t i = 0; i < MSGS_PER_PROD; ++i)
            CRUCIBLE_TEST_REQUIRE(seqs[i] == i + 1);
    }
}

// ── Driver: Permissioned CalendarGrid (Deadline/Cfs/Eevdf) ────────
//
// Shape: M static producers × 1 linear consumer.  Each producer
// pushes priority-keyed PriorityMsg.
//
// What's verified:
//   * Cookie integrity per message (no torn writes — the load-bearing
//     CSL property at the handle boundary).
//   * Exactly-once delivery via a global seen[] vector: each unique
//     (producer_id, seq) appears exactly once at the consumer.
//
// What's NOT verified (by design):
//   * Per-producer FIFO at the consumer.  Calendar grid's bucket-
//     clamp invariant trades strict per-producer FIFO for monotone
//     priority delivery under bucket wraparound.  When a producer
//     pushes faster than the consumer drains, late messages can be
//     forced to the current bucket regardless of their notional
//     priority — bucket order is preserved, not per-producer order.
//     This matches test_permissioned_calendar_grid.cpp::Tier 6's
//     verification scope.

template <typename Channel>
void drive_pcalendar_cookie_(Channel& grid) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, PriorityMsg>);
    constexpr std::size_t M = Channel::num_producers;
    static_assert(M == 4, "drive_pcalendar_cookie_ hardcodes M=4");

    constexpr std::uint32_t MSGS_PER_PROD = 800;
    constexpr std::uint32_t EXPECTED_TOTAL =
        static_cast<std::uint32_t>(M) * MSGS_PER_PROD;

    using WT = typename Channel::whole_tag;
    auto whole = permission_root_mint<WT>();
    auto perms = split_grid<WT, M, 1>(std::move(whole));

    auto p0 = grid.template producer<0>(std::move(std::get<0>(perms.producers)));
    auto p1 = grid.template producer<1>(std::move(std::get<1>(perms.producers)));
    auto p2 = grid.template producer<2>(std::move(std::get<2>(perms.producers)));
    auto p3 = grid.template producer<3>(std::move(std::get<3>(perms.producers)));
    auto cons = grid.consumer(std::move(std::get<0>(perms.consumers)));

    std::atomic<bool>          producers_done{false};
    std::atomic<std::uint32_t> producers_finished{0};
    std::atomic<std::uint64_t> total_received{0};
    std::atomic<std::uint64_t> total_torn{0};
    // Each (producer_id, seq) pair encodes to a unique payload index;
    // seen[idx] flips at most once (catches duplicates across the
    // bucket-clamp + scan path).
    std::vector<bool> seen(EXPECTED_TOTAL, false);
    std::mutex seen_mu;

    auto run_producer = [&](auto& handle, std::uint32_t pid) {
        // Spread keys across many buckets but stay well under
        // NumBuckets to avoid wraparound (which collapses calendar
        // priority into FIFO behavior).
        std::uint64_t key = pid * 100ULL;
        for (std::uint32_t s = 1; s <= MSGS_PER_PROD; ++s) {
            key += 1000;
            while (!handle.try_push(make_priority_msg_(pid, s, key))) {
                std::this_thread::yield();
            }
        }
        if (producers_finished.fetch_add(1, std::memory_order_acq_rel) + 1
            == M) {
            producers_done.store(true, std::memory_order_release);
        }
    };

    std::jthread t_p0([&](std::stop_token){ run_producer(p0, 0); });
    std::jthread t_p1([&](std::stop_token){ run_producer(p1, 1); });
    std::jthread t_p2([&](std::stop_token){ run_producer(p2, 2); });
    std::jthread t_p3([&](std::stop_token){ run_producer(p3, 3); });

    std::jthread cons_t([&](std::stop_token) noexcept {
        while (total_received.load(std::memory_order_acquire) < EXPECTED_TOTAL) {
            auto opt = cons.try_pop();
            if (!opt) {
                if (producers_done.load() &&
                    total_received.load() >= EXPECTED_TOTAL) break;
                std::this_thread::yield();
                continue;
            }
            if (!verify_priority_msg_(*opt)) [[unlikely]] {
                total_torn.fetch_add(1, std::memory_order_acq_rel);
                continue;
            }
            if (opt->producer_id < M && opt->seq >= 1 &&
                opt->seq <= MSGS_PER_PROD) {
                const std::uint32_t idx =
                    opt->producer_id * MSGS_PER_PROD + (opt->seq - 1);
                std::scoped_lock lk{seen_mu};
                if (seen[idx]) {
                    // Duplicate delivery — protocol violation.
                    total_torn.fetch_add(1, std::memory_order_acq_rel);
                    continue;
                }
                seen[idx] = true;
                total_received.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    });

    t_p0.join(); t_p1.join(); t_p2.join(); t_p3.join();
    producers_done.store(true, std::memory_order_release);
    cons_t.join();

    CRUCIBLE_TEST_REQUIRE(total_torn.load() == 0);
    CRUCIBLE_TEST_REQUIRE(total_received.load() == EXPECTED_TOTAL);
    // Exactly-once: every (pid, seq) appeared.
    for (std::uint32_t i = 0; i < EXPECTED_TOTAL; ++i) {
        CRUCIBLE_TEST_REQUIRE(seen[i]);
    }
}

// ── Driver: Permissioned SPSC ─────────────────────────────────────

template <typename Channel>
void drive_pspsc_cookie_(Channel& ch) {
    using Msg = typename Channel::value_type;
    static_assert(std::is_same_v<Msg, MpscMsg>);

    constexpr std::uint32_t NUM_MSGS = 5'000;

    auto whole = permission_root_mint<typename Channel::whole_tag>();
    auto [pp, cp] = permission_split<typename Channel::producer_tag,
                                      typename Channel::consumer_tag>(
        std::move(whole));
    auto producer = ch.producer(std::move(pp));
    auto consumer = ch.consumer(std::move(cp));

    std::atomic<std::uint32_t> received{0};
    std::vector<std::uint32_t> received_seqs;
    received_seqs.reserve(NUM_MSGS);
    std::atomic<std::uint64_t> total_torn{0};

    std::jthread cons_t([&](std::stop_token) noexcept {
        while (received.load(std::memory_order_acquire) < NUM_MSGS) {
            auto opt = consumer.try_pop();
            if (!opt) { std::this_thread::yield(); continue; }
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
                std::this_thread::yield();
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

// ─── Section II named tests — direct wrapper instantiation ───────

struct PSpscCookieTag      {};
struct PMpscCookieTag      {};
struct PMpmcCookieTag      {};
struct PShardedCookieTag   {};
struct PChaseLevCookieTag  {};
struct PCalCookieTag       {};

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
    // M=4 producers, NumBuckets=1024, BucketCap=128, QuantumNs=1000.
    // Bucket window = 1024 × 1000 = 1.024M key-units; producers stride
    // 1000/key so each lands in successive buckets, well within window.
    // Heap-allocated — the calendar grid is ~13 MB (4 × 1024 × 128 ×
    // sizeof(SpscRing slot)), too large for the default 8 MB stack.
    using Grid = PermissionedCalendarGrid<PriorityMsg, 4, 1024, 128,
                                          PriorityKey, /*QuantumNs=*/1000ULL,
                                          PCalCookieTag>;
    auto grid_ptr = std::make_unique<Grid>();
    drive_pcalendar_cookie_(*grid_ptr);
}

// ═════════════════════════════════════════════════════════════════
// SECTION III — Scheduler policy queue_template stress
// ═════════════════════════════════════════════════════════════════
//
// Each scheduler policy's queue_template<Job> resolves to one of the
// Section-II Permissioned wrappers — but with a DIFFERENT UserTag
// (scheduler::tag::Fifo vs PMpmcCookieTag, etc.).  Because UserTag
// participates in the wrapper's Permission tree, the resulting types
// are genuinely distinct.  Re-running the same adversarial workload
// through the policy alias verifies the policy → wrapper mapping is
// end-to-end correct.

namespace cs = crucible::concurrent::scheduler;

// PriorityMsg-flavored Deadline / Cfs / Eevdf policy instantiations
// matching the Section-II calendar grid configuration (M=4, 1024
// buckets, BucketCap=128, Quantum=1000).
using SchedDeadline = cs::Deadline<PriorityKey, 4, 1024, 128, 1000ULL>;
using SchedCfs      = cs::Cfs<PriorityKey,      4, 1024, 128, 1000ULL>;
using SchedEevdf    = cs::Eevdf<PriorityKey,    4, 1024, 128, 1000ULL>;

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

    run_test("AtomicSnapshot cookie-payload SWMR fuzzer",
             test_atomic_snapshot_cookie_fuzzer);
    run_test("OwnedRegion split-disjointness N=8",
             test_owned_region_disjoint_split_n8);
    run_test("OwnedRegion split-disjointness N=16",
             test_owned_region_disjoint_split_n16);
    run_test("OwnedRegion split-disjointness uneven (N=7)",
             test_owned_region_disjoint_split_uneven);
    run_test("MpscRing exactly-once delivery (8 producers)",
             test_mpsc_ring_exactly_once);
    run_test("ChaseLevDeque no-duplicate steal (4 thieves)",
             test_chaselev_deque_no_duplicate_steal);
    run_test("SpscRing exactly-once delivery",
             test_spsc_ring_exactly_once);
    run_test("PermissionedSnapshot mode-transition torture",
             test_pool_mode_transition_torture);
    run_test("Pool refcount conservation (8 threads)",
             test_pool_refcount_conservation);
    run_test("parallel_for_views nested integrity (5 rounds)",
             test_parallel_for_nested_integrity);

    // ─── Section I additions ──────────────────────────────────────
    run_test("Raw MpmcRing (SCQ) cookie fuzzer (4P × 4C)",
             test_raw_mpmc_ring_cookie_fuzzer);
    run_test("Raw ShardedSpscGrid cookie fuzzer (4P × 4C)",
             test_raw_sharded_grid_cookie_fuzzer);

    // ─── Section II — Permissioned wrappers handle-boundary stress ──
    run_test("PermissionedSpscChannel cookie fuzzer",
             test_permissioned_spsc_cookie_fuzzer);
    run_test("PermissionedMpscChannel cookie fuzzer (4P × 1C linear)",
             test_permissioned_mpsc_cookie_fuzzer);
    run_test("PermissionedMpmcChannel cookie fuzzer (4P × 4C pool churn)",
             test_permissioned_mpmc_cookie_fuzzer);
    run_test("PermissionedChaseLevDeque cookie fuzzer (1 owner + 4 thieves)",
             test_permissioned_chase_lev_cookie_fuzzer);
    run_test("PermissionedShardedGrid cookie fuzzer (4×4 grid)",
             test_permissioned_sharded_grid_cookie_fuzzer);
    run_test("PermissionedCalendarGrid cookie fuzzer (4P × 1C priority)",
             test_permissioned_calendar_cookie_fuzzer);

    // ─── Section III — Scheduler policy queue_template stress ────
    run_test("scheduler::Fifo queue_template cookie fuzzer",
             test_scheduler_fifo_cookie_fuzzer);
    run_test("scheduler::Lifo queue_template cookie fuzzer",
             test_scheduler_lifo_cookie_fuzzer);
    run_test("scheduler::RoundRobin queue_template cookie fuzzer",
             test_scheduler_round_robin_cookie_fuzzer);
    run_test("scheduler::LocalityAware queue_template cookie fuzzer",
             test_scheduler_locality_aware_cookie_fuzzer);
    run_test("scheduler::Deadline<K> queue_template cookie fuzzer",
             test_scheduler_deadline_cookie_fuzzer);
    run_test("scheduler::Cfs<K> queue_template cookie fuzzer",
             test_scheduler_cfs_cookie_fuzzer);
    run_test("scheduler::Eevdf<K> queue_template cookie fuzzer",
             test_scheduler_eevdf_cookie_fuzzer);

    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
