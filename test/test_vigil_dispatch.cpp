// Vigil::dispatch_op() integration tests — Tier 1 compiled replay
// through the full pipeline: TraceRing → BackgroundThread → dispatch_op().
//
// Tests cover:
//   1. Basic RECORD → COMPILED transition via dispatch_op()
//   2. Divergence detection and fallback to RECORD
//   3. Recovery: re-activation after divergence + new iteration cycle
//   4. Data flow: writes to output_ptr visible via input_ptr on next op
//   5. External slot auto-registration from recorded TensorMeta

#include <crucible/Vigil.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace crucible;

// ── Constants matching test_end_to_end.cpp ─────────────────────────

static constexpr uint32_t NUM_OPS = 8;

static constexpr uint64_t SCHEMA[NUM_OPS] = {
    0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107
};
static constexpr uint64_t SHAPE[NUM_OPS] = {
    0x200, 0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x207
};

// ── Helpers ────────────────────────────────────────────────────────

// Unique fake pointer for (iter, op). Never null, never zero.
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

// Build TraceRing::Entry + TensorMeta for one op in an iteration.
// Op 0: 0 inputs, 1 output. Ops 1+: 1 input (from prev op), 1 output.
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
    d.n_metas = d.entry.num_inputs + d.entry.num_outputs;
    return d;
}

// Feed one full iteration via record_op (bypass dispatch).
static void feed_record(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(d.entry, d.metas, d.n_metas);
    }
}

// Feed first K ops (trigger ops for IterationDetector).
static void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(d.entry, d.metas, d.n_metas);
    }
}

// Wait until Vigil::is_compiled() (mode_ set by bg thread).
static void wait_mode_compiled(Vigil& vigil, uint32_t timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (!vigil.is_compiled()) {
        assert(std::chrono::steady_clock::now() < deadline
               && "Vigil did not reach COMPILED mode in time");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Wait until step counter reaches at least the target value.
static void wait_step(Vigil& vigil, uint64_t target, uint32_t timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (vigil.current_step() < target) {
        assert(std::chrono::steady_clock::now() < deadline
               && "Vigil step counter did not reach target in time");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Activate CrucibleContext via ONE dispatch_op call (one-op activation delay).
// Requires: pending_region_ is already set (bg thread produced a region).
// The activation op goes through RECORDING, triggers try_activate_,
// and is "wasted" — the next dispatch_op enters COMPILED.
static void activate_via_dispatch(Vigil& vigil, uint32_t iter) {
    auto d = make_op(iter, 0);
    auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
    assert(r.action == DispatchResult::Action::RECORD
           && "activation op should return RECORD (one-op delay)");
    assert(vigil.context().is_compiled()
           && "CrucibleContext should be compiled after activation dispatch");
}

// ── Test 1: Basic dispatch — RECORD → activation → COMPILED ───────
//
// Feed 2 iterations + trigger via record_op (bg thread builds region).
// One dispatch_op call activates ctx (one-op delay). Subsequent
// dispatch_ops return COMPILED. Full iteration: 7×MATCH + 1×COMPLETE.
static void test_dispatch_basic() {
    Vigil vigil;

    // Feed 2 iterations + K trigger ops → bg produces region.
    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    vigil.flush();
    wait_mode_compiled(vigil);

    // Activate ctx with one dispatch_op (one-op delay).
    activate_via_dispatch(vigil, 3);

    // Full compiled iteration.
    uint32_t match_count = 0;
    uint32_t complete_count = 0;
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(3, i);
        auto result = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);

        if (result.status == ReplayStatus::MATCH)
            match_count++;
        else if (result.status == ReplayStatus::COMPLETE)
            complete_count++;
    }

    assert(match_count == NUM_OPS - 1);
    assert(complete_count == 1);
    assert(vigil.compiled_iterations() == 1);

    std::printf("  test_dispatch_basic: PASSED\n");
}

// ── Test 2: Divergence — mismatched op falls back to RECORD ──────
//
// Activate, then send a mismatched schema_hash. Verify:
//   - Returns RECORD with DIVERGED status
//   - Subsequent calls return RECORD
//   - diverged_count() == 1
static void test_dispatch_divergence() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    vigil.flush();
    wait_mode_compiled(vigil);
    activate_via_dispatch(vigil, 3);

    // First few ops match.
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(3, i);
        auto result = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);
        assert(result.status == ReplayStatus::MATCH);
    }

    // Op 3: wrong schema → DIVERGED.
    TraceRing::Entry bad_entry{};
    bad_entry.schema_hash = 0xBAD;
    bad_entry.shape_hash = SHAPE[3];
    bad_entry.num_inputs = 1;
    bad_entry.num_outputs = 1;
    TensorMeta bad_metas[2]{};
    bad_metas[0] = make_meta(fake_ptr(3, 2));
    bad_metas[1] = make_meta(fake_ptr(3, 3));

    auto result = vigil.dispatch_op(bad_entry, bad_metas, 2);
    assert(result.action == DispatchResult::Action::RECORD);
    assert(result.status == ReplayStatus::DIVERGED);
    assert(vigil.diverged_count() == 1);

    // Subsequent calls should be RECORD (ctx deactivated).
    auto d = make_op(3, 4);
    auto result2 = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
    assert(result2.action == DispatchResult::Action::RECORD);
    assert(result2.status == ReplayStatus::MATCH); // MATCH in RECORD = normal
    assert(!vigil.context().is_compiled());

    std::printf("  test_dispatch_divergence: PASSED\n");
}

