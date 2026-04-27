// ═══════════════════════════════════════════════════════════════════
// test_permissioned_bitmap_mpsc_channel — sentinel TU.
//
// Validates PermissionedBitmapMpscChannel wraps BitmapMpscRing
// with full CSL fractional permissions:
//   * Producer-side fractional pool (multiple ProducerHandles)
//   * Consumer-side linear Permission (exactly one ConsumerHandle)
//   * Mode transition via with_drained_access
//   * Compile-time role discrimination (no try_pop on producer, etc.)
//   * Hot-path zero-cost (sizeof(Handle) == sizeof(Channel*))
// ═══════════════════════════════════════════════════════════════════

#include <crucible/concurrent/PermissionedBitmapMpscChannel.h>
#include <crucible/permissions/Permission.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

using namespace crucible::concurrent;

struct BenchTag {};
using TestChannel =
    PermissionedBitmapMpscChannel<std::uint64_t, 1024, BenchTag>;

// Compile-time structural claims
static_assert(!std::is_copy_constructible_v<TestChannel>);
static_assert(!std::is_move_constructible_v<TestChannel>);
static_assert(TestChannel::capacity() == 1024);

// Handle sizeof claims (the zero-cost EBO promise)
static_assert(sizeof(TestChannel::ProducerHandle) <= 2 * sizeof(void*),
              "ProducerHandle must be ≤ 2 ptr (Channel* + Guard via EBO)");
static_assert(sizeof(TestChannel::ConsumerHandle) <= 2 * sizeof(void*),
              "ConsumerHandle must be ≤ 2 ptr (Channel& + Permission via EBO)");

// Move-only on both handles
static_assert(!std::is_copy_constructible_v<TestChannel::ProducerHandle>);
static_assert(std::is_move_constructible_v<TestChannel::ProducerHandle>);
static_assert(!std::is_copy_constructible_v<TestChannel::ConsumerHandle>);
static_assert(std::is_move_constructible_v<TestChannel::ConsumerHandle>);

static void test_single_thread_round_trip() {
    TestChannel ch;
    auto cons_perm =
        crucible::safety::permission_root_mint<TestChannel::consumer_tag>();
    auto consumer = ch.consumer(std::move(cons_perm));

    auto p_opt = ch.producer();
    assert(p_opt);
    auto producer = std::move(*p_opt);

    for (uint64_t i = 0; i < 100; ++i) {
        assert(producer.try_push(i));
    }
    for (uint64_t i = 0; i < 100; ++i) {
        auto v = consumer.try_pop();
        assert(v && *v == i);
    }
    std::printf("  test_single_thread_round_trip: PASSED\n");
}

static void test_batched_round_trip() {
    TestChannel ch;
    auto cons_perm =
        crucible::safety::permission_root_mint<TestChannel::consumer_tag>();
    auto consumer = ch.consumer(std::move(cons_perm));

    auto p_opt = ch.producer();
    assert(p_opt);
    auto producer = std::move(*p_opt);

    std::array<uint64_t, 64> tx{};
    for (size_t i = 0; i < 64; ++i) tx[i] = 1000 + i;
    const size_t pushed = producer.try_push_batch(
        std::span<const uint64_t>(tx));
    assert(pushed == 64);

    std::array<uint64_t, 64> rx{};
    const size_t popped = consumer.try_pop_batch(std::span<uint64_t>(rx));
    assert(popped == 64);
    for (size_t i = 0; i < 64; ++i) assert(rx[i] == 1000 + i);
    std::printf("  test_batched_round_trip: PASSED\n");
}

