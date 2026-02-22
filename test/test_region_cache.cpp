// RegionCache integration tests — instant switching between compiled regions
// on shape divergence, including mid-iteration data migration.
//
// Tests cover:
//   1. Divergence at pos>0 with cached alternate → switch via cache
//   2. Data integrity: output data written before divergence survives switch
//   3. No alternate available → falls back to RECORDING (regression)
//   4. Cache dedup and capacity
//   5. Repeated alternation (A→B→A→B)

#include <crucible/Vigil.h>
#include <crucible/Effects.h>
#include "test_harness.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace crucible;

// ── Constants ──────────────────────────────────────────────────────

static constexpr uint32_t NUM_OPS = 8;
static constexpr uint32_t K = Vigil::ALIGNMENT_K;  // 5

// Both variants share the same op identity (schemas).
static constexpr SchemaHash SCHEMA[NUM_OPS] = {
    SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102}, SchemaHash{0x103},
    SchemaHash{0x104}, SchemaHash{0x105}, SchemaHash{0x106}, SchemaHash{0x107}
};

// Shape variant A: all tensors size=1024 → 4096 bytes each.
static constexpr ShapeHash SHAPE_A[NUM_OPS] = {
    ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
    ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207}
};

// Shape variant B: shared prefix (ops 0-2, size=1024), different suffix
// (ops 3-7, size=2048 → 8192 bytes each).  Different shape_hash AND
// different actual TensorMeta shapes → different content_hash.
static constexpr ShapeHash SHAPE_B[NUM_OPS] = {
    ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202},  // shared prefix
    ShapeHash{0x303}, ShapeHash{0x304}, ShapeHash{0x305},  // different suffix
    ShapeHash{0x306}, ShapeHash{0x307}
};

// ── Helpers ────────────────────────────────────────────────────────

