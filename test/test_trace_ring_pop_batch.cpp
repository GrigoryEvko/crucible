// A bulk drain must deliver exactly what the same number of single pops
// would deliver, in the same order.  Everything below is a way of
// cornering that one claim.

#include <crucible/Platform.h>
#include <crucible/TraceRing.h>

#include <atomic>
#include "test_assert.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::MetaIndex;
using crucible::TraceRing;

// Every field of an entry is derived from the sequence number, so a
// drained entry identifies its own position in the push order.
static TraceRing::Entry make_entry(uint64_t i) noexcept {
    TraceRing::Entry e{};
    e.schema_hash = SchemaHash{i};
    e.shape_hash = ShapeHash{i ^ 0xDEADBEEFCAFEBABEULL};
    e.num_inputs = static_cast<uint16_t>(i & 0xFF);
    e.num_outputs = static_cast<uint16_t>((i >> 8) & 0xFF);
    return e;
}

static MetaIndex make_meta(uint64_t i) noexcept { return MetaIndex{static_cast<uint32_t>(i & 0xFFFFFF)}; }

static ScopeHash make_scope(uint64_t i) noexcept { return ScopeHash{i * 0x9E3779B97F4A7C15ULL}; }

static CallsiteHash make_callsite(uint64_t i) noexcept { return CallsiteHash{i ^ 0xCBF29CE484222325ULL}; }

static void test_empty_returns_zero() {
    auto ring = std::make_unique<TraceRing>();

    TraceRing::Entry out_entries[8];
    MetaIndex out_metas[8];
    ScopeHash out_scopes[8];
    CallsiteHash out_calls[8];

    uint32_t n = ring->try_pop_batch(out_entries, out_metas, out_scopes, out_calls, 8);
    assert(n == 0);
    assert(ring->size().peek() == 0);

    std::printf("  test_empty_returns_zero: PASSED\n");
}

// A count of zero returns zero without touching the output buffers,
// which is why the precondition permits null output pointers in that
// one case.
static void test_max_count_zero() {
    auto ring = std::make_unique<TraceRing>();

    // The ring is deliberately non-empty: a zero count must win over
    // available entries.
    assert(ring->try_append(make_entry(7)));
    assert(ring->size().peek() == 1);

    TraceRing::Entry out_entries[8];
    MetaIndex out_metas[8];
    ScopeHash out_scopes[8];
    CallsiteHash out_calls[8];

    uint32_t n = ring->try_pop_batch(out_entries, out_metas, out_scopes, out_calls, 0);
    assert(n == 0);
    assert(ring->size().peek() == 1);  // entry not drained

    std::printf("  test_max_count_zero: PASSED\n");
}

static void test_single_thread_fifo() {
    auto ring = std::make_unique<TraceRing>();

    constexpr uint32_t N = 100;
    for (uint64_t i = 0; i < N; ++i) {
        assert(ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i)));
    }
    assert(ring->size().peek() == N);

    TraceRing::Entry out_entries[N];
    MetaIndex out_metas[N];
    ScopeHash out_scopes[N];
    CallsiteHash out_calls[N];

    uint32_t n = ring->try_pop_batch(out_entries, out_metas, out_scopes, out_calls, N);
    assert(n == N);
    assert(ring->size().peek() == 0);

    for (uint32_t i = 0; i < N; ++i) {
        assert(out_entries[i].schema_hash == SchemaHash{i});
        assert(out_entries[i].shape_hash == ShapeHash{uint64_t(i) ^ 0xDEADBEEFCAFEBABEULL});
        assert(out_entries[i].num_inputs == (i & 0xFF));
        assert(out_metas[i] == make_meta(i));
        assert(out_scopes[i] == make_scope(i));
        assert(out_calls[i] == make_callsite(i));
    }

    std::printf("  test_single_thread_fifo: PASSED\n");
}

