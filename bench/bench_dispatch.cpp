// Vigil::dispatch_op() and related hot-path benchmark suite.
//
// Measures the full per-op pipeline:
//   • Sub-component baselines — TraceRing / MetaLog / PoolAllocator primitives
//   • Isolated ReplayEngine + CrucibleContext (no Vigil wrapper)
//   • record_op (MetaLog + TraceRing) on its own
//   • dispatch_op in every mode: RECORDING, COMPILED steady, COMPILED + output,
//     COMPILED MATCH-only, full 8-op iteration including auto-reset
//   • Divergence cold-path (end-to-end: fresh setup + divergent dispatch)
//   • RegionCache::find_alternate
//
// Target latencies (x86-64, GCC 16 -O3):
//   COMPILED path:  ~2-5 ns/op
//   RECORDING path: ~15 ns/op
//   Alignment:      ~20 ns/op (cold, amortized across K=5 ops)
//   Divergence:     ~50-200 ns (pure dispatch); end-to-end dominated by
//                   setup_compiled_vigil which is milliseconds.

#include "bench_harness.h"

#include <crucible/Effects.h>
#include <crucible/Vigil.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace crucible;

namespace {

// ── Constants ──────────────────────────────────────────────────────

constexpr uint32_t NUM_OPS = 8;
constexpr uint32_t K = Vigil::ALIGNMENT_K;  // 5

constexpr SchemaHash SCHEMA[NUM_OPS] = {
    SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102}, SchemaHash{0x103},
    SchemaHash{0x104}, SchemaHash{0x105}, SchemaHash{0x106}, SchemaHash{0x107},
};
constexpr ShapeHash SHAPE[NUM_OPS] = {
    ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
    ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207},
};

// ── Helpers ────────────────────────────────────────────────────────

void* fake_ptr(uint32_t iter, uint32_t op) noexcept {
    return reinterpret_cast<void*>(
        static_cast<uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

TensorMeta make_meta(void* data_ptr) noexcept {
    TensorMeta m{};
    m.ndim        = 1;
    m.sizes[0]    = 1024;
    m.strides[0]  = 1;
    m.dtype       = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx  = 0;
    m.layout      = Layout::Strided;
    m.data_ptr    = data_ptr;
    return m;
}

struct OpData {
    TraceRing::Entry entry{};
    TensorMeta       metas[2]{};
    uint16_t         n_metas = 0;
};

OpData make_op(uint32_t iter, uint32_t op_idx) noexcept {
    OpData d;
    d.entry.schema_hash = SCHEMA[op_idx];
    d.entry.shape_hash  = SHAPE[op_idx];
    d.entry.num_inputs  = (op_idx == 0) ? 0 : 1;
    d.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0)
        d.metas[idx++] = make_meta(fake_ptr(iter, op_idx - 1));
    d.metas[idx++] = make_meta(fake_ptr(iter, op_idx));
    d.n_metas = d.entry.num_inputs + d.entry.num_outputs;
    return d;
}

void feed_record(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(d.entry, d.metas, d.n_metas);
    }
}

void feed_trigger(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(iter, i);
        (void)vigil.record_op(d.entry, d.metas, d.n_metas);
    }
}

void wait_mode_compiled(Vigil& vigil) {
    uint64_t spins = 0;
    while (!vigil.is_compiled()) {
        assert(++spins < 100'000'000 && "Vigil did not reach COMPILED mode");
        CRUCIBLE_SPIN_PAUSE;
    }
}

void align_and_activate(Vigil& vigil, uint32_t iter) {
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(iter, i);
        [[maybe_unused]] auto r =
            vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::RECORD);
    }
    assert(vigil.context().is_compiled());

    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(iter, i);
        [[maybe_unused]] auto r =
            vigil.dispatch_op(d.entry, d.metas, d.n_metas);
        assert(r.action == DispatchResult::Action::COMPILED);
    }
}

// Build a Vigil in COMPILED mode ready for benchmarking. On return the
// engine is at position 0 and the next dispatch_op is the first MATCH.
void setup_compiled_vigil(Vigil& vigil) {
    feed_record(vigil, 0);
    feed_record(vigil, 1);
    feed_trigger(vigil, 2);
    vigil.flush();
    wait_mode_compiled(vigil);
    align_and_activate(vigil, 3);
}

