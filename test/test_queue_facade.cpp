#include <crucible/concurrent/Queue.h>
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <thread>
#include <vector>

using namespace crucible::concurrent;
using crucible::safety::Permission;
using crucible::safety::mint_permission_fork;
using crucible::safety::mint_permission_root;
using crucible::safety::mint_permission_split;

struct TestFailure {};

// Variadic so the macro accepts expressions containing template-arg commas
// like `Queue<T, kind::sharded<2, 2, 16>>::capacity() == 2`.
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

// These forwarders take their handle by concept rather than by type.
// Instantiating one is the test: a handle the facade shapes wrongly
// will not satisfy the concept and the call will not compile.

template <QueueProducer P>
bool drive_producer(P producer, std::uint64_t value) {
    return producer.try_push(value);
}

template <QueueConsumer C>
std::optional<std::uint64_t> drive_consumer(C consumer) {
    return consumer.try_pop();
}

template <Stealable T>
std::optional<std::uint64_t> drive_thief(T thief) {
    return thief.try_steal();
}

void test_spsc_single_thread() {
    Queue<std::uint64_t, kind::spsc<16>> q;
    auto p = q.producer_handle();
    auto c = q.consumer_handle();

    CRUCIBLE_TEST_REQUIRE(q.empty_approx());
    CRUCIBLE_TEST_REQUIRE(q.size_approx() == 0);
    CRUCIBLE_TEST_REQUIRE(Queue<std::uint64_t, kind::spsc<16>>::capacity() == 16);

    CRUCIBLE_TEST_REQUIRE(drive_producer(p, 100u));
    CRUCIBLE_TEST_REQUIRE(drive_producer(p, 200u));
    CRUCIBLE_TEST_REQUIRE(drive_producer(p, 300u));

    CRUCIBLE_TEST_REQUIRE(q.size_approx() == 3);

    auto v0 = drive_consumer(c);
    CRUCIBLE_TEST_REQUIRE(v0 && *v0 == 100u);
    auto v1 = drive_consumer(c);
    CRUCIBLE_TEST_REQUIRE(v1 && *v1 == 200u);
    auto v2 = drive_consumer(c);
    CRUCIBLE_TEST_REQUIRE(v2 && *v2 == 300u);

    auto v3 = drive_consumer(c);
    CRUCIBLE_TEST_REQUIRE(!v3);
    CRUCIBLE_TEST_REQUIRE(q.empty_approx());

    // Fill the ring, drain it, then refill across the wrap point.
    for (std::uint64_t i = 0; i < 16; ++i) {
        CRUCIBLE_TEST_REQUIRE(drive_producer(p, i));
    }
    CRUCIBLE_TEST_REQUIRE(!drive_producer(p, 999u));

    for (std::uint64_t i = 0; i < 16; ++i) {
        auto v = drive_consumer(c);
        CRUCIBLE_TEST_REQUIRE(v && *v == i);
    }

    for (std::uint64_t i = 0; i < 5; ++i) {
        CRUCIBLE_TEST_REQUIRE(drive_producer(p, 100 + i));
    }
    for (std::uint64_t i = 0; i < 5; ++i) {
        auto v = drive_consumer(c);
        CRUCIBLE_TEST_REQUIRE(v && *v == 100 + i);
    }
}

void test_mpsc_single_thread() {
    Queue<std::uint64_t, kind::mpsc<16>> q;
    auto p = q.producer_handle();
    auto c = q.consumer_handle();

    CRUCIBLE_TEST_REQUIRE(q.empty_approx());
    CRUCIBLE_TEST_REQUIRE(Queue<std::uint64_t, kind::mpsc<16>>::capacity() == 16);

    for (std::uint64_t i = 0; i < 8; ++i) {
        CRUCIBLE_TEST_REQUIRE(drive_producer(p, i + 1));
    }
    for (std::uint64_t i = 0; i < 8; ++i) {
        auto v = drive_consumer(c);
        CRUCIBLE_TEST_REQUIRE(v && *v == i + 1);
    }
    auto empty = drive_consumer(c);
    CRUCIBLE_TEST_REQUIRE(!empty);
}