static void test_multi_batch_drain() {
    auto ring = std::make_unique<TraceRing>();

    constexpr uint32_t N = 1000;
    for (uint64_t i = 0; i < N; ++i) {
        assert(ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i)));
    }

    TraceRing::Entry out_entries[N];
    MetaIndex out_metas[N];
    ScopeHash out_scopes[N];
    CallsiteHash out_calls[N];

    // The sizes are irregular on purpose, so no drain starts or ends on
    // a round boundary and none of them is a multiple of another.
    constexpr uint32_t batch_sizes[] = {7, 13, 31, 64, 100, 200, 500, 100, 50};
    uint32_t total = 0;
    for (uint32_t batch_size : batch_sizes) {
        uint32_t n = ring->try_pop_batch(out_entries + total, out_metas + total, out_scopes + total, out_calls + total,
                                         batch_size);
        total += n;
        if (total >= N) break;
    }

    assert(total == N);
    for (uint32_t i = 0; i < N; ++i) {
        assert(out_entries[i].schema_hash == SchemaHash{i});
        assert(out_metas[i] == make_meta(i));
    }

    std::printf("  test_multi_batch_drain: PASSED\n");
}

// Draining a run of entries that straddles the end of the ring has to
// copy two segments rather than one.  The push-then-drain below moves
// head and tail away from zero without leaving live entries behind,
// which is the only way to reach that boundary from an empty ring.

static void test_wrap_around() {
    auto ring = std::make_unique<TraceRing>();

    // The staging buffers are heap vectors: an array of half the ring's
    // entries would not fit on the stack.
    constexpr uint32_t HALF = TraceRing::CAPACITY / 2;
    for (uint64_t i = 0; i < HALF; ++i) {
        assert(ring->try_append(make_entry(i)));
    }
    {
        std::vector<TraceRing::Entry> e(HALF);
        std::vector<MetaIndex> m(HALF);
        std::vector<ScopeHash> s(HALF);
        std::vector<CallsiteHash> c(HALF);
        uint32_t n = ring->try_pop_batch(e.data(), m.data(), s.data(), c.data(), HALF);
        assert(n == HALF);
    }

    constexpr uint32_t WRAP_N = 1000;
    for (uint64_t i = 0; i < WRAP_N; ++i) {
        // The payloads start well above the ones drained already, so an
        // entry from the first round would be recognisable here.
        assert(ring->try_append(make_entry(0x10000ULL + i)));
    }

    std::vector<TraceRing::Entry> e(WRAP_N);
    std::vector<MetaIndex> m(WRAP_N);
    std::vector<ScopeHash> s(WRAP_N);
    std::vector<CallsiteHash> c(WRAP_N);
    uint32_t n = ring->try_pop_batch(e.data(), m.data(), s.data(), c.data(), WRAP_N);
    assert(n == WRAP_N);

    for (uint32_t i = 0; i < WRAP_N; ++i) {
        assert(e[i].schema_hash == SchemaHash{0x10000ULL + i});
    }

    std::printf("  test_wrap_around: PASSED\n");
}

// Two rings receive the same stream.  One is drained in random batch
// sizes and the other one entry at a time, and the two output
// sequences are then compared field by field.  This is the claim the
// rest of the file only approaches from one side at a time.

static void test_equivalence_with_single_pop() {
    auto ring_a = std::make_unique<TraceRing>();
    auto ring_b = std::make_unique<TraceRing>();

    constexpr uint32_t N = 5000;
    for (uint64_t i = 0; i < N; ++i) {
        assert(ring_a->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i)));
        assert(ring_b->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i)));
    }

    std::vector<TraceRing::Entry> a_entries(N), b_entries(N);
    std::vector<MetaIndex> a_metas(N), b_metas(N);
    std::vector<ScopeHash> a_scopes(N), b_scopes(N);
    std::vector<CallsiteHash> a_calls(N), b_calls(N);

    // The seed is fixed, so a failing schedule is reproducible.
    std::mt19937_64 rng{0xCAFEBABEDEADBEEFULL};
    std::uniform_int_distribution<uint32_t> batch_dist(1, 64);
    uint32_t a_total = 0;
    while (a_total < N) {
        uint32_t batch = std::min(batch_dist(rng), N - a_total);
        uint32_t n = ring_a->try_pop_batch(a_entries.data() + a_total, a_metas.data() + a_total,
                                           a_scopes.data() + a_total, a_calls.data() + a_total, batch);
        assert(n == batch);  // the batch is clamped, so it always fills
        a_total += n;
    }

    for (uint32_t i = 0; i < N; ++i) {
        uint32_t n =
            ring_b->drain(b_entries.data() + i, 1, b_metas.data() + i, b_scopes.data() + i, b_calls.data() + i);
        assert(n == 1);
    }

    for (uint32_t i = 0; i < N; ++i) {
        assert(a_entries[i].schema_hash == b_entries[i].schema_hash);
        assert(a_entries[i].shape_hash == b_entries[i].shape_hash);
        assert(a_entries[i].num_inputs == b_entries[i].num_inputs);
        assert(a_entries[i].num_outputs == b_entries[i].num_outputs);
        assert(a_metas[i] == b_metas[i]);
        assert(a_scopes[i] == b_scopes[i]);
        assert(a_calls[i] == b_calls[i]);
    }

    std::printf("  test_equivalence_with_single_pop: PASSED (%u entries)\n", N);
}