// ── Reusable BenchRegion for isolated engine/context/region-cache runs ──
//
// Build a full RegionNode with valid TensorMetas, slot IDs, and a
// MemoryPlan. compute_content_hash reads input_metas/output_metas, so
// we allocate those too — even though the pointers we write through
// fake_ptr don't point at real storage.
struct BenchRegion {
    fx::Test    test;
    Arena       arena{1 << 16};
    RegionNode* region = nullptr;
    MemoryPlan* plan   = nullptr;

    void build(uint32_t n_ops,
               const SchemaHash* schemas,
               const ShapeHash*  shapes) {
        auto* ops = arena.alloc_array<TraceEntry>(test.alloc, n_ops);
        for (uint32_t i = 0; i < n_ops; i++) {
            std::memset(&ops[i], 0, sizeof(TraceEntry));
            ops[i].schema_hash = schemas[i];
            ops[i].shape_hash  = shapes[i];
            ops[i].num_inputs  = (i == 0) ? 0 : 1;
            ops[i].num_outputs = 1;

            if (i > 0) {
                ops[i].input_metas =
                    arena.alloc_array<TensorMeta>(test.alloc, 1);
                std::memset(ops[i].input_metas, 0, sizeof(TensorMeta));
                ops[i].input_metas[0].ndim       = 1;
                ops[i].input_metas[0].sizes[0]   = 1024;
                ops[i].input_metas[0].strides[0] = 1;
                ops[i].input_metas[0].dtype      = ScalarType::Float;
            }
            ops[i].output_metas =
                arena.alloc_array<TensorMeta>(test.alloc, 1);
            std::memset(ops[i].output_metas, 0, sizeof(TensorMeta));
            ops[i].output_metas[0].ndim       = 1;
            ops[i].output_metas[0].sizes[0]   = 1024;
            ops[i].output_metas[0].strides[0] = 1;
            ops[i].output_metas[0].dtype      = ScalarType::Float;

            ops[i].output_slot_ids =
                arena.alloc_array<SlotId>(test.alloc, 1);
            ops[i].output_slot_ids[0] = SlotId{i};
            if (i > 0) {
                ops[i].input_slot_ids =
                    arena.alloc_array<SlotId>(test.alloc, 1);
                ops[i].input_slot_ids[0] = SlotId{i - 1};
            }
        }

        region = make_region(test.alloc, arena, ops, n_ops);

        auto* slots =
            arena.alloc_array<TensorSlot>(test.alloc, n_ops);
        for (uint32_t i = 0; i < n_ops; i++) {
            slots[i].offset_bytes = i * 4096;
            slots[i].nbytes       = 4096;
            slots[i].slot_id      = SlotId{i};
            slots[i].birth_op     = OpIndex{i};
            slots[i].death_op     = OpIndex{i < n_ops - 1 ? i + 1 : i};
            slots[i].dtype        = ScalarType::Float;
            slots[i].is_external  = false;
        }

        plan = arena.alloc_obj<MemoryPlan>(test.alloc);
        std::memset(plan, 0, sizeof(MemoryPlan));
        plan->slots        = slots;
        plan->num_slots    = n_ops;
        plan->num_external = 0;
        plan->pool_bytes   = n_ops * 4096;
        region->plan       = plan;
    }
};

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();
    const bool json = bench::env_json();

    std::printf("=== dispatch ===\n");
    std::printf("  sizeof(Vigil)            : %zu B\n",  sizeof(Vigil));
    std::printf("  sizeof(CrucibleContext)  : %zu B\n",  sizeof(CrucibleContext));
    std::printf("  sizeof(ReplayEngine)     : %zu B\n",  sizeof(ReplayEngine));
    std::printf("  sizeof(TraceRing::Entry) : %zu B\n",  sizeof(TraceRing::Entry));
    std::printf("  sizeof(TensorMeta)       : %zu B\n\n",sizeof(TensorMeta));

    std::vector<bench::Report> reports;
    reports.reserve(20);

    // ── Sub-component baselines ────────────────────────────────────
    std::printf("--- sub-component baselines ---\n");

    reports.push_back([&]{
        TraceRing ring;
        ring.reset();
        TraceRing::Entry entry{};
        entry.schema_hash = SchemaHash{0x100};
        entry.shape_hash  = ShapeHash{0x200};
        entry.num_inputs  = 1;
        entry.num_outputs = 1;
        auto r = bench::run("TraceRing::try_append (raw, reset-on-full)", [&]{
            if (!ring.try_append(entry)) [[unlikely]] ring.reset();
            bench::do_not_optimize(&ring);
        });
        return r;
    }());

    reports.push_back([&]{
        MetaLog meta_log;
        meta_log.reset();
        TensorMeta metas[2]{
            make_meta(fake_ptr(0, 0)),
            make_meta(fake_ptr(0, 1)),
        };
        return bench::run("MetaLog::try_append (2 metas)", [&]{
            bench::do_not_optimize(&meta_log);
            bench::do_not_optimize(meta_log.try_append(metas, 2));
        });
    }());

    reports.push_back([&]{
        fx::Test test;
        Arena arena{1 << 16};
        constexpr uint32_t NSLOTS = 16;
        auto* slots = arena.alloc_array<TensorSlot>(test.alloc, NSLOTS);
        for (uint32_t i = 0; i < NSLOTS; i++) {
            slots[i].offset_bytes = i * 256;
            slots[i].nbytes       = 256;
            slots[i].slot_id      = SlotId{i};
            slots[i].birth_op     = OpIndex{0};
            slots[i].death_op     = OpIndex{0};
            slots[i].dtype        = ScalarType::Float;
            slots[i].is_external  = false;
        }
        auto* plan = arena.alloc_obj<MemoryPlan>(test.alloc);
        plan->slots        = slots;
        plan->num_slots    = NSLOTS;
        plan->num_external = 0;
        plan->pool_bytes   = NSLOTS * 256;

        PoolAllocator pool;
        pool.init(plan);

        uint32_t slot_idx = 0;
        auto pv = pool.mint_initialized_view();
        auto r = bench::run("PoolAllocator::slot_ptr (cyclic, N=16)", [&]{
            bench::do_not_optimize(pool.slot_ptr(SlotId{slot_idx}, pv));
            slot_idx = (slot_idx + 1) & (NSLOTS - 1);
        });
        pool.destroy();
        return r;
    }());

    reports.push_back([&]{
        Vigil vigil;
        setup_compiled_vigil(vigil);
        return bench::run("Vigil::is_compiled() [true]", [&]{
            bench::do_not_optimize(&vigil);
            bench::do_not_optimize(vigil.context().is_compiled());
        });
    }());

    // ── Isolated engine / context ──────────────────────────────────
    std::printf("\n--- isolated engine/context ---\n");

    reports.push_back([&]{
        BenchRegion br;
        br.build(NUM_OPS, SCHEMA, SHAPE);
        PoolAllocator pool;
        pool.init(br.plan);
        ReplayEngine engine;
        engine.init(br.region, &pool);
        auto av = engine.mint_active_view();

        uint32_t op_idx = 0;
        auto r = bench::run("ReplayEngine::advance (cyclic, 8 ops)", [&]{
            bench::do_not_optimize(&engine);
            auto s = engine.advance(SCHEMA[op_idx], SHAPE[op_idx], av);
            bench::do_not_optimize(s);
            op_idx++;
            if (op_idx >= NUM_OPS) {
                op_idx = 0;
                engine.reset(av);
            }
        });
        pool.destroy();
        return r;
    }());

    reports.push_back([&]{
        BenchRegion br;
        br.build(NUM_OPS, SCHEMA, SHAPE);
        CrucibleContext ctx;
        (void)ctx.activate(br.region);

        uint32_t op_idx = 0;
        auto cv = ctx.mint_compiled_view();
        auto r = bench::run("CrucibleContext::advance (cyclic, 8 ops)", [&]{
            bench::do_not_optimize(&ctx);
            auto s = ctx.advance(SCHEMA[op_idx], SHAPE[op_idx], cv);
            bench::do_not_optimize(s);
            op_idx = (op_idx + 1) % NUM_OPS;
        });
        ctx.deactivate();
        return r;
    }());

    reports.push_back([&]{
        constexpr uint32_t BIG = 32;
        SchemaHash big_sch[BIG];
        ShapeHash  big_shp[BIG];
        for (uint32_t i = 0; i < BIG; i++) {
            big_sch[i] = SchemaHash{0x1000 + i};
            big_shp[i] = ShapeHash{0x2000 + i};
        }
        BenchRegion br;
        br.build(BIG, big_sch, big_shp);
        CrucibleContext ctx;
        (void)ctx.activate(br.region);

        uint32_t op_idx = 0;
        auto cv = ctx.mint_compiled_view();
        auto r = bench::run("CrucibleContext::advance (cyclic, 32 ops)", [&]{
            bench::do_not_optimize(&ctx);
            auto s = ctx.advance(big_sch[op_idx], big_shp[op_idx], cv);
            bench::do_not_optimize(s);
            op_idx = (op_idx + 1) % BIG;
        });
        ctx.deactivate();
        return r;
    }());

    // ── record_op path ─────────────────────────────────────────────
    std::printf("\n--- record_op path ---\n");

    reports.push_back([&]{
        Vigil vigil;
        TraceRing::Entry e{};
        e.schema_hash = SchemaHash{0x999};
        e.shape_hash  = ShapeHash{0xAAA};
        e.num_inputs  = 0;
        e.num_outputs = 0;
        return bench::run("Vigil::record_op (0 metas, profiler hook)", [&]{
            bench::do_not_optimize(vigil.record_op(e, nullptr, 0));
        });
    }());

    reports.push_back([&]{
        Vigil vigil;
        auto d = make_op(0, 1);
        return bench::run("Vigil::record_op (2 metas)", [&]{
            bench::do_not_optimize(
                vigil.record_op(d.entry, d.metas, d.n_metas));
        });
    }());

    // ── dispatch_op RECORDING ──────────────────────────────────────
    std::printf("\n--- dispatch_op RECORDING ---\n");

    reports.push_back([&]{
        Vigil vigil;
        auto d = make_op(0, 1);
        return bench::run("dispatch_op [RECORDING, no pending]", [&]{
            bench::do_not_optimize(
                vigil.dispatch_op(d.entry, d.metas, d.n_metas));
        });
    }());

    // ── dispatch_op COMPILED ───────────────────────────────────────
    std::printf("\n--- dispatch_op COMPILED ---\n");

    reports.push_back([&]{
        Vigil vigil;
        setup_compiled_vigil(vigil);
        OpData ops[NUM_OPS];
        for (uint32_t i = 0; i < NUM_OPS; i++) ops[i] = make_op(10, i);

        uint32_t op_idx = 0;
        return bench::run("dispatch_op [COMPILED, cyclic]", [&]{
            auto& d = ops[op_idx];
            auto r  = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
            bench::do_not_optimize(r);
            op_idx = (op_idx + 1) % NUM_OPS;
        });
    }());

    reports.push_back([&]{
        Vigil vigil;
        setup_compiled_vigil(vigil);
        OpData ops[NUM_OPS];
        for (uint32_t i = 0; i < NUM_OPS; i++) ops[i] = make_op(10, i);

        uint32_t op_idx = 0;
        return bench::run("dispatch_op [COMPILED + output_ptr]", [&]{
            auto& d = ops[op_idx];
            auto r  = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
            if (r.action == DispatchResult::Action::COMPILED)
                bench::do_not_optimize(vigil.output_ptr(0));
            bench::do_not_optimize(r);
            op_idx = (op_idx + 1) % NUM_OPS;
        });
    }());

    reports.push_back([&]{
        Vigil vigil;
        setup_compiled_vigil(vigil);
        OpData ops[NUM_OPS];
        for (uint32_t i = 0; i < NUM_OPS; i++) ops[i] = make_op(10, i);

        uint32_t op_idx = 0;
        return bench::run("dispatch_op [COMPILED, MATCH only]", [&]{
            auto& d = ops[op_idx];
            auto r  = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
            bench::do_not_optimize(r);
            op_idx++;
            if (op_idx >= NUM_OPS - 1) {
                auto& last = ops[NUM_OPS - 1];
                auto rl = vigil.dispatch_op(last.entry,
                                            last.metas, last.n_metas);
                bench::do_not_optimize(rl);
                op_idx = 0;
            }
        });
    }());

    reports.push_back([&]{
        Vigil vigil;
        setup_compiled_vigil(vigil);
        OpData ops[NUM_OPS];
        for (uint32_t i = 0; i < NUM_OPS; i++) ops[i] = make_op(10, i);

        return bench::run("dispatch_op [full 8-op iteration]", [&]{
            for (uint32_t i = 0; i < NUM_OPS; i++) {
                auto& d = ops[i];
                auto r  = vigil.dispatch_op(d.entry, d.metas, d.n_metas);
                bench::do_not_optimize(r);
            }
        });
    }());

    // ── Cold path ──────────────────────────────────────────────────
    std::printf("\n--- cold paths ---\n");

    // Divergence is single-shot per sample: each call destroys compiled
    // state. The harness auto-batch won't help; a fresh compiled Vigil
    // is built inside the body every sample. The Report thus measures
    // *end-to-end* cost (setup + advance + divergence), not divergence
    // alone — label makes that explicit.
    reports.push_back([&]{
        bench::Run r{"divergence [end-to-end: setup + dispatch]"};
        if (const int c = bench::env_core(); c >= 0) (void)r.core(c);
        return r.samples(100).warmup(2).batch(1).measure([&]{
            Vigil vigil;
            setup_compiled_vigil(vigil);
            for (uint32_t i = 0; i < 3; i++) {
                auto d = make_op(10, i);
                [[maybe_unused]] auto rr =
                    vigil.dispatch_op(d.entry, d.metas, d.n_metas);
                assert(rr.action == DispatchResult::Action::COMPILED);
            }
            TraceRing::Entry bad{};
            bad.schema_hash = SchemaHash{0xBAD};
            bad.shape_hash  = SHAPE[3];
            bad.num_inputs  = 1;
            bad.num_outputs = 1;
            TensorMeta bad_metas[2]{
                make_meta(fake_ptr(10, 2)),
                make_meta(fake_ptr(10, 3)),
            };
            auto rr = vigil.dispatch_op(bad, bad_metas, 2);
            bench::do_not_optimize(rr);
            assert(rr.action == DispatchResult::Action::RECORD);
            assert(rr.status == ReplayStatus::DIVERGED);
        });
    }());

    reports.push_back([&]{
        fx::Test test;
        Arena arena{1 << 16};
        constexpr uint32_t NR = 4;
        const RegionNode* regions[NR];
        for (uint32_t r = 0; r < NR; r++) {
            auto* ops = arena.alloc_array<TraceEntry>(test.alloc, NUM_OPS);
            for (uint32_t i = 0; i < NUM_OPS; i++) {
                std::memset(&ops[i], 0, sizeof(TraceEntry));
                ops[i].schema_hash = SCHEMA[i];
                ops[i].shape_hash  = ShapeHash{SHAPE[i].raw() + r * 0x100};
            }
            auto* region = make_region(test.alloc, arena, ops, NUM_OPS);
            auto* plan   = arena.alloc_obj<MemoryPlan>(test.alloc);
            plan->slots        = nullptr;
            plan->num_slots    = 0;
            plan->pool_bytes   = 0;
            region->plan       = plan;
            regions[r] = region;
        }
        RegionCache cache;
        for (uint32_t r = 0; r < NR; r++) cache.insert(regions[r]);

        SchemaHash tgt_schema = SCHEMA[3];
        ShapeHash  tgt_shape{SHAPE[3].raw() + 2 * 0x100};
        return bench::run("RegionCache::find_alternate (4 entries)", [&]{
            auto* found =
                cache.find_alternate(3, tgt_schema, tgt_shape, regions[0]);
            bench::do_not_optimize(found);
        });
    }());

    bench::emit_reports_text(reports);

    // ── compare: dispatch_op RECORDING vs record_op (overhead) ─────
    //
    // dispatch_op [RECORDING] does everything record_op does, plus:
    //   - ctx_.is_compiled() check (false)
    //   - pending_region_.load(acquire) + pending_activation_ branch
    //   - return DispatchResult instead of bool
    // The Δ is the dispatch wrapper's overhead on the RECORDING path.
    const size_t ri_rec = 8;    // Vigil::record_op (2 metas)
    const size_t ri_dis = 9;    // dispatch_op [RECORDING, no pending]
    std::printf("\n=== compare — dispatch_op vs record_op (RECORDING) ===\n  ");
    bench::compare(reports[ri_rec], reports[ri_dis]).print_text(stdout);

    // COMPILED cyclic vs full-iteration (per-op amortized):
    // the full-iteration Report divides its total by 8 (roughly).
    std::printf("\n=== compare — COMPILED cyclic vs full-iteration ===\n  ");
    bench::compare(reports[10], reports[13]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