void test_sharded_single_thread() {
    Queue<std::uint64_t, kind::sharded<2, 2, 16>> q;

    auto p0 = q.producer_handle(0);
    auto p1 = q.producer_handle(1);
    auto c0 = q.consumer_handle(0);
    auto c1 = q.consumer_handle(1);

    CRUCIBLE_TEST_REQUIRE(p0.shard_id() == 0);
    CRUCIBLE_TEST_REQUIRE(p1.shard_id() == 1);
    CRUCIBLE_TEST_REQUIRE(c0.shard_id() == 0);
    CRUCIBLE_TEST_REQUIRE(c1.shard_id() == 1);

    CRUCIBLE_TEST_REQUIRE(Queue<std::uint64_t, kind::sharded<2, 2, 16>>::producer_count() == 2);
    CRUCIBLE_TEST_REQUIRE(Queue<std::uint64_t, kind::sharded<2, 2, 16>>::consumer_count() == 2);

    // Round-robin routing starts each producer's sequence at zero, so
    // a producer's even-numbered items go to consumer 0 and its
    // odd-numbered items to consumer 1.
    CRUCIBLE_TEST_REQUIRE(drive_producer(p0, 1000u));
    CRUCIBLE_TEST_REQUIRE(drive_producer(p0, 1001u));
    CRUCIBLE_TEST_REQUIRE(drive_producer(p1, 2000u));
    CRUCIBLE_TEST_REQUIRE(drive_producer(p1, 2001u));

    // A consumer's two items can arrive in either order, hence the
    // membership checks rather than positional ones.
    std::array<std::uint64_t, 2> from_c0{};
    std::size_t c0_count = 0;
    while (auto v = drive_consumer(c0)) {
        CRUCIBLE_TEST_REQUIRE(c0_count < 2);
        from_c0[c0_count++] = *v;
    }
    CRUCIBLE_TEST_REQUIRE(c0_count == 2);
    bool got_1000 = (from_c0[0] == 1000u || from_c0[1] == 1000u);
    bool got_2000 = (from_c0[0] == 2000u || from_c0[1] == 2000u);
    CRUCIBLE_TEST_REQUIRE(got_1000);
    CRUCIBLE_TEST_REQUIRE(got_2000);

    std::array<std::uint64_t, 2> from_c1{};
    std::size_t c1_count = 0;
    while (auto v = drive_consumer(c1)) {
        CRUCIBLE_TEST_REQUIRE(c1_count < 2);
        from_c1[c1_count++] = *v;
    }
    CRUCIBLE_TEST_REQUIRE(c1_count == 2);
    bool got_1001 = (from_c1[0] == 1001u || from_c1[1] == 1001u);
    bool got_2001 = (from_c1[0] == 2001u || from_c1[1] == 2001u);
    CRUCIBLE_TEST_REQUIRE(got_1001);
    CRUCIBLE_TEST_REQUIRE(got_2001);
}

void test_work_stealing_single_thread() {
    Queue<std::uint64_t, kind::work_stealing<16>> q;
    auto owner = q.owner_handle();
    auto thief = q.thief_handle();

    CRUCIBLE_TEST_REQUIRE(Queue<std::uint64_t, kind::work_stealing<16>>::capacity() == 16);

    // The owner pops from its own end, so it sees its pushes in
    // reverse.
    CRUCIBLE_TEST_REQUIRE(owner.try_push(1u));
    CRUCIBLE_TEST_REQUIRE(owner.try_push(2u));
    CRUCIBLE_TEST_REQUIRE(owner.try_push(3u));

    auto p3 = owner.try_pop();
    CRUCIBLE_TEST_REQUIRE(p3 && *p3 == 3u);
    auto p2 = owner.try_pop();
    CRUCIBLE_TEST_REQUIRE(p2 && *p2 == 2u);
    auto p1 = owner.try_pop();
    CRUCIBLE_TEST_REQUIRE(p1 && *p1 == 1u);
    auto empty = owner.try_pop();
    CRUCIBLE_TEST_REQUIRE(!empty);

    // A thief takes from the far end, so it sees the same pushes in
    // order.
    CRUCIBLE_TEST_REQUIRE(owner.try_push(10u));
    CRUCIBLE_TEST_REQUIRE(owner.try_push(20u));
    CRUCIBLE_TEST_REQUIRE(owner.try_push(30u));

    auto s10 = drive_thief(thief);
    CRUCIBLE_TEST_REQUIRE(s10 && *s10 == 10u);
    auto s20 = drive_thief(thief);
    CRUCIBLE_TEST_REQUIRE(s20 && *s20 == 20u);
    auto s30 = drive_thief(thief);
    CRUCIBLE_TEST_REQUIRE(s30 && *s30 == 30u);
    auto s_empty = drive_thief(thief);
    CRUCIBLE_TEST_REQUIRE(!s_empty);
}