static void* fake_ptr(uint32_t variant, uint32_t iter, uint32_t op) {
    return reinterpret_cast<void*>(
        static_cast<uintptr_t>(
            (variant + 1) * 0x10000000ULL +
            (iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

// Tensor size for each (variant, op_idx).
//   A: uniform 1024 for all ops.
//   B: 1024 for ops 0-2 (shared prefix), 2048 for ops 3-7 (different suffix).
static int64_t tensor_size(uint32_t variant, uint32_t op_idx) {
    if (variant == 0) return 1024;           // A: all 1024
    return (op_idx < 3) ? 1024 : 2048;      // B: shared prefix, larger suffix
}

static TensorMeta make_meta(void* data_ptr, int64_t size) {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = size;
    m.strides[0] = 1;
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.layout = Layout::Strided;
    m.data_ptr = data_ptr;
    return m;
}

struct OpData {
    TraceRing::Entry entry{};
    TensorMeta metas[2]{};
    uint16_t n_metas = 0;
};

static OpData make_op(const ShapeHash* shapes, uint32_t variant,
                      uint32_t iter, uint32_t op_idx) {
    OpData d;
    d.entry.schema_hash = SCHEMA[op_idx];
    d.entry.shape_hash = shapes[op_idx];
    d.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    d.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0) {
        int64_t in_sz = tensor_size(variant, op_idx - 1);
        d.metas[idx++] = make_meta(fake_ptr(variant, iter, op_idx - 1), in_sz);
    }
    int64_t out_sz = tensor_size(variant, op_idx);
    d.metas[idx++] = make_meta(fake_ptr(variant, iter, op_idx), out_sz);
    d.n_metas = d.entry.num_inputs + d.entry.num_outputs;
    return d;
}

static void feed_record(Vigil& vigil, const ShapeHash* shapes,
                        uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(shapes, variant, iter, i);
        (void)vigil.record_op(d.entry, d.metas, d.n_metas);
    }
}

static void feed_trigger(Vigil& vigil, const ShapeHash* shapes,
                         uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(shapes, variant, iter, i);
        (void)vigil.record_op(d.entry, d.metas, d.n_metas);
    }
}

using test::flush_and_wait_compiled;

static void align_and_activate(Vigil& vigil, const ShapeHash* shapes,
                                uint32_t variant, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(shapes, variant, iter, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::RECORD
               && "alignment ops should return RECORD");
    }
    assert(vigil.context().is_compiled()
           && "CrucibleContext should be compiled after K alignment ops");

    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(shapes, variant, iter, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
}

// ── Test 1: Cache switch at mid-iteration divergence point ────────
//
// Build variant A, then variant B (shared prefix, different suffix).
// Once both cached, switch A→B at op 3 via cache.
static void test_cache_switch_mid_iter() {
    Vigil vigil;

    // Build variant A region.
    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    assert(vigil.region_cache().size() == 1);

    // Complete one full A iteration to confirm it works.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 4, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    // Diverge at op 3 with B shapes.
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 5, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
    auto dB3 = make_op(SHAPE_B, 1, 5, 3);
    auto r_div = vigil.dispatch_op(dB3.entry, dB3.metas, dB3.n_metas);
    assert(r_div.action == DispatchResult::Action::RECORD);
    assert(r_div.status == ReplayStatus::DIVERGED);

    // A is cached.  Now build B via bg thread.
    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, SHAPE_B, 1, iter);
    feed_trigger(vigil, SHAPE_B, 1, 16);

    flush_and_wait_compiled(vigil);

    // Activate B via alignment.
    align_and_activate(vigil, SHAPE_B, 1, 17);

    // Both A and B are now in the cache.
    assert(vigil.region_cache().size() == 2);

    // Full compiled iteration with B.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_B, 1, 18, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    // Now switch back to A shapes — should hit cache at op 3!
    // Shared prefix (ops 0-2) matches B, op 3 diverges → find A in cache.
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 19, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    auto dA3 = make_op(SHAPE_A, 0, 19, 3);
    auto r_switch = vigil.dispatch_op(dA3.entry, dA3.metas, dA3.n_metas);

    assert(r_switch.action == DispatchResult::Action::COMPILED
           && "Expected instant cache switch to variant A");

    // Complete the iteration with A shapes.
    for (uint32_t i = 4; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 19, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    std::printf("  test_cache_switch_mid_iter: PASSED\n");
}

// ── Test 2: Data migration — byte patterns survive pool switch ────
//
// Write distinct byte patterns to ops 0-2 outputs, then diverge at
// op 3.  Switch via cache.  Verify op 3's input (= op 2's output)
// still has the pattern from before the switch.
static void test_cache_data_migration() {
    Vigil vigil;

    // Build variant A region.
    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    // Diverge from A to trigger fallback.
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 4, i);
        (void)vigil.dispatch_op(d.entry, d.metas, d.n_metas);
    }
    auto dB3 = make_op(SHAPE_B, 1, 4, 3);
    auto r_div = vigil.dispatch_op(dB3.entry, dB3.metas, dB3.n_metas);
    assert(r_div.action == DispatchResult::Action::RECORD);

    // Build B via bg thread.
    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, SHAPE_B, 1, iter);
    feed_trigger(vigil, SHAPE_B, 1, 16);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_B, 1, 17);

    assert(vigil.region_cache().size() == 2);

    // Start a B iteration and write byte patterns to ops 0-2 outputs.
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_B, 1, 18, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
        // Op 0-2 output is 1024 * 4 = 4096 bytes (shared prefix shape).
        std::memset(vigil.output_ptr(0), static_cast<int>(0xA0 + i), 4096);
    }

    // Op 3: send A's shape → diverges from B.  Cache should find A.
    auto dA3 = make_op(SHAPE_A, 0, 18, 3);
    auto r_switch = vigil.dispatch_op(dA3.entry, dA3.metas, dA3.n_metas);
    assert(r_switch.action == DispatchResult::Action::COMPILED
           && "Expected cache switch from B to A at pos 3");

    // Verify data from prefix ops survived migration.
    // Op 3's input (= op 2's output) should have the 0xA2 pattern.
    auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(0));
    for (uint32_t b = 0; b < 4096; b++) {
        assert(in_data[b] == 0xA2
               && "Data migration failed: op 2's output not preserved");
    }

    // Complete the iteration with A shapes.
    for (uint32_t i = 4; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 18, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    std::printf("  test_cache_data_migration: PASSED\n");
}

// ── Test 3: No alternate available → RECORDING fallback ───────────
static void test_cache_miss_fallback() {
    Vigil vigil;

    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    // Diverge with a completely different schema — no cache match.
    TraceRing::Entry bad{};
    bad.schema_hash = SchemaHash{0xDEAD};
    bad.shape_hash = ShapeHash{0xBEEF};
    bad.num_inputs = 0;
    bad.num_outputs = 1;
    TensorMeta bad_meta = make_meta(fake_ptr(99, 99, 0), 512);

    auto r = vigil.dispatch_op(bad, &bad_meta, 1);
    assert(r.action == DispatchResult::Action::RECORD);
    assert(r.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());
    assert(vigil.diverged_count() == 1);

    // Region A should still be in cache for future use.
    assert(vigil.region_cache().size() == 1);

    std::printf("  test_cache_miss_fallback: PASSED\n");
}

