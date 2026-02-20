// Comprehensive benchmark for Vigil::dispatch_op() — the full per-op pipeline.
//
// Measures:
//   1. RECORDING mode steady state (no pending region)
//   2. COMPILED mode steady state (all ops match, cyclic)
//   3. COMPILED mode full iteration (8 ops -> COMPLETE -> auto-reset)
//   4. COMPILED mode with output_ptr() access per op
//   5. RECORDING->COMPILED transition cost (alignment phase)
//   6. COMPILED->RECORDING divergence cost
//   7. Region cache switch cost (mid-iteration)
//   8. Comparison: record_op alone vs dispatch_op(RECORDING)
//   9. Comparison: raw TraceRing::try_append vs full record_op
//  10. Sub-component isolation: MetaLog::try_append, ReplayEngine::advance
//
// Target latencies (x86-64, Clang 22 -O3):
//   COMPILED path:  ~2-5ns/op
//   RECORDING path: ~15ns/op
//   Alignment:      ~20ns/op (cold, amortized across K=5 ops)
//   Divergence:     ~100ns (cold, one-time cost)

#include "bench_harness.h"

#include <crucible/Vigil.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

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

static void feed_record(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(d.entry, d.metas, d.n_metas);
    }
}

static void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(d.entry, d.metas, d.n_metas);
    }
}

static void wait_mode_compiled(Vigil& vigil, uint32_t timeout_ms = 5000) {
    [[maybe_unused]] auto deadline = std::chrono::steady_clock::now() +
                                     std::chrono::milliseconds(timeout_ms);
    while (!vigil.is_compiled()) {
        assert(std::chrono::steady_clock::now() < deadline
               && "Vigil did not reach COMPILED mode in time");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

static void align_and_activate(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(iter, i);
        [[maybe_unused]] auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::RECORD);
    }
    assert(vigil.context().is_compiled());

    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        [[maybe_unused]] auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
}

// Build a Vigil in COMPILED mode ready for benchmarking.
// Returns with the engine at position 0, ready for dispatch_op calls.
static void setup_compiled_vigil(Vigil& vigil) {
    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);
    vigil.flush();
    wait_mode_compiled(vigil);
    align_and_activate(vigil, 3);
}

// ── Benchmark 1: Raw TraceRing::try_append (baseline) ────────────

static void bench_tracering_raw() {
    TraceRing ring;
    ring.reset();

    TraceRing::Entry entry{};
    entry.schema_hash = SchemaHash{0x100};
    entry.shape_hash = ShapeHash{0x200};
    entry.num_inputs = 1;
    entry.num_outputs = 1;

    // Pre-drain: keep tail moving so ring doesn't fill.
    // We'll manually advance tail between rounds.
    BENCH("TraceRing::try_append (raw)", 1'000'000, {
        bench::DoNotOptimize(ring.try_append(entry));
    });

    // Reset for next bench — ring may be full.
    ring.reset();
}

// ── Benchmark 2: MetaLog::try_append (baseline) ─────────────────

static void bench_metalog_raw() {
    MetaLog meta_log;
    meta_log.reset();

    TensorMeta metas[2]{};
    metas[0] = make_meta(fake_ptr(0, 0));
    metas[1] = make_meta(fake_ptr(0, 1));

    BENCH("MetaLog::try_append (2 metas)", 1'000'000, {
        bench::DoNotOptimize(&meta_log);
        bench::DoNotOptimize(meta_log.try_append(metas, 2));
    });
}

// ── Benchmark 3: record_op (MetaLog + TraceRing) ─────────────────

static void bench_record_op() {
    Vigil vigil;

    auto d = make_op(0, 1);  // 1 input, 1 output -> 2 metas

    BENCH("Vigil::record_op (2 metas)", 1'000'000, {
        bench::DoNotOptimize(
            vigil.record_op(d.entry, d.metas, d.n_metas));
    });
}

// ── Benchmark 4: dispatch_op RECORDING (no pending) ──────────────

static void bench_dispatch_recording() {
    Vigil vigil;

    auto d = make_op(0, 1);  // 1 input, 1 output

    BENCH("dispatch_op [RECORDING, no pending]", 1'000'000, {
        bench::DoNotOptimize(
            vigil.dispatch_op(d.entry, d.metas, d.n_metas));
    });
}

// ── Helper: build a complete region with N ops for isolated benchmarks ──
//
// Allocates TraceEntries with valid input_metas/output_metas (required by
// compute_content_hash), slot IDs, and a MemoryPlan.  Each op has 1 output;
// ops 1+ have 1 input from the previous op's output.
struct BenchRegion {
    Arena arena{1 << 16};
    RegionNode* region = nullptr;
    MemoryPlan* plan = nullptr;

