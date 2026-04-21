// Vigil::dispatch_op() integration tests — Tier 1 compiled replay
// through the full pipeline: TraceRing → BackgroundThread → dispatch_op().
//
// Tests cover:
//   1. Basic RECORD → COMPILED transition via dispatch_op()
//   2. Divergence detection and fallback to RECORD
//   3. Recovery: re-activation after divergence + new iteration cycle
//   4. Data flow: writes to output_ptr visible via input_ptr on next op
//   5. Pool bounds + activation wiring

#include <crucible/Vigil.h>
#include "test_harness.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace crucible;

// ── Constants ────────────────────────────────────────────────────────

static constexpr uint32_t NUM_OPS = 8;
static constexpr uint32_t K = Vigil::ALIGNMENT_K;  // 5

static constexpr SchemaHash SCHEMA[NUM_OPS] = {
    SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102}, SchemaHash{0x103},
    SchemaHash{0x104}, SchemaHash{0x105}, SchemaHash{0x106}, SchemaHash{0x107}
};
static constexpr ShapeHash SHAPE[NUM_OPS] = {
    ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
    ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207}
};

// ── Helpers ──────────────────────────────────────────────────────────

static void* fake_ptr(uint32_t iter, uint32_t op) {
    return reinterpret_cast<void*>(
        static_cast<uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

static TensorMeta make_meta(void* data_ptr) {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = 1024;
    m.strides[0] = 1;
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.layout = Layout::Strided;
    m.data_ptr = data_ptr;
    return m;
}

// Op 0: 0 inputs, 1 output.  Ops 1+: 1 input (prev op), 1 output.
struct OpData {
    TraceRing::Entry entry{};
    TensorMeta metas[2]{};
    uint16_t n_metas = 0;
};

static OpData make_op(uint32_t iter, uint32_t op_idx) {
    OpData d;
    d.entry.schema_hash = SCHEMA[op_idx];
    d.entry.shape_hash = SHAPE[op_idx];
    d.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    d.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0)
        d.metas[idx++] = make_meta(fake_ptr(iter, op_idx - 1));
    d.metas[idx++] = make_meta(fake_ptr(iter, op_idx));
    d.n_metas = static_cast<uint16_t>(d.entry.num_inputs + d.entry.num_outputs);
    return d;
}

static void feed_record(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }
}

static void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    }
}

using test::flush_and_wait_compiled;

// Align CrucibleContext via K dispatch_op calls + complete the partial
// iteration.  After return, engine is at position 0, ready for full
// compiled iterations.
//
// Alignment phase: K ops go through RECORDING (each recorded to ring),
// building a sliding-window match against region ops[0..K-1].  Once
// confirmed, CrucibleContext activates and the engine advances past K.
// Partial tail (ops K..NUM_OPS-1) goes through COMPILED to complete
// the first iteration.
static void align_and_activate(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(iter, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::RECORD
               && "alignment ops should return RECORD");
    }
    assert(vigil.context().is_compiled()
           && "CrucibleContext should be compiled after K alignment ops");

    // Complete partial iteration: ops K..NUM_OPS-1 in COMPILED mode.
    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
}

// ── Test 1: Basic dispatch — RECORD → alignment → COMPILED ──────────
//
// Feed 2 iterations + trigger via record_op (bg thread builds region).
// align_and_activate completes the first partial compiled iteration.
// Then a full 8-op compiled iteration: 7×MATCH + 1×COMPLETE.
static void test_dispatch_basic() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);

    align_and_activate(vigil, 3);
    assert(vigil.compiled_iterations() == 1);

    // Full compiled iteration.
    uint32_t match_count = 0;
    uint32_t complete_count = 0;
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(4, i);
        auto result = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);

        if (result.status == ReplayStatus::MATCH)
            match_count++;
        else if (result.status == ReplayStatus::COMPLETE)
            complete_count++;
    }

    assert(match_count == NUM_OPS - 1);
    assert(complete_count == 1);
    assert(vigil.compiled_iterations() == 2);

    std::printf("  test_dispatch_basic: PASSED\n");
}

// ── Test 2: Divergence — mismatched op falls back to RECORD ─────────
//
// Activate via alignment, then send a mismatched schema_hash. Verify:
//   - Returns RECORD with DIVERGED status
//   - Subsequent calls return RECORD
//   - diverged_count() == 1
static void test_dispatch_divergence() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, 3);

    // First few ops of new iteration match.
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(4, i);
        auto result = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);
        assert(result.status == ReplayStatus::MATCH);
    }

    // Op 3: wrong schema → DIVERGED.
    TraceRing::Entry bad_entry{};
    bad_entry.schema_hash = SchemaHash{0xBAD};
    bad_entry.shape_hash = SHAPE[3];
    bad_entry.num_inputs = 1;
    bad_entry.num_outputs = 1;
    TensorMeta bad_metas[2]{};
    bad_metas[0] = make_meta(fake_ptr(4, 2));
    bad_metas[1] = make_meta(fake_ptr(4, 3));

    auto result = vigil.dispatch_op(crucible::vouch(bad_entry), bad_metas, 2);
    assert(result.action == DispatchResult::Action::RECORD);
    assert(result.status == ReplayStatus::DIVERGED);
    assert(vigil.diverged_count() == 1);

    // Subsequent calls should be RECORD (ctx deactivated).
    auto d = make_op(4, 4);
    auto result2 = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
    assert(result2.action == DispatchResult::Action::RECORD);
    assert(!vigil.context().is_compiled());

    std::printf("  test_dispatch_divergence: PASSED\n");
}