// One producer, one consumer, and a consumer that keeps changing how it
// drains.  Mixing the two drain paths against a live producer is what
// exposes an index that one path advances differently from the other.

static void test_spsc_stress() {
    auto ring = std::make_unique<TraceRing>();
    constexpr uint64_t N = 100'000;

    std::printf("  test_spsc_stress: 1 producer + 1 consumer, N=%llu...\n", static_cast<unsigned long long>(N));

    std::atomic<bool> producer_done{false};
    std::atomic<uint64_t> received{0};

    // The consumer thread owns this vector for the whole run and main
    // reads it only after both threads have joined.  Each delivered
    // schema hash is its own push index, so the vector must come out as
    // 0, 1, 2 and so on with nothing missing or repeated.
    std::vector<uint64_t> mirror;
    mirror.reserve(N);

    std::jthread producer([&](std::stop_token /*st*/) {
        for (uint64_t i = 0; i < N; ++i) {
            while (!ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i))) {
                // The ring is full, so back off and retry the same
                // index rather than dropping it.
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::jthread consumer([&](std::stop_token /*st*/) {
        TraceRing::Entry out_entries[64];
        MetaIndex out_metas[64];
        ScopeHash out_scopes[64];
        CallsiteHash out_calls[64];

        uint64_t mode = 0;
        while (received.load(std::memory_order_relaxed) < N) {
            uint32_t got = 0;
            const uint64_t m = mode++ & 0x3;
            if (m == 0) {
                got = ring->try_pop_batch(out_entries, out_metas, out_scopes, out_calls, 64);
            } else if (m == 1) {
                got = ring->try_pop_batch(out_entries, out_metas, out_scopes, out_calls, 16);
            } else if (m == 2) {
                got = ring->drain(out_entries, 32, out_metas, out_scopes, out_calls);
            } else {
                got = ring->drain(out_entries, 1, out_metas, out_scopes, out_calls);
            }

            if (got > 0) {
                for (uint32_t i = 0; i < got; ++i) {
                    mirror.push_back(out_entries[i].schema_hash.raw());
                }
                received.fetch_add(got, std::memory_order_release);
            } else if (producer_done.load(std::memory_order_acquire) && ring->size().peek() == 0) {
                break;
            } else {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    });

    // Assigning a default-constructed thread destroys the running one,
    // which joins it.  Main must not read the mirror before this.
    producer = std::jthread{};
    consumer = std::jthread{};

    assert(mirror.size() == N);
    for (uint64_t i = 0; i < N; ++i) {
        if (mirror[i] != i) {
            std::fprintf(stderr, "FIFO violation at i=%llu: expected %llu, got %llu\n",
                         static_cast<unsigned long long>(i), static_cast<unsigned long long>(i),
                         static_cast<unsigned long long>(mirror[i]));
            std::abort();
        }
    }

    std::printf("  test_spsc_stress: PASSED (%llu entries delivered in order)\n", static_cast<unsigned long long>(N));
}

int main() {
    std::printf("test_trace_ring_pop_batch:\n");

    test_empty_returns_zero();
    test_max_count_zero();
    test_single_thread_fifo();
    test_multi_batch_drain();
    test_wrap_around();
    test_equivalence_with_single_pop();
    test_spsc_stress();

    std::printf("test_trace_ring_pop_batch: ALL PASSED\n");
    return 0;
}