    void build(uint32_t n_ops,
               const SchemaHash* schemas,
               const ShapeHash* shapes) {
        auto* ops = arena.alloc_array<TraceEntry>(n_ops);
        for (uint32_t i = 0; i < n_ops; i++) {
            std::memset(&ops[i], 0, sizeof(TraceEntry));
            ops[i].schema_hash = schemas[i];
            ops[i].shape_hash = shapes[i];
            ops[i].num_inputs = (i == 0) ? 0 : 1;
            ops[i].num_outputs = 1;

            // Allocate valid TensorMeta arrays (compute_content_hash reads these).
            if (i > 0) {
                ops[i].input_metas = arena.alloc_array<TensorMeta>(1);
                std::memset(ops[i].input_metas, 0, sizeof(TensorMeta));
                ops[i].input_metas[0].ndim = 1;
                ops[i].input_metas[0].sizes[0] = 1024;
                ops[i].input_metas[0].strides[0] = 1;
                ops[i].input_metas[0].dtype = ScalarType::Float;
            }
            ops[i].output_metas = arena.alloc_array<TensorMeta>(1);
            std::memset(ops[i].output_metas, 0, sizeof(TensorMeta));
            ops[i].output_metas[0].ndim = 1;
            ops[i].output_metas[0].sizes[0] = 1024;
            ops[i].output_metas[0].strides[0] = 1;
            ops[i].output_metas[0].dtype = ScalarType::Float;

            // Slot IDs for output_ptr/input_ptr to work.
            ops[i].output_slot_ids = arena.alloc_array<SlotId>(1);
            ops[i].output_slot_ids[0] = SlotId{i};
            if (i > 0) {
                ops[i].input_slot_ids = arena.alloc_array<SlotId>(1);
                ops[i].input_slot_ids[0] = SlotId{i - 1};
            }
        }

        region = make_region(arena, ops, n_ops);

        // Build MemoryPlan: one 4096B slot per op.
        auto* slots = arena.alloc_array<TensorSlot>(n_ops);
        for (uint32_t i = 0; i < n_ops; i++) {
            slots[i].offset_bytes = i * 4096;
            slots[i].nbytes = 4096;
            slots[i].slot_id = SlotId{i};
            slots[i].birth_op = OpIndex{i};
            slots[i].death_op = OpIndex{i < n_ops - 1 ? i + 1 : i};
            slots[i].dtype = ScalarType::Float;
            slots[i].is_external = false;
        }

        plan = arena.alloc_obj<MemoryPlan>();
        std::memset(plan, 0, sizeof(MemoryPlan));
        plan->slots = slots;
        plan->num_slots = n_ops;
        plan->num_external = 0;
        plan->pool_bytes = n_ops * 4096;
        region->plan = plan;
    }
};

// ── Benchmark 5: ReplayEngine::advance (isolated) ────────────────

static void bench_replay_engine() {
    BenchRegion br;
    br.build(NUM_OPS, SCHEMA, SHAPE);

    PoolAllocator pool;
    pool.init(br.plan);

    ReplayEngine engine;
    engine.init(br.region, &pool);

    // Benchmark: cycle through all ops, reset on complete.
    // DoNotOptimize(&engine) forces the compiler to assume engine's state
    // may have been modified by "external" code between iterations.
    uint32_t op_idx = 0;
    BENCH("ReplayEngine::advance (cyclic)", 10'000'000, {
        bench::DoNotOptimize(&engine);
        auto s = engine.advance(SCHEMA[op_idx], SHAPE[op_idx]);
        bench::DoNotOptimize(s);
        op_idx++;
        if (op_idx >= NUM_OPS) {
            op_idx = 0;
            engine.reset();
        }
    });

    pool.destroy();
}

// ── Benchmark 6: CrucibleContext::advance (isolated) ─────────────

static void bench_crucible_context() {
    BenchRegion br;
    br.build(NUM_OPS, SCHEMA, SHAPE);

    CrucibleContext ctx;
    assert(ctx.activate(br.region));

    // Cyclic advance: auto-resets on COMPLETE.
    // Force the compiler to treat ctx as possibly-modified between iterations.
    // Without this, the compiler sees ctx is a local with no aliasing and
    // optimizes the advance() chain to near-zero by hoisting loads.
    uint32_t op_idx = 0;
    BENCH("CrucibleContext::advance (cyclic)", 10'000'000, {
        bench::DoNotOptimize(&ctx);
        auto s = ctx.advance(SCHEMA[op_idx], SHAPE[op_idx]);
        bench::DoNotOptimize(s);
        op_idx = (op_idx + 1) % NUM_OPS;
    });

    ctx.deactivate();
}