void test_spsc_multi_thread() {
    Queue<std::uint64_t, kind::spsc<1024>> q;
    constexpr std::uint64_t N = 50000;

    std::atomic<bool> producer_done{false};
    std::atomic<std::uint64_t> received_sum{0};
    std::atomic<std::uint64_t> received_count{0};

    std::jthread producer_t([&q, &producer_done](std::stop_token) {
        auto p = q.producer_handle();
        for (std::uint64_t i = 1; i <= N; ++i) {
            while (!p.try_push(i)) {
                // The consumer drains, so a full queue is temporary.
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::jthread consumer_t([&q, &producer_done, &received_sum, &received_count](std::stop_token) {
        auto c = q.consumer_handle();
        std::uint64_t local_sum = 0;
        std::uint64_t local_count = 0;
        while (true) {
            auto v = c.try_pop();
            if (v) {
                local_sum += *v;
                local_count += 1;
                if (local_count == N) break;
            } else if (producer_done.load(std::memory_order_acquire) && q.empty_approx()) {
                break;
            }
        }
        received_sum.store(local_sum, std::memory_order_release);
        received_count.store(local_count, std::memory_order_release);
    });

    producer_t.join();
    consumer_t.join();

    // Sum of 1..N = N*(N+1)/2.
    const std::uint64_t expected_sum = N * (N + 1) / 2;
    CRUCIBLE_TEST_REQUIRE(received_count.load() == N);
    CRUCIBLE_TEST_REQUIRE(received_sum.load() == expected_sum);
}

void test_mpsc_multi_thread() {
    Queue<std::uint64_t, kind::mpsc<2048>> q;
    constexpr int NUM_PRODUCERS = 4;
    constexpr std::uint64_t PER_PRODUCER = 25000;
    constexpr std::uint64_t TOTAL = NUM_PRODUCERS * PER_PRODUCER;

    std::atomic<int> producers_done{0};
    std::atomic<std::uint64_t> received_sum{0};
    std::atomic<std::uint64_t> received_count{0};

    std::vector<std::jthread> producers;
    for (int t = 0; t < NUM_PRODUCERS; ++t) {
        producers.emplace_back([&q, &producers_done, t](std::stop_token) {
            auto p = q.producer_handle();
            const std::uint64_t base = static_cast<std::uint64_t>(t) * PER_PRODUCER;
            for (std::uint64_t i = 1; i <= PER_PRODUCER; ++i) {
                while (!p.try_push(base + i)) {
                    // The consumer drains, so a full queue is temporary.
                }
            }
            producers_done.fetch_add(1, std::memory_order_acq_rel);
        });
    }

    std::jthread consumer_t([&q, &producers_done, &received_sum, &received_count](std::stop_token) {
        auto c = q.consumer_handle();
        std::uint64_t local_sum = 0;
        std::uint64_t local_count = 0;
        while (true) {
            auto v = c.try_pop();
            if (v) {
                local_sum += *v;
                local_count += 1;
                if (local_count == TOTAL) break;
            } else if (producers_done.load(std::memory_order_acquire) == NUM_PRODUCERS && q.empty_approx()) {
                break;
            }
        }
        received_sum.store(local_sum, std::memory_order_release);
        received_count.store(local_count, std::memory_order_release);
    });

    for (auto& t : producers)
        t.join();
    consumer_t.join();

    // Each producer sends base+1..base+PER_PRODUCER, base = t*PER_PRODUCER.
    // Total sum = sum over t of (t*PER + 1 + t*PER + 2 + ... + t*PER + PER)
    //           = sum over t of (PER*t*PER + PER*(PER+1)/2)
    //           = NUM_PRODUCERS * PER*(PER+1)/2 + PER*PER*(0+1+...+(NUM-1))
    //           = TOTAL*(PER+1)/2 + PER*PER*NUM*(NUM-1)/2
    const std::uint64_t per_producer_sum = PER_PRODUCER * (PER_PRODUCER + 1) / 2;
    const std::uint64_t base_offsets_sum = PER_PRODUCER * PER_PRODUCER * (NUM_PRODUCERS * (NUM_PRODUCERS - 1) / 2);
    const std::uint64_t expected_sum = NUM_PRODUCERS * per_producer_sum + base_offsets_sum;

    CRUCIBLE_TEST_REQUIRE(received_count.load() == TOTAL);
    CRUCIBLE_TEST_REQUIRE(received_sum.load() == expected_sum);
}

void test_sharded_multi_thread() {
    constexpr std::size_t M = 4;
    constexpr std::size_t N = 4;
    Queue<std::uint64_t, kind::sharded<M, N, 256>> q;

    constexpr std::uint64_t PER_PRODUCER = 10000;
    constexpr std::uint64_t TOTAL = M * PER_PRODUCER;

    std::atomic<int> producers_done{0};
    std::atomic<std::uint64_t> received_count{0};
    std::atomic<std::uint64_t> received_sum{0};

    std::vector<std::jthread> producers;
    for (std::size_t shard = 0; shard < M; ++shard) {
        producers.emplace_back([&q, &producers_done, shard](std::stop_token) {
            auto p = q.producer_handle(shard);
            const std::uint64_t base = static_cast<std::uint64_t>(shard) * PER_PRODUCER;
            for (std::uint64_t i = 1; i <= PER_PRODUCER; ++i) {
                while (!p.try_push(base + i)) {
                    // This shard's consumer drains, so a full shard is
                    // temporary.
                }
            }
            producers_done.fetch_add(1, std::memory_order_acq_rel);
        });
    }

    std::vector<std::jthread> consumers;
    for (std::size_t shard = 0; shard < N; ++shard) {
        consumers.emplace_back([&q, &producers_done, &received_count, &received_sum, shard](std::stop_token) {
            auto c = q.consumer_handle(shard);
            std::uint64_t local_sum = 0;
            std::uint64_t local_count = 0;
            // Exit once every producer has finished and one further
            // try_pop comes back empty.  The second pop collects an
            // item that landed between the first pop and the read of
            // the producer count.  Testing the shared received_count
            // instead would deadlock: peers publish their counts only
            // after their loops end, so a consumer whose own share is
            // below the total would wait forever.
            while (true) {
                auto v = c.try_pop();
                if (v) {
                    local_sum += *v;
                    local_count += 1;
                } else if (producers_done.load(std::memory_order_acquire) == static_cast<int>(M)) {
                    auto last = c.try_pop();
                    if (!last) break;
                    local_sum += *last;
                    local_count += 1;
                }
            }
            received_count.fetch_add(local_count, std::memory_order_acq_rel);
            received_sum.fetch_add(local_sum, std::memory_order_acq_rel);
        });
    }

    for (auto& t : producers)
        t.join();
    for (auto& t : consumers)
        t.join();

    // The same derivation as the multi-producer test above, with M
    // producers each sending 1..PER_PRODUCER offset by their shard.
    const std::uint64_t per_producer_sum = PER_PRODUCER * (PER_PRODUCER + 1) / 2;
    const std::uint64_t base_offsets_sum = PER_PRODUCER * PER_PRODUCER * (M * (M - 1) / 2);
    const std::uint64_t expected_sum = M * per_producer_sum + base_offsets_sum;

    CRUCIBLE_TEST_REQUIRE(received_count.load() == TOTAL);
    CRUCIBLE_TEST_REQUIRE(received_sum.load() == expected_sum);
}

void test_work_stealing_multi_thread() {
    Queue<std::uint64_t, kind::work_stealing<1024>> q;
    constexpr std::uint64_t N = 20000;

    std::atomic<bool> owner_done{false};
    std::atomic<std::uint64_t> taken_count{0};
    std::atomic<std::uint64_t> taken_sum{0};

    std::jthread owner_t([&q, &owner_done, &taken_count, &taken_sum](std::stop_token) {
        auto owner = q.owner_handle();
        std::uint64_t local_taken_count = 0;
        std::uint64_t local_taken_sum = 0;
        for (std::uint64_t i = 1; i <= N; ++i) {
            while (!owner.try_push(i)) {
                // The thieves drain, so a full deque is temporary.
            }
            // Pop now and then, or the owner outruns the thieves and
            // the deque stays full.
            if (i % 4 == 0) {
                if (auto v = owner.try_pop()) {
                    local_taken_count += 1;
                    local_taken_sum += *v;
                }
            }
        }
        while (auto v = owner.try_pop()) {
            local_taken_count += 1;
            local_taken_sum += *v;
        }
        owner_done.store(true, std::memory_order_release);
        taken_count.fetch_add(local_taken_count, std::memory_order_acq_rel);
        taken_sum.fetch_add(local_taken_sum, std::memory_order_acq_rel);
    });

    constexpr int NUM_THIEVES = 3;
    std::vector<std::jthread> thieves;
    for (int t = 0; t < NUM_THIEVES; ++t) {
        thieves.emplace_back([&q, &owner_done, &taken_count, &taken_sum](std::stop_token) {
            auto thief = q.thief_handle();
            std::uint64_t local_taken_count = 0;
            std::uint64_t local_taken_sum = 0;
            while (true) {
                auto v = thief.try_steal();
                if (v) {
                    local_taken_count += 1;
                    local_taken_sum += *v;
                } else if (owner_done.load(std::memory_order_acquire)) {
                    // One further steal collects an item the owner
                    // pushed between the failed steal and this read.
                    auto last = thief.try_steal();
                    if (!last) break;
                    local_taken_count += 1;
                    local_taken_sum += *last;
                }
            }
            taken_count.fetch_add(local_taken_count, std::memory_order_acq_rel);
            taken_sum.fetch_add(local_taken_sum, std::memory_order_acq_rel);
        });
    }

    owner_t.join();
    for (auto& t : thieves)
        t.join();

    const std::uint64_t expected_sum = N * (N + 1) / 2;
    CRUCIBLE_TEST_REQUIRE(taken_count.load() == N);
    CRUCIBLE_TEST_REQUIRE(taken_sum.load() == expected_sum);
}

template <typename Q>
void scenario_drive_spsc(Q& q) {
    auto p = q.producer_handle();
    auto c = q.consumer_handle();
    CRUCIBLE_TEST_REQUIRE(p.try_push(42u));
    CRUCIBLE_TEST_REQUIRE(p.try_push(43u));
    auto v0 = c.try_pop();
    auto v1 = c.try_pop();
    CRUCIBLE_TEST_REQUIRE(v0 && *v0 == 42u);
    CRUCIBLE_TEST_REQUIRE(v1 && *v1 == 43u);
}

void test_auto_queue_t_deduction() {
    constexpr WorkloadHint h_spsc{
        .producer_count = 1,
        .consumer_count = 1,
        .capacity = 32,
    };
    using AutoSpsc = auto_queue_t<std::uint64_t, h_spsc>;
    static_assert(std::is_same_v<AutoSpsc, Queue<std::uint64_t, kind::spsc<32>>>);
    AutoSpsc q_spsc;
    scenario_drive_spsc(q_spsc);

    constexpr WorkloadHint h_mpsc{
        .producer_count = 4,
        .consumer_count = 1,
        .capacity = 256,
    };
    using AutoMpsc = auto_queue_t<std::uint64_t, h_mpsc>;
    static_assert(std::is_same_v<AutoMpsc, Queue<std::uint64_t, kind::mpsc<256>>>);
    AutoMpsc q_mpsc;
    auto p_mpsc = q_mpsc.producer_handle();
    auto c_mpsc = q_mpsc.consumer_handle();
    CRUCIBLE_TEST_REQUIRE(p_mpsc.try_push(7u));
    auto v = c_mpsc.try_pop();
    CRUCIBLE_TEST_REQUIRE(v && *v == 7u);

    constexpr WorkloadHint h_sharded{
        .producer_count = 3,
        .consumer_count = 5,
        .capacity = 16,
    };
    using AutoSharded = auto_queue_t<std::uint64_t, h_sharded>;
    static_assert(std::is_same_v<AutoSharded, Queue<std::uint64_t, kind::sharded<3, 5, 16, RoundRobinRouting>>>);
    AutoSharded q_sharded;
    auto p_sh = q_sharded.producer_handle(0);
    auto c_sh = q_sharded.consumer_handle(0);
    CRUCIBLE_TEST_REQUIRE(p_sh.try_push(99u));
    auto vsh = c_sh.try_pop();
    CRUCIBLE_TEST_REQUIRE(vsh && *vsh == 99u);

    constexpr WorkloadHint h_ws{
        .producer_count = 1,
        .consumer_count = 1,
        .capacity = 64,
        .work_stealing = true,
    };
    using AutoWs = auto_queue_t<std::uint64_t, h_ws>;
    static_assert(std::is_same_v<AutoWs, Queue<std::uint64_t, kind::work_stealing<64>>>);
    AutoWs q_ws;
    auto owner = q_ws.owner_handle();
    auto thief = q_ws.thief_handle();
    CRUCIBLE_TEST_REQUIRE(owner.try_push(11u));
    auto stolen = thief.try_steal();
    CRUCIBLE_TEST_REQUIRE(stolen && *stolen == 11u);
}

// The same exercise as the multi-threaded test above, driven by a
// permission fork instead of atomic flags and a spin on a peer's
// signal.  The producer stops after N pushes and the consumer after N
// pops, so neither waits on the other's state, and the fork's join
// supplies the ordering the atomics were there to provide.  There is
// no done-flag to miss, no memory order to get wrong, and no exit
// predicate to compute incorrectly.  A second producer handle for the
// same tag cannot be built either: the permission is linear and the
// first handle consumed it.

namespace permissioned_test {
// One tag per logical channel.  Distinct tags give distinct
// ownership triples, so several queues coexist without colliding.
struct SpscChannel {};
}  // namespace permissioned_test

void test_spsc_mint_permission_fork() {
    using namespace crucible::concurrent::queue_tag;

    Queue<std::uint64_t, kind::spsc<1024>> q;
    constexpr std::uint64_t N = 50000;

    // These slots are plain, not atomic.  The fork joins both threads
    // before it returns, and that join is what orders the consumer's
    // writes before the reads further down.
    std::uint64_t received_sum = 0;
    std::uint64_t received_count = 0;

    auto whole = mint_permission_root<Whole<permissioned_test::SpscChannel>>();

    auto rebuilt =
        mint_permission_fork<Producer<permissioned_test::SpscChannel>, Consumer<permissioned_test::SpscChannel>>(
            ::crucible::safety::PermissionForkSpawnCtx{}, std::move(whole),
            [&q](Permission<Producer<permissioned_test::SpscChannel>>&& p,
                 ::crucible::safety::PermissionForkSpawnCtx const&) noexcept {
                auto handle = q.producer_handle(std::move(p));
                for (std::uint64_t i = 1; i <= N; ++i) {
                    while (!handle.try_push(i)) {
                        // This waits on backpressure alone, never on a
                        // peer's signal, and the consumer's drain rate
                        // bounds it.
                    }
                }
            },
            [&q, &received_sum, &received_count](Permission<Consumer<permissioned_test::SpscChannel>>&& c,
                                                 ::crucible::safety::PermissionForkSpawnCtx const&) noexcept {
                auto handle = q.consumer_handle(std::move(c));
                std::uint64_t local_sum = 0;
                std::uint64_t local_count = 0;
                while (local_count < N) {
                    if (auto v = handle.try_pop()) {
                        local_sum += *v;
                        local_count += 1;
                    }
                }
                received_sum = local_sum;
                received_count = local_count;
            });

    const std::uint64_t expected_sum = N * (N + 1) / 2;
    CRUCIBLE_TEST_REQUIRE(received_count == N);
    CRUCIBLE_TEST_REQUIRE(received_sum == expected_sum);

    // Dropping the rebuilt permission says the region is finished
    // with.  Letting it leave scope says the same thing.
    crucible::safety::permission_drop(std::move(rebuilt));
}

void test_permission_integration_compile_time() {
    using SpscQ = Queue<std::uint64_t, kind::spsc<16>>;
    using PProd = SpscQ::PermissionedProducerHandle<permissioned_test::SpscChannel>;
    using PCons = SpscQ::PermissionedConsumerHandle<permissioned_test::SpscChannel>;

    static_assert(sizeof(PProd) == sizeof(void*), "PermissionedProducerHandle must collapse to sizeof(Queue*) via EBO");
    static_assert(sizeof(PCons) == sizeof(void*), "PermissionedConsumerHandle must collapse to sizeof(Queue*) via EBO");

    static_assert(!std::is_copy_constructible_v<PProd>);
    static_assert(!std::is_copy_assignable_v<PProd>);
    static_assert(std::is_move_constructible_v<PProd>);
    static_assert(std::is_nothrow_move_constructible_v<PProd>);

    static_assert(!std::is_copy_constructible_v<PCons>);
    static_assert(std::is_move_constructible_v<PCons>);

    static_assert(QueueProducer<PProd>);
    static_assert(QueueConsumer<PCons>);

    static_assert(!std::is_same_v<PProd, SpscQ::ProducerHandle>);
}

void test_concept_compliance_compile_time() {
    using SpscQ = Queue<std::uint64_t, kind::spsc<16>>;
    using MpscQ = Queue<std::uint64_t, kind::mpsc<16>>;
    using ShardedQ = Queue<std::uint64_t, kind::sharded<2, 2, 16>>;
    using WsQ = Queue<std::uint64_t, kind::work_stealing<16>>;

    static_assert(QueueProducer<SpscQ::ProducerHandle>);
    static_assert(QueueConsumer<SpscQ::ConsumerHandle>);

    static_assert(QueueProducer<MpscQ::ProducerHandle>);
    static_assert(QueueConsumer<MpscQ::ConsumerHandle>);

    static_assert(QueueProducer<ShardedQ::ProducerHandle>);
    static_assert(QueueConsumer<ShardedQ::ConsumerHandle>);

    static_assert(QueueProducer<WsQ::OwnerHandle>);
    static_assert(QueueConsumer<WsQ::OwnerHandle>);

    static_assert(Stealable<WsQ::ThiefHandle>);
    static_assert(!QueueProducer<WsQ::ThiefHandle>);
    static_assert(!QueueConsumer<WsQ::ThiefHandle>);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_queue_facade:\n");

    test_concept_compliance_compile_time();
    test_permission_integration_compile_time();

    run_test("test_spsc_single_thread", test_spsc_single_thread);
    run_test("test_mpsc_single_thread", test_mpsc_single_thread);
    run_test("test_sharded_single_thread", test_sharded_single_thread);
    run_test("test_work_stealing_single_thread", test_work_stealing_single_thread);

    run_test("test_spsc_multi_thread", test_spsc_multi_thread);
    run_test("test_mpsc_multi_thread", test_mpsc_multi_thread);
    run_test("test_sharded_multi_thread", test_sharded_multi_thread);
    run_test("test_work_stealing_multi_thread", test_work_stealing_multi_thread);

    run_test("test_auto_queue_t_deduction", test_auto_queue_t_deduction);

    run_test("test_spsc_mint_permission_fork", test_spsc_mint_permission_fork);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
