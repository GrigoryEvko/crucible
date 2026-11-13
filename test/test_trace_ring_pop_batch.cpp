// A bulk drain must deliver exactly what the same number of single pops
// would deliver, in the same order.  Everything below is a way of
// cornering that one claim.

#include <crucible/TraceRing.h>
#include <foundation/Platform.h>

#include <algorithm>
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

// The one call of try_pop_batch in this file.  The batch drain inlines with
// its copies, and one copy of it keeps the object below its size limit.
[[gnu::noinline]] static uint32_t pop_batch(TraceRing& ring, ::fixy::View<TraceRing::Entry> entries,
                                            ::fixy::View<MetaIndex> metas, ::fixy::View<ScopeHash> scopes,
                                            ::fixy::View<CallsiteHash> calls) noexcept {
    return ring.try_pop_batch(entries, metas, scopes, calls);
}

static void test_empty_returns_zero() {
    auto ring = std::make_unique<TraceRing>();

    ::fixy::FixedArray<TraceRing::Entry, 8> out_entries{};
    ::fixy::FixedArray<MetaIndex, 8> out_metas{};
    ::fixy::FixedArray<ScopeHash, 8> out_scopes{};
    ::fixy::FixedArray<CallsiteHash, 8> out_calls{};

    uint32_t n = pop_batch(*ring, out_entries.view(), out_metas.view(), out_scopes.view(), out_calls.view());
    assert(n == 0);
    assert(ring->size().peek() == 0);

    crucible::test::pass("  test_empty_returns_zero: PASSED\n");
}

// Four empty outputs return zero and drain nothing.
static void test_max_count_zero() {
    auto ring = std::make_unique<TraceRing>();

    // The ring is deliberately non-empty: a zero count must win over
    // available entries.
    assert(ring->try_append(make_entry(7)));
    assert(ring->size().peek() == 1);

    uint32_t n = pop_batch(*ring, ::fixy::View<TraceRing::Entry>{}, ::fixy::View<MetaIndex>{},
                           ::fixy::View<ScopeHash>{}, ::fixy::View<CallsiteHash>{});
    assert(n == 0);
    assert(ring->size().peek() == 1);  // entry not drained

    crucible::test::pass("  test_max_count_zero: PASSED\n");
}

static void test_single_thread_fifo() {
    auto ring = std::make_unique<TraceRing>();

    constexpr uint32_t N = 100;
    for (uint64_t i = 0; i < N; ++i) {
        assert(ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i)));
    }
    assert(ring->size().peek() == N);

    ::fixy::FixedArray<TraceRing::Entry, N> out_entries{};
    ::fixy::FixedArray<MetaIndex, N> out_metas{};
    ::fixy::FixedArray<ScopeHash, N> out_scopes{};
    ::fixy::FixedArray<CallsiteHash, N> out_calls{};

    uint32_t n = pop_batch(*ring, out_entries.view(), out_metas.view(), out_scopes.view(), out_calls.view());
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

    crucible::test::pass("  test_single_thread_fifo: PASSED\n");
}

static void test_multi_batch_drain() {
    auto ring = std::make_unique<TraceRing>();

    constexpr uint32_t N = 1000;
    for (uint64_t i = 0; i < N; ++i) {
        assert(ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i)));
    }

    ::fixy::FixedArray<TraceRing::Entry, N> out_entries{};
    ::fixy::FixedArray<MetaIndex, N> out_metas{};
    ::fixy::FixedArray<ScopeHash, N> out_scopes{};
    ::fixy::FixedArray<CallsiteHash, N> out_calls{};

    // The sizes are irregular on purpose, so no drain starts or ends on
    // a round boundary and none of them is a multiple of another.  The
    // last batch stops at the end of the outputs.
    constexpr uint32_t batch_sizes[] = {7, 13, 31, 64, 100, 200, 500, 100, 50};
    uint32_t total = 0;
    for (uint32_t batch_size : batch_sizes) {
        const uint32_t batch = ::fixy::min(batch_size, N - total);
        uint32_t n = pop_batch(*ring, out_entries.view().window(total, batch).expect("the batch fits"),
                               out_metas.view().window(total, batch).expect("the batch fits"),
                               out_scopes.view().window(total, batch).expect("the batch fits"),
                               out_calls.view().window(total, batch).expect("the batch fits"));
        total += n;
        if (total >= N) break;
    }

    assert(total == N);
    for (uint32_t i = 0; i < N; ++i) {
        assert(out_entries[i].schema_hash == SchemaHash{i});
        assert(out_metas[i] == make_meta(i));
    }

    crucible::test::pass("  test_multi_batch_drain: PASSED\n");
}

// Draining a run of entries that straddles the end of the ring has to
// copy two segments rather than one.  The push-then-drain below moves
// head and tail away from zero without leaving live entries behind,
// which is the only way to reach that boundary from an empty ring.

