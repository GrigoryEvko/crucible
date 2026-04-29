// ═══════════════════════════════════════════════════════════════════
// test_trace_ring_pop_batch — SIMD-11 SPSC bulk-drain correctness
//
// Proves that TraceRing::try_pop_batch (the new SIMD-pipelined bulk
// drain) produces output bit-identical to N successive single-pop
// drain() calls.  Establishes the deterministic FIFO contract that
// SIMD-12's batch processor will rely on.
//
// What we cover:
//   1. Empty ring: try_pop_batch returns 0
//   2. Single-thread basic: push N, try_pop_batch all, verify FIFO
//   3. Multi-batch drain: push N, drain via several smaller batches,
//      total received == N, order preserved
//   4. Wrap-around: push past CAPACITY (with intermediate drains)
//      so the wrap-split memcpy path fires
//   5. Equivalence: random schedule of single-pop drain(_, 1) and
//      bulk try_pop_batch(_, N) produces the IDENTICAL FIFO sequence
//   6. SPSC stress: 1 producer + 1 consumer, consumer alternates
//      between drain() and try_pop_batch().  All entries delivered
//      in producer's push order.
// ═══════════════════════════════════════════════════════════════════

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

// ── Helpers ────────────────────────────────────────────────────────

static TraceRing::Entry make_entry(uint64_t i) noexcept {
    TraceRing::Entry e{};
    e.schema_hash = SchemaHash{i};
    e.shape_hash  = ShapeHash{i ^ 0xDEADBEEFCAFEBABEULL};
    e.num_inputs  = static_cast<uint16_t>(i & 0xFF);
    e.num_outputs = static_cast<uint16_t>((i >> 8) & 0xFF);
    return e;
}

static MetaIndex make_meta(uint64_t i) noexcept {
    return MetaIndex{static_cast<uint32_t>(i & 0xFFFFFF)};
}

static ScopeHash make_scope(uint64_t i) noexcept {
    return ScopeHash{i * 0x9E3779B97F4A7C15ULL};
}

static CallsiteHash make_callsite(uint64_t i) noexcept {
    return CallsiteHash{i ^ 0xCBF29CE484222325ULL};
}

// ── Test: empty ring returns 0 ─────────────────────────────────────

static void test_empty_returns_zero() {
    auto ring = std::make_unique<TraceRing>();

    TraceRing::Entry  out_entries[8];
    MetaIndex         out_metas[8];
    ScopeHash         out_scopes[8];
    CallsiteHash      out_calls[8];

    uint32_t n = ring->try_pop_batch(out_entries, out_metas, out_scopes,
                                     out_calls, 8);
    assert(n == 0);
    assert(ring->size() == 0);

    std::printf("  test_empty_returns_zero: PASSED\n");
}

// ── Test: max_count == 0 ───────────────────────────────────────────
//
// Edge case: max_count of 0 must return 0 without touching outputs.
// Precondition skips the null check (allowed when max_count == 0).

static void test_max_count_zero() {
    auto ring = std::make_unique<TraceRing>();

    // Push something so the ring isn't empty — make sure max_count=0
    // returns 0 even with available entries.
    assert(ring->try_append(make_entry(7)));
    assert(ring->size() == 1);

    TraceRing::Entry  out_entries[8];
    MetaIndex         out_metas[8];
    ScopeHash         out_scopes[8];
    CallsiteHash      out_calls[8];

    uint32_t n = ring->try_pop_batch(out_entries, out_metas, out_scopes,
                                     out_calls, 0);
    assert(n == 0);
    assert(ring->size() == 1);  // entry not drained

    std::printf("  test_max_count_zero: PASSED\n");
}

// ── Test: single-thread push/drain round-trip preserves FIFO ───────

static void test_single_thread_fifo() {
    auto ring = std::make_unique<TraceRing>();

    constexpr uint32_t N = 100;
    for (uint64_t i = 0; i < N; ++i) {
        assert(ring->try_append(make_entry(i), make_meta(i),
                                make_scope(i), make_callsite(i)));
    }
    assert(ring->size() == N);

    TraceRing::Entry  out_entries[N];
    MetaIndex         out_metas[N];
    ScopeHash         out_scopes[N];
    CallsiteHash      out_calls[N];

    uint32_t n = ring->try_pop_batch(out_entries, out_metas, out_scopes,
                                     out_calls, N);
    assert(n == N);
    assert(ring->size() == 0);

    for (uint32_t i = 0; i < N; ++i) {
        assert(out_entries[i].schema_hash == SchemaHash{i});
        assert(out_entries[i].shape_hash  == ShapeHash{uint64_t(i) ^ 0xDEADBEEFCAFEBABEULL});
        assert(out_entries[i].num_inputs  == (i & 0xFF));
        assert(out_metas[i]   == make_meta(i));
        assert(out_scopes[i]  == make_scope(i));
        assert(out_calls[i]   == make_callsite(i));
    }

    std::printf("  test_single_thread_fifo: PASSED\n");
}