// ── Test 3: Recovery — re-activation after divergence ────────────────
//
// After divergence, feed clean iterations via record_op.  Wait for the
// bg thread to produce a new region.  Re-align, verify COMPILED replay.
static void test_dispatch_recovery() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);

    // Activate and immediately diverge.
    align_and_activate(vigil, 3);

    TraceRing::Entry bad{};
    bad.schema_hash = SchemaHash{0xBAD};
    bad.shape_hash = SHAPE[0];
    bad.num_inputs = 0;
    bad.num_outputs = 1;
    TensorMeta bad_meta = make_meta(fake_ptr(99, 0));

    auto r_div = vigil.dispatch_op(crucible::vouch(bad), &bad_meta, 1);
    assert(r_div.action == DispatchResult::Action::RECORD);
    assert(r_div.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());

    // Feed enough clean data for bg thread to find new boundaries.
    // The ring has noise from alignment + divergence; 6 clean iterations
    // guarantee the bg thread sees at least 2 clean boundaries.
    for (uint32_t iter = 10; iter < 16; iter++)
        feed_record(vigil, iter);
    feed_trigger(vigil, 16);

    // bg thread sets mode_=COMPILED and pending_region_ when it finds
    // a boundary in the clean data.  After divergence, mode_ was set
    // to RECORDING, so flush_and_wait detects the new transition.
    flush_and_wait_compiled(vigil);

    // Re-align and activate.
    align_and_activate(vigil, 17);

    // Full compiled iteration to prove recovery works.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(18, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    std::printf("  test_dispatch_recovery: PASSED\n");
}

// ── Test 4: Data flow — output_ptr → input_ptr wiring ───────────────
//
// In COMPILED mode, write byte patterns to output_ptr(0) for each op,
// verify via input_ptr(0) on the next op.  Proves PoolAllocator +
// ReplayEngine + CrucibleContext + Vigil wire dataflow correctly.
static void test_dispatch_data_flow() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, 3);

    // Op 0: write pattern to output.
    auto d0 = make_op(4, 0);
    auto r0 = vigil.dispatch_op(crucible::vouch(d0.entry), d0.metas, d0.n_metas);
    assert(r0.action == DispatchResult::Action::COMPILED);
    assert(r0.status == ReplayStatus::MATCH);
    std::memset(vigil.output_ptr(0), 0x42, 4096);

    // Ops 1-6: verify input carries previous output's data, write new.
    for (uint32_t i = 1; i < NUM_OPS - 1; i++) {
        auto d = make_op(4, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
        assert(r.status == ReplayStatus::MATCH);

        auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(0));
        uint8_t expected = static_cast<uint8_t>(0x42 + i - 1);
        for (uint32_t b = 0; b < 4096; b++)
            assert(in_data[b] == expected);

        std::memset(vigil.output_ptr(0), static_cast<int>(0x42 + i), 4096);
    }

    // Op 7 (final): verify input, advance to COMPLETE.
    auto d7 = make_op(4, NUM_OPS - 1);
    auto r7 = vigil.dispatch_op(crucible::vouch(d7.entry), d7.metas, d7.n_metas);
    assert(r7.action == DispatchResult::Action::COMPILED);
    assert(r7.status == ReplayStatus::COMPLETE);

    auto* in_last = static_cast<uint8_t*>(vigil.input_ptr(0));
    uint8_t expected_last = static_cast<uint8_t>(0x42 + NUM_OPS - 2);
    for (uint32_t b = 0; b < 4096; b++)
        assert(in_last[b] == expected_last);

    // 1 from align_and_activate + 1 from data flow iteration.
    assert(vigil.compiled_iterations() == 2);

    std::printf("  test_dispatch_data_flow: PASSED\n");
}

// ── Test 5: Pool bounds + activation wiring ─────────────────────────
//
// Verifies that CrucibleContext activation through dispatch_op()
// correctly sets up the PoolAllocator — all output pointers are
// within pool bounds.
static void test_dispatch_pool_bounds() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    flush_and_wait_compiled(vigil);
    align_and_activate(vigil, 3);

    auto& pool = vigil.context().pool();
    assert(pool.is_initialized());
    assert(pool.pool_bytes() > 0);

    auto* pool_base = static_cast<uint8_t*>(pool.pool_base());
    uint64_t pool_bytes = pool.pool_bytes();

    // Full iteration — verify every output pointer is within pool.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(4, i);
        auto r = vigil.dispatch_op(crucible::vouch(d.entry), d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);

        auto* p = static_cast<uint8_t*>(vigil.output_ptr(0));
        assert(p >= pool_base);
        assert(p + 4096 <= pool_base + pool_bytes);
    }

    assert(vigil.compiled_iterations() == 2);
    assert(pool.num_external() == 0);

    std::printf("  test_dispatch_pool_bounds: PASSED\n");
}

int main() {
    std::printf("test_vigil_dispatch:\n");
    test_dispatch_basic();
    test_dispatch_divergence();
    test_dispatch_recovery();
    test_dispatch_data_flow();
    test_dispatch_pool_bounds();
    std::printf("test_vigil_dispatch: all tests passed\n");
    return 0;
}