// ── Test 3: Recovery — re-activation after divergence ─────────────
//
// After divergence, feed clean iterations via record_op (not dispatch_op,
// to avoid premature activation with a garbage region). Wait for the bg
// thread to produce 2 new regions (1st is garbage from noise, 2nd is
// clean). Then activate and verify COMPILED replay works.
static void test_dispatch_recovery() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    vigil.flush();
    wait_mode_compiled(vigil);

    uint64_t step_after_first = vigil.current_step(); // == 1

    // Activate with one dispatch_op.
    activate_via_dispatch(vigil, 3);

    // 3 compiled ops (these don't go to the ring).
    for (uint32_t i = 0; i < 3; i++) {
        auto d = make_op(3, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    // Diverge at op 3.
    TraceRing::Entry bad{};
    bad.schema_hash = 0xBAD;
    bad.shape_hash = SHAPE[3];
    bad.num_inputs = 1;
    bad.num_outputs = 1;
    TensorMeta bad_metas[2]{};
    bad_metas[0] = make_meta(fake_ptr(99, 0));
    bad_metas[1] = make_meta(fake_ptr(99, 1));

    auto r_div = vigil.dispatch_op(bad, bad_metas, 2);
    assert(r_div.action == DispatchResult::Action::RECORD);
    assert(r_div.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());

    // Feed clean ops via record_op to avoid dispatch_op consuming
    // pending_region_ before the clean region is produced.
    //
    // bg thread's current_trace after 1st boundary has K=5 trigger ops
    // from iter 2. Then it accumulates:
    //   1 activation op + 1 bad op + recovery ops below.
    // The 1st post-divergence boundary produces a garbage region.
    // The 2nd boundary produces a clean 8-op region.
    //
    // We need: remaining iter 2 + 2 full iters + trigger.
    for (uint32_t i = IterationDetector::K; i < NUM_OPS; i++) {
        auto d = make_op(2, i);
        (void)vigil.record_op(d.entry, d.metas, d.n_metas);
    }
    feed_record(vigil, 3);
    feed_record(vigil, 4);
    feed_trigger(vigil, 5);

    vigil.flush();

    // Wait for 2 more boundaries (garbage + clean).
    wait_step(vigil, step_after_first + 2);

    // pending_region_ now has the clean region (2nd boundary overwrites 1st).
    // Activate with one dispatch_op.
    auto d_act = make_op(5, 0);
    auto r_act = vigil.dispatch_op(d_act.entry, d_act.metas, d_act.n_metas);
    assert(r_act.action == DispatchResult::Action::RECORD);
    assert(vigil.context().is_compiled() && "Recovery failed: ctx not compiled");

    // Full compiled iteration to prove it works.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(5, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }

    std::printf("  test_dispatch_recovery: PASSED\n");
}

// ── Test 4: Data flow — output_ptr → input_ptr wiring ─────────────
//
// In COMPILED mode, write byte patterns to output_ptr(0) for each op,
// verify via input_ptr(0) on the next op. Proves PoolAllocator +
// ReplayEngine + CrucibleContext + Vigil wire dataflow correctly.
static void test_dispatch_data_flow() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    vigil.flush();
    wait_mode_compiled(vigil);
    activate_via_dispatch(vigil, 3);

    // Op 0: write pattern to output.
    auto d0 = make_op(3, 0);
    auto r0 = vigil.dispatch_op(d0.entry, d0.metas, d0.n_metas);
    assert(r0.action == DispatchResult::Action::COMPILED);
    assert(r0.status == ReplayStatus::MATCH);
    std::memset(vigil.output_ptr(0), 0x42, 4096);

    // Ops 1-6: verify input carries previous output's data, write new pattern.
    for (uint32_t i = 1; i < NUM_OPS - 1; i++) {
        auto d = make_op(3, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
        assert(r.status == ReplayStatus::MATCH);

        auto* in_data = static_cast<uint8_t*>(vigil.input_ptr(0));
        uint8_t expected = static_cast<uint8_t>(0x42 + i - 1);
        for (uint32_t b = 0; b < 4096; b++) {
            assert(in_data[b] == expected);
        }

        std::memset(vigil.output_ptr(0), static_cast<int>(0x42 + i), 4096);
    }

    // Op 7 (final): verify input, advance to COMPLETE.
    auto d7 = make_op(3, NUM_OPS - 1);
    auto r7 = vigil.dispatch_op(d7.entry, d7.metas, d7.n_metas);
    assert(r7.action == DispatchResult::Action::COMPILED);
    assert(r7.status == ReplayStatus::COMPLETE);

    auto* in_last = static_cast<uint8_t*>(vigil.input_ptr(0));
    uint8_t expected_last = static_cast<uint8_t>(0x42 + NUM_OPS - 2);
    for (uint32_t b = 0; b < 4096; b++) {
        assert(in_last[b] == expected_last);
    }

    assert(vigil.compiled_iterations() == 1);

    std::printf("  test_dispatch_data_flow: PASSED\n");
}

// ── Test 5: Pool bounds + activation wiring ────────────────────────
//
// Verifies that CrucibleContext activation through dispatch_op()
// correctly sets up the PoolAllocator — all output pointers are
// within pool bounds, and the pool has the expected configuration.
static void test_dispatch_pool_bounds() {
    Vigil vigil;

    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);

    vigil.flush();
    wait_mode_compiled(vigil);
    activate_via_dispatch(vigil, 3);

    // Verify pool is initialized and all pointers are valid.
    auto& pool = vigil.context().pool();
    assert(pool.is_initialized());
    assert(pool.pool_bytes() > 0);

    auto* pool_base = static_cast<uint8_t*>(pool.pool_base());
    uint64_t pool_bytes = pool.pool_bytes();

    // Replay and verify every output pointer is within pool bounds.
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(3, i);
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);

        auto* p = static_cast<uint8_t*>(vigil.output_ptr(0));
        assert(p >= pool_base);
        assert(p + 4096 <= pool_base + pool_bytes);
    }

    assert(vigil.compiled_iterations() == 1);
    assert(pool.num_external() == 0); // our test chain has no externals

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