// ── Benchmark 7: dispatch_op COMPILED steady-state ───────────────

static void bench_dispatch_compiled() {
    Vigil vigil;
    setup_compiled_vigil(vigil);

    // Pre-build OpData array to avoid construction in hot loop.
    OpData ops[NUM_OPS];
    for (uint32_t i = 0; i < NUM_OPS; i++)
        ops[i] = make_op(10, i);

    uint32_t op_idx = 0;
    BENCH("dispatch_op [COMPILED, cyclic]", 10'000'000, {
        auto& d = ops[op_idx];
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        bench::DoNotOptimize(r);
        op_idx = (op_idx + 1) % NUM_OPS;
    });
}

// ── Benchmark 8: dispatch_op COMPILED + output_ptr access ────────

static void bench_dispatch_compiled_with_output() {
    Vigil vigil;
    setup_compiled_vigil(vigil);

    OpData ops[NUM_OPS];
    for (uint32_t i = 0; i < NUM_OPS; i++)
        ops[i] = make_op(10, i);

    uint32_t op_idx = 0;
    BENCH("dispatch_op [COMPILED + output_ptr]", 10'000'000, {
        auto& d = ops[op_idx];
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        if (r.action == DispatchResult::Action::COMPILED)
            bench::DoNotOptimize(vigil.output_ptr(0));
        bench::DoNotOptimize(r);
        op_idx = (op_idx + 1) % NUM_OPS;
    });
}

// ── Benchmark 9: dispatch_op full iteration (8 ops) ──────────────

static void bench_dispatch_full_iteration() {
    Vigil vigil;
    setup_compiled_vigil(vigil);

    OpData ops[NUM_OPS];
    for (uint32_t i = 0; i < NUM_OPS; i++)
        ops[i] = make_op(10, i);

    // Amortized per-op cost: one full 8-op iteration.
    BENCH("dispatch_op [full 8-op iteration]", 1'000'000, {
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            auto& d = ops[i];
            auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
            bench::DoNotOptimize(r);
        }
    });
}

// ── Benchmark 10: dispatch_op COMPILED — MATCH only (no COMPLETE) ─

static void bench_dispatch_compiled_match_only() {
    Vigil vigil;
    setup_compiled_vigil(vigil);

    // Only benchmark ops 0..6 (MATCH), then manually reset to avoid COMPLETE path.
    // This isolates the MATCH-only hot path without the is_complete() check.
    OpData ops[NUM_OPS];
    for (uint32_t i = 0; i < NUM_OPS; i++)
        ops[i] = make_op(10, i);

    // Run 7 ops (0..6), all MATCH. Then skip op 7 (COMPLETE) and instead
    // do a full iteration to reset, then measure again.
    // Simpler: just measure op 0 repeatedly after resetting the context.
    // Actually: we need sequential ops. Measure ops 0..6 per round.

    uint32_t op_idx = 0;
    BENCH("dispatch_op [COMPILED, MATCH only]", 10'000'000, {
        auto& d = ops[op_idx];
        auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        bench::DoNotOptimize(r);
        op_idx++;
        if (op_idx >= NUM_OPS - 1) {
            // Complete the iteration to reset, then start over.
            auto& last = ops[NUM_OPS - 1];
            auto rl = vigil.dispatch_op(last.entry, last.metas, last.n_metas);
            bench::DoNotOptimize(rl);
            op_idx = 0;
        }
    });
}

// ── Benchmark 11: dispatch_op RECORDING vs record_op comparison ──

static void bench_dispatch_vs_record() {
    // dispatch_op in RECORDING mode does:
    //   1. ctx_.is_compiled() check (false)
    //   2. pending_region_.load(acquire)
    //   3. pending_activation_ check (false)
    //   4. record_op()
    //   5. pending_activation_ check again (false)
    //   6. return DispatchResult
    //
    // record_op does:
    //   1. MetaLog::try_append
    //   2. TraceRing::try_append
    //   3. return bool
    //
    // The difference is the overhead of the dispatch_op wrapper.

    Vigil vigil_dispatch;
    Vigil vigil_record;

    auto d = make_op(0, 1);

    BENCH("record_op alone (2 metas)", 1'000'000, {
        bench::DoNotOptimize(
            vigil_record.record_op(d.entry, d.metas, d.n_metas));
    });

    BENCH("dispatch_op [RECORDING] (2 metas)", 1'000'000, {
        bench::DoNotOptimize(
            vigil_dispatch.dispatch_op(d.entry, d.metas, d.n_metas));
    });
}