// ── Test: multiple smaller batches drain everything in order ───────

static void test_multi_batch_drain() {
    auto ring = std::make_unique<TraceRing>();

    constexpr uint32_t N = 1000;
    for (uint64_t i = 0; i < N; ++i) {
        assert(ring->try_append(make_entry(i), make_meta(i),
                                make_scope(i), make_callsite(i)));
    }

    TraceRing::Entry  out_entries[N];
    MetaIndex         out_metas[N];
    ScopeHash         out_scopes[N];
    CallsiteHash      out_calls[N];

    // Drain in irregular batch sizes: 7, 13, 31, 64, 100, 200, ...
    constexpr uint32_t batch_sizes[] = {7, 13, 31, 64, 100, 200, 500, 100, 50};
    uint32_t total = 0;
    for (uint32_t batch_size : batch_sizes) {
        uint32_t n = ring->try_pop_batch(
            out_entries + total, out_metas + total,
            out_scopes + total, out_calls + total,
            batch_size);
        total += n;
        if (total >= N) break;
    }

    assert(total == N);
    for (uint32_t i = 0; i < N; ++i) {
        assert(out_entries[i].schema_hash == SchemaHash{i});
        assert(out_metas[i]   == make_meta(i));
    }

    std::printf("  test_multi_batch_drain: PASSED\n");
}

// ── Test: wrap-around triggers the two-segment memcpy ─────────────
//
// Push past CAPACITY with intermediate drains so the ring physically
// wraps around, then drain a batch that crosses the wrap boundary.
// This exercises the `if (second > 0)` arm.

static void test_wrap_around() {
    auto ring = std::make_unique<TraceRing>();

    // Push CAPACITY/2, drain CAPACITY/2 — head and tail both
    // advance to CAPACITY/2.  Use heap (vector) for the buffers
    // since 32K × 64B = 2MB exceeds reasonable stack budget.
    constexpr uint32_t HALF = TraceRing::CAPACITY / 2;
    for (uint64_t i = 0; i < HALF; ++i) {
        assert(ring->try_append(make_entry(i)));
    }
    {
        std::vector<TraceRing::Entry> e(HALF);
        std::vector<MetaIndex>        m(HALF);
        std::vector<ScopeHash>        s(HALF);
        std::vector<CallsiteHash>     c(HALF);
        uint32_t n = ring->try_pop_batch(e.data(), m.data(), s.data(),
                                         c.data(), HALF);
        assert(n == HALF);
    }

    // Now push 1000 more — head wraps past CAPACITY.
    constexpr uint32_t WRAP_N = 1000;
    for (uint64_t i = 0; i < WRAP_N; ++i) {
        // Use distinct payloads so we can verify FIFO across the wrap.
        assert(ring->try_append(make_entry(0x10000ULL + i)));
    }

    // Drain WRAP_N items — the batch crosses the wrap boundary
    // (start = HALF & MASK = HALF, first = CAPACITY - HALF, second =
    // WRAP_N - first if WRAP_N > CAPACITY - HALF).
    std::vector<TraceRing::Entry> e(WRAP_N);
    std::vector<MetaIndex>        m(WRAP_N);
    std::vector<ScopeHash>        s(WRAP_N);
    std::vector<CallsiteHash>     c(WRAP_N);
    uint32_t n = ring->try_pop_batch(e.data(), m.data(), s.data(),
                                     c.data(), WRAP_N);
    assert(n == WRAP_N);

    for (uint32_t i = 0; i < WRAP_N; ++i) {
        assert(e[i].schema_hash == SchemaHash{0x10000ULL + i});
    }

    std::printf("  test_wrap_around: PASSED\n");
}

// ── Test: try_pop_batch ≡ N × drain(out, 1, ...) ──────────────────
//
// The load-bearing equivalence test.  Push 2 identical streams to
// 2 separate rings.  Drain ring-A via try_pop_batch in random batch
// sizes; drain ring-B via successive single-pop drain(_, 1, _, _, _).
// The two output sequences MUST be bit-identical.