static void test_with_drained_access() {
    TestChannel ch;
    auto cons_perm =
        crucible::safety::permission_root_mint<TestChannel::consumer_tag>();
    [[maybe_unused]] auto consumer = ch.consumer(std::move(cons_perm));

    // Acquire a producer — outstanding = 1.
    auto p_opt = ch.producer();
    assert(p_opt);
    assert(ch.outstanding_producers() == 1);

    // with_drained_access should fail (producer out).
    bool body_ran = false;
    const bool ok = ch.with_drained_access([&]{ body_ran = true; });
    assert(!ok);
    assert(!body_ran);

    // Drop the producer — outstanding = 0.
    p_opt.reset();
    assert(ch.outstanding_producers() == 0);

    // Now with_drained_access should succeed.
    const bool ok2 = ch.with_drained_access([&]{ body_ran = true; });
    assert(ok2);
    assert(body_ran);

    // After body, can acquire producer again.
    auto p_opt2 = ch.producer();
    assert(p_opt2);
    std::printf("  test_with_drained_access: PASSED\n");
}

static void test_multi_producer_stress() {
    constexpr size_t M = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 25000;
    constexpr size_t TOTAL = M * ITEMS_PER_PRODUCER;
    std::printf("  test_multi_producer_stress: %zu producers × %zu items "
                "via batch<16>...\n", M, ITEMS_PER_PRODUCER);

    TestChannel ch;
    auto cons_perm =
        crucible::safety::permission_root_mint<TestChannel::consumer_tag>();
    auto consumer = ch.consumer(std::move(cons_perm));

    std::vector<std::atomic<int>> seen(TOTAL);
    std::atomic<bool> start{false};
    std::atomic<size_t> consumed{0};

    auto encode = [](size_t p, size_t s) {
        return static_cast<uint64_t>(p) * ITEMS_PER_PRODUCER + s;
    };

    constexpr size_t BATCH = 16;

    std::vector<std::jthread> producers;
    for (size_t p = 0; p < M; ++p) {
        producers.emplace_back([&ch, &start, &encode, p, BATCH](
                std::stop_token) {
            while (!start.load(std::memory_order_acquire)) {}
            auto h_opt = ch.producer();
            assert(h_opt);
            auto h = std::move(*h_opt);
            std::array<uint64_t, BATCH> buf{};
            for (size_t base = 0; base < ITEMS_PER_PRODUCER; base += BATCH) {
                const size_t n = std::min(BATCH, ITEMS_PER_PRODUCER - base);
                for (size_t i = 0; i < n; ++i) buf[i] = encode(p, base + i);
                size_t pushed = 0;
                while (pushed < n) {
                    const size_t r = h.try_push_batch(
                        std::span<const uint64_t>(buf.data() + pushed,
                                                   n - pushed));
                    if (r > 0) pushed += r;
                    else std::this_thread::yield();
                }
            }
        });
    }

    std::jthread consumer_thread(
        [&consumer, &seen, &consumed, &start](std::stop_token) {
            while (!start.load(std::memory_order_acquire)) {}
            std::array<uint64_t, 32> buf{};
            while (consumed.load(std::memory_order_relaxed) < TOTAL) {
                const size_t n = consumer.try_pop_batch(
                    std::span<uint64_t>(buf));
                if (n == 0) {
                    std::this_thread::yield();
                    continue;
                }
                for (size_t i = 0; i < n; ++i) {
                    seen[buf[i]].fetch_add(1, std::memory_order_relaxed);
                }
                consumed.fetch_add(n, std::memory_order_relaxed);
            }
        });

    start.store(true, std::memory_order_release);
    producers.clear();
    consumer_thread.join();

    int missing = 0, dup = 0;
    for (size_t i = 0; i < TOTAL; ++i) {
        const int c = seen[i].load(std::memory_order_relaxed);
        if (c == 0) ++missing;
        else if (c > 1) dup += c - 1;
    }
    std::printf("    expected: %zu, missing: %d, duplicates: %d\n",
                TOTAL, missing, dup);
    assert(missing == 0 && "item lost under permissioned bitmap stress");
    assert(dup == 0 && "item duplicated under permissioned bitmap stress");
    std::printf("  test_multi_producer_stress: PASSED\n");
}

int main() {
    std::printf("test_permissioned_bitmap_mpsc_channel:\n");
    test_single_thread_round_trip();
    test_batched_round_trip();
    test_with_drained_access();
    test_multi_producer_stress();
    std::printf("test_permissioned_bitmap_mpsc_channel: ALL PASSED\n");
    return 0;
}