// ── Benchmark 12: record_op with 0 metas (zero-tensor op) ────────

static void bench_record_op_zero_metas() {
    Vigil vigil;

    TraceRing::Entry e{};
    e.schema_hash = SchemaHash{0x999};
    e.shape_hash = ShapeHash{0xAAA};
    e.num_inputs = 0;
    e.num_outputs = 0;

    BENCH("record_op (0 metas, profiler hook)", 1'000'000, {
        bench::DoNotOptimize(vigil.record_op(e, nullptr, 0));
    });
}

// ── Benchmark 13: dispatch_op divergence cost ────────────────────

static void bench_dispatch_divergence() {
    // Measures the cost of a single divergence event.
    // This is a cold path — we expect ~50-200ns.
    //
    // Setup: activate compiled mode, then send divergent ops.
    // After each divergence, re-feed + re-align to set up again.
    // We measure the divergence op itself.

    // Use manual rdtsc for single-shot measurements since the BENCH
    // macro assumes uniform iterations.
    static double ratio = bench::tsc_ns_ratio();

    constexpr uint32_t SAMPLES = 100;
    double divergence_ns[SAMPLES];

    for (uint32_t s = 0; s < SAMPLES; s++) {
        Vigil vigil;
        setup_compiled_vigil(vigil);

        // Advance to op 3 in compiled mode.
        for (uint32_t i = 0; i < 3; i++) {
            auto d = make_op(10, i);
            [[maybe_unused]] auto r = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
            assert(r.action == DispatchResult::Action::COMPILED);
        }

        // Divergent op: wrong schema.
        TraceRing::Entry bad{};
        bad.schema_hash = SchemaHash{0xBAD};
        bad.shape_hash = SHAPE[3];
        bad.num_inputs = 1;
        bad.num_outputs = 1;
        TensorMeta bad_metas[2]{};
        bad_metas[0] = make_meta(fake_ptr(10, 2));
        bad_metas[1] = make_meta(fake_ptr(10, 3));

        bench::ClobberMemory();
        uint64_t t0 = bench::rdtsc();
        auto r = vigil.dispatch_op(bad, bad_metas, 2);
        uint64_t t1 = bench::rdtsc();
        bench::DoNotOptimize(r);

        assert(r.action == DispatchResult::Action::RECORD);
        assert(r.status == ReplayStatus::DIVERGED);

        divergence_ns[s] = static_cast<double>(t1 - t0) * ratio;
    }

    // Sort and report percentiles.
    std::sort(divergence_ns, divergence_ns + SAMPLES);
    std::printf("  %-40s %6.1f ns     (p10=%5.1f  p50=%5.1f  p90=%6.1f)  [%u samples]\n",
                "dispatch_op [DIVERGENCE]",
                divergence_ns[SAMPLES / 2],
                divergence_ns[SAMPLES / 10],
                divergence_ns[SAMPLES / 2],
                divergence_ns[SAMPLES * 9 / 10],
                SAMPLES);
}

// ── Benchmark 14: RegionCache::find_alternate ────────────────────

static void bench_region_cache_lookup() {
    // Isolated RegionCache::find_alternate benchmark.
    Arena arena{1 << 16};

    // Build 4 dummy regions with different shapes.
    constexpr uint32_t NUM_REGIONS = 4;
    const RegionNode* regions[NUM_REGIONS];

    for (uint32_t r = 0; r < NUM_REGIONS; r++) {
        auto* ops = arena.alloc_array<TraceEntry>(NUM_OPS);
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            std::memset(&ops[i], 0, sizeof(TraceEntry));
            ops[i].schema_hash = SCHEMA[i];
            ops[i].shape_hash = ShapeHash{SHAPE[i].raw() + r * 0x100};
        }
        auto* region = make_region(arena, ops, NUM_OPS);

        // Add a plan so it's eligible.
        auto* plan = arena.alloc_obj<MemoryPlan>();
        plan->slots = nullptr;
        plan->num_slots = 0;
        plan->pool_bytes = 0;
        region->plan = plan;

        regions[r] = region;
    }

    RegionCache cache;
    for (uint32_t r = 0; r < NUM_REGIONS; r++)
        cache.insert(regions[r]);

    // Benchmark: search for op 3 matching region[2]'s shapes.
    SchemaHash target_schema = SCHEMA[3];
    ShapeHash target_shape{SHAPE[3].raw() + 2 * 0x100};

    BENCH("RegionCache::find_alternate (4 entries)", 10'000'000, {
        auto* found = cache.find_alternate(3, target_schema, target_shape, regions[0]);
        bench::DoNotOptimize(found);
    });
}