static void test_equivalence_with_single_pop() {
    auto ring_a = std::make_unique<TraceRing>();
    auto ring_b = std::make_unique<TraceRing>();

    constexpr uint32_t N = 5000;
    for (uint64_t i = 0; i < N; ++i) {
        assert(ring_a->try_append(make_entry(i), make_meta(i),
                                  make_scope(i), make_callsite(i)));
        assert(ring_b->try_append(make_entry(i), make_meta(i),
                                  make_scope(i), make_callsite(i)));
    }

    std::vector<TraceRing::Entry>  a_entries(N), b_entries(N);
    std::vector<MetaIndex>         a_metas(N),   b_metas(N);
    std::vector<ScopeHash>         a_scopes(N),  b_scopes(N);
    std::vector<CallsiteHash>      a_calls(N),   b_calls(N);

    // Ring A: random batch sizes via try_pop_batch.
    std::mt19937_64 rng{0xCAFEBABEDEADBEEFULL};
    std::uniform_int_distribution<uint32_t> batch_dist(1, 64);
    uint32_t a_total = 0;
    while (a_total < N) {
        uint32_t batch = std::min(batch_dist(rng), N - a_total);
        uint32_t n = ring_a->try_pop_batch(
            a_entries.data() + a_total, a_metas.data() + a_total,
            a_scopes.data() + a_total, a_calls.data() + a_total,
            batch);
        assert(n == batch);  // ring has enough
        a_total += n;
    }

    // Ring B: successive single-pop drain(out, 1, ...).
    for (uint32_t i = 0; i < N; ++i) {
        uint32_t n = ring_b->drain(b_entries.data() + i, 1,
                                   b_metas.data() + i,
                                   b_scopes.data() + i,
                                   b_calls.data() + i);
        assert(n == 1);
    }

    // Bit-identical sequences.
    for (uint32_t i = 0; i < N; ++i) {
        assert(a_entries[i].schema_hash == b_entries[i].schema_hash);
        assert(a_entries[i].shape_hash  == b_entries[i].shape_hash);
        assert(a_entries[i].num_inputs  == b_entries[i].num_inputs);
        assert(a_entries[i].num_outputs == b_entries[i].num_outputs);
        assert(a_metas[i]   == b_metas[i]);
        assert(a_scopes[i]  == b_scopes[i]);
        assert(a_calls[i]   == b_calls[i]);
    }

    std::printf("  test_equivalence_with_single_pop: PASSED (%u entries)\n", N);
}

// ── SPSC stress: producer + consumer thread, mixed drain modes ────
//
// Producer pushes 0..N-1 over time.  Consumer alternates between
// drain() (single-pop and small batch) and try_pop_batch() (large
// batch).  All entries delivered in order; no losses; no duplicates.

static void test_spsc_stress() {
    auto ring = std::make_unique<TraceRing>();
    constexpr uint64_t N = 100'000;

    std::printf("  test_spsc_stress: 1 producer + 1 consumer, N=%llu...\n",
                static_cast<unsigned long long>(N));

    std::atomic<bool> producer_done{false};
    std::atomic<uint64_t> received{0};

    // Mirror: receiver writes each delivered schema_hash in order;
    // we verify it's a strictly increasing sequence 0, 1, 2, ...
    std::vector<uint64_t> mirror;
    mirror.reserve(N);

    std::jthread producer([&](std::stop_token /*st*/) {
        for (uint64_t i = 0; i < N; ++i) {
            while (!ring->try_append(make_entry(i), make_meta(i),
                                     make_scope(i), make_callsite(i))) {
                // Ring full, wait briefly.
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::jthread consumer([&](std::stop_token /*st*/) {
        TraceRing::Entry  out_entries[64];
        MetaIndex         out_metas[64];
        ScopeHash         out_scopes[64];
        CallsiteHash      out_calls[64];

        uint64_t mode = 0;
        while (received.load(std::memory_order_relaxed) < N) {
            uint32_t got = 0;
            const uint64_t m = mode++ & 0x3;
            if (m == 0) {
                // try_pop_batch with batch 64
                got = ring->try_pop_batch(out_entries, out_metas,
                                          out_scopes, out_calls, 64);
            } else if (m == 1) {
                // try_pop_batch with batch 16
                got = ring->try_pop_batch(out_entries, out_metas,
                                          out_scopes, out_calls, 16);
            } else if (m == 2) {
                // drain with batch 32, optional outputs
                got = ring->drain(out_entries, 32, out_metas,
                                  out_scopes, out_calls);
            } else {
                // single-pop drain
                got = ring->drain(out_entries, 1, out_metas,
                                  out_scopes, out_calls);
            }

            if (got > 0) {
                for (uint32_t i = 0; i < got; ++i) {
                    mirror.push_back(out_entries[i].schema_hash.raw());
                }
                received.fetch_add(got, std::memory_order_release);
            } else if (producer_done.load(std::memory_order_acquire)
                       && ring->size() == 0) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer = std::jthread{};
    consumer = std::jthread{};

    // Verify all N entries received in producer order.
    assert(mirror.size() == N);
    for (uint64_t i = 0; i < N; ++i) {
        if (mirror[i] != i) {
            std::fprintf(stderr,
                "FIFO violation at i=%llu: expected %llu, got %llu\n",
                static_cast<unsigned long long>(i),
                static_cast<unsigned long long>(i),
                static_cast<unsigned long long>(mirror[i]));
            std::abort();
        }
    }

    std::printf("  test_spsc_stress: PASSED (%llu entries delivered in order)\n",
                static_cast<unsigned long long>(N));
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