static void test_wrap_around() {
    auto ring = std::make_unique<TraceRing>();

    // The staging buffers have static storage: an array of half the ring's
    // entries would not fit on the stack.
    constexpr uint32_t HALF = TraceRing::CAPACITY / 2;
    for (uint64_t i = 0; i < HALF; ++i) {
        assert(ring->try_append(make_entry(i)));
    }
    {
        static ::fixy::FixedArray<TraceRing::Entry, HALF> half_e{};
        static ::fixy::FixedArray<MetaIndex, HALF> half_m{};
        static ::fixy::FixedArray<ScopeHash, HALF> half_s{};
        static ::fixy::FixedArray<CallsiteHash, HALF> half_c{};
        uint32_t n = pop_batch(*ring, half_e.view(), half_m.view(), half_s.view(), half_c.view());
        assert(n == HALF);
    }

    constexpr uint32_t WRAP_N = 1000;
    for (uint64_t i = 0; i < WRAP_N; ++i) {
        // The payloads start well above the ones drained already, so an
        // entry from the first round would be recognisable here.
        assert(ring->try_append(make_entry(0x10000ULL + i)));
    }

    ::fixy::FixedArray<TraceRing::Entry, WRAP_N> e{};
    ::fixy::FixedArray<MetaIndex, WRAP_N> m{};
    ::fixy::FixedArray<ScopeHash, WRAP_N> s{};
    ::fixy::FixedArray<CallsiteHash, WRAP_N> c{};
    uint32_t n = pop_batch(*ring, e.view(), m.view(), s.view(), c.view());
    assert(n == WRAP_N);

    for (uint32_t i = 0; i < WRAP_N; ++i) {
        assert(e[i].schema_hash == SchemaHash{0x10000ULL + i});
    }

    crucible::test::pass("  test_wrap_around: PASSED\n");
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

    // The outputs have static storage: the two entry arrays would not fit
    // on the stack.
    static ::fixy::FixedArray<TraceRing::Entry, N> a_entries{};
    static ::fixy::FixedArray<TraceRing::Entry, N> b_entries{};
    static ::fixy::FixedArray<MetaIndex, N> a_metas{};
    static ::fixy::FixedArray<MetaIndex, N> b_metas{};
    static ::fixy::FixedArray<ScopeHash, N> a_scopes{};
    static ::fixy::FixedArray<ScopeHash, N> b_scopes{};
    static ::fixy::FixedArray<CallsiteHash, N> a_calls{};
    static ::fixy::FixedArray<CallsiteHash, N> b_calls{};

    // The seed is fixed, so a failing schedule is reproducible.
    std::mt19937_64 rng{0xCAFEBABEDEADBEEFULL};
    std::uniform_int_distribution<uint32_t> batch_dist(1, 64);
    uint32_t a_total = 0;
    while (a_total < N) {
        uint32_t batch = std::min(batch_dist(rng), N - a_total);
        uint32_t n = pop_batch(*ring_a, a_entries.view().window(a_total, batch).expect("the batch fits"),
                               a_metas.view().window(a_total, batch).expect("the batch fits"),
                               a_scopes.view().window(a_total, batch).expect("the batch fits"),
                               a_calls.view().window(a_total, batch).expect("the batch fits"));
        assert(n == batch);  // the batch is clamped, so it always fills
        a_total += n;
    }

    for (uint32_t i = 0; i < N; ++i) {
        uint32_t n = ring_b->drain(b_entries.view().window(i, 1).expect("one entry fits"),
                                   b_metas.view().window(i, 1).expect("one entry fits"),
                                   b_scopes.view().window(i, 1).expect("one entry fits"),
                                   b_calls.view().window(i, 1).expect("one entry fits"));
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

    crucible::test::pass("  test_equivalence_with_single_pop: PASSED ({} entries)\n", N);
}

// One producer, one consumer, and a consumer that keeps changing how it
// drains.  Mixing the two drain paths against a live producer is what
// exposes an index that one path advances differently from the other.

static void test_spsc_stress() {
    auto ring = std::make_unique<TraceRing>();
    constexpr uint64_t N = 100000;

    ::fixy::report(::fixy::Sink::Out, "  test_spsc_stress: 1 producer + 1 consumer, N={}...\n",
                   static_cast<unsigned long long>(N));

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
        ::fixy::FixedArray<TraceRing::Entry, 64> out_entries{};
        ::fixy::FixedArray<MetaIndex, 64> out_metas{};
        ::fixy::FixedArray<ScopeHash, 64> out_scopes{};
        ::fixy::FixedArray<CallsiteHash, 64> out_calls{};

        // The first batch entries of each output.
        const auto drain_batch = [&](uint32_t batch, bool is_pop_batch) -> uint32_t {
            const auto entries = out_entries.view().window(0, batch).expect("the batch fits");
            const auto metas = out_metas.view().window(0, batch).expect("the batch fits");
            const auto scopes = out_scopes.view().window(0, batch).expect("the batch fits");
            const auto calls = out_calls.view().window(0, batch).expect("the batch fits");
            return is_pop_batch ? pop_batch(*ring, entries, metas, scopes, calls)
                                : ring->drain(entries, metas, scopes, calls);
        };

        uint64_t mode = 0;
        while (received.load(std::memory_order_relaxed) < N) {
            uint32_t got = 0;
            const uint64_t m = mode++ & 0x3;
            if (m == 0) {
                got = drain_batch(64, true);
            } else if (m == 1) {
                got = drain_batch(16, true);
            } else if (m == 2) {
                got = drain_batch(32, false);
            } else {
                got = drain_batch(1, false);
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

    crucible::test::pass("  test_spsc_stress: PASSED ({} entries delivered in order)\n",
                         static_cast<unsigned long long>(N));
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_trace_ring_pop_batch:\n");

    test_empty_returns_zero();
    test_max_count_zero();
    test_single_thread_fifo();
    test_multi_batch_drain();
    test_wrap_around();
    test_equivalence_with_single_pop();
    test_spsc_stress();

    crucible::test::pass("test_trace_ring_pop_batch: ALL PASSED\n");
    return 0;
}