// ── Test 4: Cache capacity and FIFO eviction ──────────────────────
static void test_cache_dedup_and_cap() {
    fx::Test test;
    RegionCache cache;

    assert(cache.size() == 0);
    assert(cache.empty());

    Arena arena(4096);
    auto* region = make_region(test.alloc, arena, nullptr, 0);
    region->content_hash = ContentHash{0x1234};

    cache.insert(region);
    assert(cache.size() == 1);

    // Dedup: same content_hash → no growth.
    cache.insert(region);
    assert(cache.size() == 1);

    // Insert different regions up to CAP.
    for (uint32_t i = 1; i < RegionCache::CAP; i++) {
        auto* r = make_region(test.alloc, arena, nullptr, 0);
        r->content_hash = ContentHash{0x1234 + i};
        cache.insert(r);
    }
    assert(cache.size() == RegionCache::CAP);

    // One more → evicts oldest, size stays at CAP.
    auto* overflow = make_region(test.alloc, arena, nullptr, 0);
    overflow->content_hash = ContentHash{0xFFFF};
    cache.insert(overflow);
    assert(cache.size() == RegionCache::CAP);

    // The overflowed region should be findable.
    assert(cache.find(ContentHash{0xFFFF}) != nullptr);

    // The oldest (0x1234) should have been evicted.
    assert(cache.find(ContentHash{0x1234}) == nullptr);

    std::printf("  test_cache_dedup_and_cap: PASSED\n");
}

// ── Test 5: Repeated switching (A→B→A→B) ─────────────────────────
//
// Simulate alternating batch sizes.  After both regions are cached,
// every switch at op 3 should be instant.
static void test_cache_repeated_switching() {
    Vigil vigil;

    // Build variant A.
    feed_record(vigil, SHAPE_A, 0, 0);
    feed_record(vigil, SHAPE_A, 0, 1);
    feed_trigger(vigil, SHAPE_A, 0, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_A, 0, 3);

    // Full A iteration.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_A, 0, 4, i);
        (void)vigil.dispatch_op(d.entry, d.metas, d.n_metas);
    }

    // Diverge → build variant B.
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(SHAPE_A, 0, 5, i);
        (void)vigil.dispatch_op(d.entry, d.metas, d.n_metas);
    }
    auto dB3 = make_op(SHAPE_B, 1, 5, 3);
    (void)vigil.dispatch_op(dB3.entry, dB3.metas, dB3.n_metas);

    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, SHAPE_B, 1, iter);
    feed_trigger(vigil, SHAPE_B, 1, 16);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, SHAPE_B, 1, 17);

    assert(vigil.region_cache().size() == 2);

    // Full B iteration.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(SHAPE_B, 1, 18, i);
        (void)vigil.dispatch_op(d.entry, d.metas, d.n_metas);
    }

    // Now alternate A ↔ B repeatedly.  Each switch should be via cache.
    for (uint32_t cycle = 0; cycle < 4; cycle++) {
        const ShapeHash* active_shapes = (cycle % 2 == 0) ? SHAPE_A : SHAPE_B;
        uint32_t variant = (cycle % 2 == 0) ? 0 : 1;
        uint32_t iter = 20 + cycle;

        // Shared prefix (ops 0-2) — always COMPILED.
        for (uint32_t i = 0; i < 3; i++) {
            auto d = make_op(active_shapes, variant, iter, i);
            auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }

        // Op 3: divergence point → cache switch.
        auto d3 = make_op(active_shapes, variant, iter, 3);
        auto r3 = vigil.dispatch_op(d3.entry, d3.metas, d3.n_metas);
        assert(r3.action == DispatchResult::Action::COMPILED
               && "Cache switch failed during repeated alternation");

        // Complete iteration.
        for (uint32_t i = 4; i < NUM_OPS; i++) {
            auto d = make_op(active_shapes, variant, iter, i);
            auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }
    }

    std::printf("  test_cache_repeated_switching: PASSED\n");
}

int main() {
    std::printf("test_region_cache:\n");
    test_cache_dedup_and_cap();
    test_cache_miss_fallback();
    test_cache_switch_mid_iter();
    test_cache_data_migration();
    test_cache_repeated_switching();
    std::printf("test_region_cache: all tests passed\n");
    return 0;
}
