// What fixy/concurrent/SpscRing.h and fixy/concurrent/MpscRing.h claim,
// checked.
//
// Both rings are lock-free and single-consumer, and both make their
// correctness argument out of memory ordering rather than exclusion, so
// a single-threaded walk can only check the index arithmetic: the gates,
// the wrap, the batch clamps.  The threaded half is what checks the
// ordering claim, and it is the half that means anything under TSan.

#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/SpscRing.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

namespace c = fixy::concurrent;

namespace {

// ── What a cell may hold ─────────────────────────────────────────────

struct Plain {
    int value = 0;
    std::uint32_t producer = 0;
};

struct OwnsSomething {
    OwnsSomething() = default;
    ~OwnsSomething() { delete owned; }
    OwnsSomething(const OwnsSomething&) = delete;
    OwnsSomething& operator=(const OwnsSomething&) = delete;
    int* owned = nullptr;
};

static_assert(c::RingValue<int>);
static_assert(c::RingValue<Plain>);
static_assert(!c::RingValue<OwnsSomething>);

// ── Shape ────────────────────────────────────────────────────────────

using Spsc = c::SpscRing<Plain, 8>;
using Mpsc = c::MpscRing<Plain, 8>;

// The address is the identity: both rings hold atomics other threads
// reach through a pointer taken once.
static_assert(!std::is_copy_constructible_v<Spsc>);
static_assert(!std::is_move_constructible_v<Spsc>);
static_assert(!std::is_copy_constructible_v<Mpsc>);
static_assert(!std::is_move_constructible_v<Mpsc>);

static_assert(Spsc::capacity() == 8);
static_assert(Spsc::channel_capacity == 8);
static_assert(Mpsc::capacity() == 8);
static_assert(std::is_same_v<typename Spsc::value_type, Plain>);

// The head and tail counters are on separate cache lines, so a push
// does not invalidate the consumer's line and a pop does not invalidate
// the producer's.  AtomicMonotonic is what carries that, and the ring
// no longer restates it, so this line is where the ring's dependence on
// it is recorded.
static_assert(alignof(Spsc) >= 64);
static_assert(alignof(Mpsc) >= 64);
static_assert(alignof(::fixy::AtomicMonotonic<std::uint64_t>) >= 64);
static_assert(sizeof(::fixy::AtomicMonotonic<std::uint64_t>) >= 64);

// A ring whose capacity is below one bitmap word still gets one word,
// and one that spans several gets several.  Both are exercised below.
using MpscSmall = c::MpscRing<int, 4>;
using MpscWide = c::MpscRing<int, 128>;

[[nodiscard]] int spsc_single_threaded() {
    Spsc ring{};
    if (!ring.empty_approx() || ring.size_approx() != 0) {
        std::fprintf(stderr, "a fresh SPSC ring did not read as empty\n");
        return 1;
    }
    if (ring.try_pop().has_value()) {
        std::fprintf(stderr, "an empty SPSC ring returned an item\n");
        return 1;
    }

    // Filling to capacity and one past it: the gate is h - t >= Capacity.
    for (int i = 0; i < 8; ++i) {
        if (!ring.try_push(Plain{i, 0})) {
            std::fprintf(stderr, "SPSC push %d was refused below capacity\n", i);
            return 1;
        }
    }
    if (ring.try_push(Plain{8, 0})) {
        std::fprintf(stderr, "SPSC accepted a push past capacity\n");
        return 1;
    }
    if (ring.size_approx() != 8) {
        std::fprintf(stderr, "SPSC size_approx disagreed with the pushes\n");
        return 1;
    }

    // FIFO, and the ring wraps: after draining and refilling, the
    // positions are past the buffer end and the mask brings them back.
    for (int i = 0; i < 8; ++i) {
        const auto item = ring.try_pop();
        if (!item || item->value != i) {
            std::fprintf(stderr, "SPSC pop %d came back out of order\n", i);
            return 1;
        }
    }

    // A batch that does not fit fills what it can, unlike the MPSC
    // batch, which is all or nothing.
    const std::array<Plain, 6> six{Plain{100, 0}, Plain{101, 0}, Plain{102, 0},
                                   Plain{103, 0}, Plain{104, 0}, Plain{105, 0}};
    if (ring.try_push_batch(std::span<const Plain>{six}) != 6) {
        std::fprintf(stderr, "SPSC batch push did not take all six\n");
        return 1;
    }
    if (ring.try_push_batch(std::span<const Plain>{six}) != 2) {
        std::fprintf(stderr, "SPSC batch push did not fill the remaining two\n");
        return 1;
    }

    std::array<Plain, 8> drained{};
    if (ring.try_pop_batch(std::span<Plain>{drained}) != 8) {
        std::fprintf(stderr, "SPSC batch pop did not drain the ring\n");
        return 1;
    }
    if (drained[0].value != 100 || drained[5].value != 105 || drained[6].value != 100 || drained[7].value != 101) {
        std::fprintf(stderr, "SPSC batch pop returned the wrong sequence across the wrap\n");
        return 1;
    }
    if (!ring.empty_approx()) {
        std::fprintf(stderr, "SPSC ring did not read as empty after the drain\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int mpsc_single_threaded() {
    Mpsc ring{};
    if (!ring.empty_approx() || ring.try_pop().has_value()) {
        std::fprintf(stderr, "a fresh MPSC ring did not read as empty\n");
        return 1;
    }

    for (int i = 0; i < 8; ++i) {
        if (!ring.try_push(Plain{i, 0})) {
            std::fprintf(stderr, "MPSC push %d was refused below capacity\n", i);
            return 1;
        }
    }
    if (ring.try_push(Plain{8, 0})) {
        std::fprintf(stderr, "MPSC accepted a push past capacity\n");
        return 1;
    }

    for (int i = 0; i < 8; ++i) {
        const auto item = ring.try_pop();
        if (!item || item->value != i) {
            std::fprintf(stderr, "MPSC pop %d came back out of order\n", i);
            return 1;
        }
    }

    // All or nothing: a batch larger than the free space pushes nothing.
    const std::array<Plain, 6> six{Plain{200, 0}, Plain{201, 0}, Plain{202, 0},
                                   Plain{203, 0}, Plain{204, 0}, Plain{205, 0}};
    if (ring.try_push_batch(std::span<const Plain>{six}) != 6) {
        std::fprintf(stderr, "MPSC batch push did not take all six\n");
        return 1;
    }
    if (ring.try_push_batch(std::span<const Plain>{six}) != 0) {
        std::fprintf(stderr, "MPSC batch push partially filled where it must refuse\n");
        return 1;
    }

    // try_pop_batch clamps the request to Capacity.  Unclamped, the
    // prefix scan wraps past the buffer end, counts cells it already
    // took, and leaves tail ahead of head — after which the producer's
    // unsigned gate underflows and the ring reads as full forever.
    std::array<Plain, 64> oversized{};
    const std::size_t taken = ring.try_pop_batch(std::span<Plain>{oversized});
    if (taken != 6) {
        std::fprintf(stderr, "MPSC oversized batch pop returned %zu, not the six available\n", taken);
        return 1;
    }
    if (oversized[0].value != 200 || oversized[5].value != 205) {
        std::fprintf(stderr, "MPSC batch pop returned the wrong sequence\n");
        return 1;
    }
    if (!ring.empty_approx() || ring.size_approx() != 0) {
        std::fprintf(stderr, "MPSC ring did not read as empty after the clamped drain\n");
        return 1;
    }

    // The ring still accepts pushes, which is what the clamp protects:
    // an underflowed gate would refuse every one of them.
    if (!ring.try_push(Plain{7, 0})) {
        std::fprintf(stderr, "MPSC ring refused a push after the oversized drain — the capacity gate underflowed\n");
        return 1;
    }
    (void)ring.try_pop();
    return 0;
}

// The bitmap is a word per 64 cells.  A capacity below one word leaves
// high bits permanently zero; a capacity spanning words makes the
// prefix scan cross a word boundary.  Both paths run here.
[[nodiscard]] int mpsc_bitmap_widths() {
    MpscSmall small{};
    for (int i = 0; i < 4; ++i) {
        if (!small.try_push(i)) return 1;
    }
    if (small.try_push(4)) return 1;
    std::array<int, 4> out{};
    if (small.try_pop_batch(std::span<int>{out}) != 4 || out[3] != 3) return 1;

    MpscWide wide{};
    std::vector<int> items(100);
    for (int i = 0; i < 100; ++i)
        items[static_cast<std::size_t>(i)] = i;
    if (wide.try_push_batch(std::span<const int>{items}) != 100) return 1;

    // The drain crosses the boundary between bitmap words 0 and 1.
    std::vector<int> drained(100);
    if (wide.try_pop_batch(std::span<int>{drained}) != 100) return 1;
    for (int i = 0; i < 100; ++i) {
        if (drained[static_cast<std::size_t>(i)] != i) {
            std::fprintf(stderr, "the wide MPSC drain broke at index %d\n", i);
            return 1;
        }
    }
    return 0;
}

// ── Threaded: the ordering claim ─────────────────────────────────────

[[nodiscard]] int spsc_two_threads() {
    constexpr int kItems = 20000;
    c::SpscRing<int, 1024> ring{};
    std::atomic<bool> producer_done{false};

    int next_expected = 0;
    int received = 0;

    {
        std::jthread producer([&ring, &producer_done] {
            for (int i = 0; i < kItems;) {
                if (ring.try_push(i)) ++i;
            }
            producer_done.store(true, std::memory_order_release);
        });

        while (received < kItems) {
            if (const auto item = ring.try_pop()) {
                // FIFO is a property of the ring, not of the schedule:
                // one producer means the order is the push order.
                if (*item != next_expected) {
                    std::fprintf(stderr, "SPSC delivered %d where %d was owed\n", *item, next_expected);
                    return 1;
                }
                ++next_expected;
                ++received;
            } else if (producer_done.load(std::memory_order_acquire) && ring.empty_approx()) {
                break;
            }
        }
    }

    if (received != kItems) {
        std::fprintf(stderr, "SPSC lost %d items\n", kItems - received);
        return 1;
    }
    return 0;
}

[[nodiscard]] int mpsc_many_producers() {
    constexpr std::uint32_t kProducers = 4;
    constexpr int kPerProducer = 4000;
    constexpr int kTotal = static_cast<int>(kProducers) * kPerProducer;

    c::MpscRing<Plain, 1024> ring{};
    std::atomic<std::uint32_t> finished{0};

    // Per producer, the ring must preserve that producer's own order;
    // across producers it says nothing, and this test claims nothing.
    std::array<int, kProducers> last_seen{};
    for (auto& slot : last_seen)
        slot = -1;
    int received = 0;

    {
        std::vector<std::jthread> producers;
        for (std::uint32_t p = 0; p < kProducers; ++p) {
            producers.emplace_back([&ring, &finished, p] {
                for (int i = 0; i < kPerProducer;) {
                    if (ring.try_push(Plain{i, p})) ++i;
                }
                finished.fetch_add(1, std::memory_order_release);
            });
        }

        while (received < kTotal) {
            if (const auto item = ring.try_pop()) {
                const std::size_t who = item->producer;
                if (who >= kProducers) {
                    std::fprintf(stderr, "MPSC delivered an item from producer %zu\n", who);
                    return 1;
                }
                if (item->value != last_seen[who] + 1) {
                    std::fprintf(stderr, "MPSC reordered producer %zu: saw %d after %d\n", who, item->value,
                                 last_seen[who]);
                    return 1;
                }
                last_seen[who] = item->value;
                ++received;
            } else if (finished.load(std::memory_order_acquire) == kProducers && ring.empty_approx()) {
                break;
            }
        }
    }

    if (received != kTotal) {
        std::fprintf(stderr, "MPSC lost %d items\n", kTotal - received);
        return 1;
    }
    for (std::uint32_t p = 0; p < kProducers; ++p) {
        if (last_seen[p] != kPerProducer - 1) {
            std::fprintf(stderr, "MPSC dropped the tail of producer %u\n", p);
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = spsc_single_threaded(); rc != 0) return rc;
    if (const int rc = mpsc_single_threaded(); rc != 0) return rc;
    if (const int rc = mpsc_bitmap_widths(); rc != 0) return rc;
    if (const int rc = spsc_two_threads(); rc != 0) return rc;
    if (const int rc = mpsc_many_producers(); rc != 0) return rc;
    return 0;
}