// ── Benchmark 15: is_compiled() check (branch prediction) ────────

static void bench_is_compiled_check() {
    Vigil vigil;
    setup_compiled_vigil(vigil);

    BENCH("Vigil::is_compiled() [true]", 100'000'000, {
        bench::DoNotOptimize(&vigil);
        bench::DoNotOptimize(vigil.context().is_compiled());
    });
}

// ── Benchmark 16: dispatch_op COMPILED single op (op 0, MATCH) ───

static void bench_dispatch_compiled_single_op() {
    // Measure the steady-state cost of a single MATCH in compiled mode.
    // Uses a larger region (32 ops) to avoid COMPLETE path amortization noise.

    constexpr uint32_t BIG_OPS = 32;

    SchemaHash big_schema[BIG_OPS];
    ShapeHash big_shape[BIG_OPS];
    for (uint32_t i = 0; i < BIG_OPS; i++) {
        big_schema[i] = SchemaHash{0x1000 + i};
        big_shape[i] = ShapeHash{0x2000 + i};
    }

    BenchRegion br;
    br.build(BIG_OPS, big_schema, big_shape);

    CrucibleContext ctx;
    assert(ctx.activate(br.region));

    uint32_t op_idx = 0;
    BENCH("CrucibleContext::advance (32-op cyclic)", 10'000'000, {
        bench::DoNotOptimize(&ctx);
        auto s = ctx.advance(big_schema[op_idx], big_shape[op_idx]);
        bench::DoNotOptimize(s);
        op_idx++;
        if (op_idx >= BIG_OPS) {
            op_idx = 0;
            // After COMPLETE, engine auto-resets on next advance.
        }
    });

    ctx.deactivate();
}

// ── Benchmark 17: PoolAllocator::slot_ptr (isolated) ─────────────

static void bench_pool_slot_ptr() {
    Arena arena{1 << 16};
    constexpr uint32_t NSLOTS = 16;

    auto* slots = arena.alloc_array<TensorSlot>(NSLOTS);
    for (uint32_t i = 0; i < NSLOTS; i++) {
        slots[i].offset_bytes = i * 256;
        slots[i].nbytes = 256;
        slots[i].slot_id = SlotId{i};
        slots[i].birth_op = OpIndex{0};
        slots[i].death_op = OpIndex{0};
        slots[i].dtype = ScalarType::Float;
        slots[i].is_external = false;
    }

    auto* plan = arena.alloc_obj<MemoryPlan>();
    plan->slots = slots;
    plan->num_slots = NSLOTS;
    plan->num_external = 0;
    plan->pool_bytes = NSLOTS * 256;

    PoolAllocator pool;
    pool.init(plan);

    uint32_t slot_idx = 0;
    BENCH("PoolAllocator::slot_ptr (cyclic)", 100'000'000, {
        bench::DoNotOptimize(pool.slot_ptr(SlotId{slot_idx}));
        slot_idx = (slot_idx + 1) & (NSLOTS - 1);
    });

    pool.destroy();
}

// ═════════════════════════════════════════════════════════════════════

int main() {
    std::printf("=== Crucible dispatch_op() Benchmark Suite ===\n\n");

    std::printf("--- Sub-component baselines ---\n");
    bench_tracering_raw();
    bench_metalog_raw();
    bench_pool_slot_ptr();
    bench_is_compiled_check();

    std::printf("\n--- Isolated engine/context ---\n");
    bench_replay_engine();
    bench_crucible_context();
    bench_dispatch_compiled_single_op();

    std::printf("\n--- record_op path ---\n");
    bench_record_op_zero_metas();
    bench_record_op();
    bench_dispatch_vs_record();

    std::printf("\n--- dispatch_op RECORDING ---\n");
    bench_dispatch_recording();

    std::printf("\n--- dispatch_op COMPILED ---\n");
    bench_dispatch_compiled();
    bench_dispatch_compiled_with_output();
    bench_dispatch_compiled_match_only();
    bench_dispatch_full_iteration();

    std::printf("\n--- Cold paths ---\n");
    bench_dispatch_divergence();
    bench_region_cache_lookup();

    std::printf("\n=== Done ===\n");
    return 0;
}
