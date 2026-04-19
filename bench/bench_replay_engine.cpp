// ReplayEngine + CrucibleContext hot-path benchmark.
//
// Four scenarios per region size (8 / 100 / 1000 ops):
//   1. advance()                           — steady-state matching
//   2. advance() + output_ptr(0)           — typical per-op cost
//   3. advance() + output_ptr + input_ptr  — full per-op cost
//   4. CrucibleContext advance path        — overhead vs raw engine
//
// Each "walk" is a full region traversal; the bench measures one walk
// per sample. Auto-batch runs it multiple times per timed region with
// engine.reset() at the top of each walk so state is repeatable.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <crucible/CrucibleContext.h>
#include <crucible/ReplayEngine.h>

#include "bench_harness.h"

using namespace crucible;

namespace {

// Synthetic region: N ops, one output + one input each. Slot layout:
// op[i] writes to slot i, reads from slot (i-1) mod N. Plan gives each
// slot 256 B of a dummy pool.
struct BenchRegion {
    TraceEntry* ops        = nullptr;
    SlotId*     out_slots  = nullptr;
    SlotId*     in_slots   = nullptr;
    TensorSlot* slots      = nullptr;
    MemoryPlan  plan{};
    RegionNode  region{};
    uint32_t    num_ops    = 0;

    explicit BenchRegion(uint32_t n) : num_ops{n} {
        ops       = static_cast<TraceEntry*>(std::calloc(n, sizeof(TraceEntry)));
        out_slots = static_cast<SlotId*>    (std::calloc(n, sizeof(SlotId)));
        in_slots  = static_cast<SlotId*>    (std::calloc(n, sizeof(SlotId)));
        slots     = static_cast<TensorSlot*>(std::calloc(n, sizeof(TensorSlot)));
        if (!ops || !out_slots || !in_slots || !slots) std::abort();

        for (uint32_t i = 0; i < n; i++) {
            out_slots[i] = SlotId{i};
            in_slots[i]  = SlotId{(i > 0) ? (i - 1) : (n - 1)};

            ops[i].schema_hash     = SchemaHash{1000 + i};
            ops[i].shape_hash      = ShapeHash{2000 + i};
            ops[i].num_outputs     = 1;
            ops[i].output_slot_ids = &out_slots[i];
            ops[i].num_inputs      = 1;
            ops[i].input_slot_ids  = &in_slots[i];
        }

        for (uint32_t i = 0; i < n; i++) {
            slots[i] = TensorSlot{
                .offset_bytes = i * 256ULL, .nbytes = 256,
                .birth_op = OpIndex{0}, .death_op = OpIndex{n},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided, .is_external = false,
                .pad = {}, .slot_id = SlotId{i}, .pad2 = {}
            };
        }

        plan.slots        = slots;
        plan.num_slots    = n;
        plan.num_external = 0;
        plan.pool_bytes   = static_cast<uint64_t>(n) * 256;
        plan.device_type  = DeviceType::CPU;
        plan.device_idx   = 0;

        std::memset(&region, 0, sizeof(region));
        region.kind    = TraceNodeKind::REGION;
        region.ops     = ops;
        region.num_ops = n;
        region.plan    = &plan;
    }

    ~BenchRegion() {
        std::free(ops);
        std::free(out_slots);
        std::free(in_slots);
        std::free(slots);
    }

    BenchRegion(const BenchRegion&)            = delete;
    BenchRegion& operator=(const BenchRegion&) = delete;
};

// Walks — each calls engine.reset() at top so repeated invocation
// through auto-batch is stateless.
ReplayStatus walk_advance_only(ReplayEngine& engine,
                               const TraceEntry* ops, uint32_t n) {
    engine.reset();
    ReplayStatus last = ReplayStatus::MATCH;
    for (uint32_t i = 0; i < n; i++) {
        last = engine.advance(ops[i].schema_hash, ops[i].shape_hash);
    }
    return last;
}

void* walk_advance_output(ReplayEngine& engine,
                          const TraceEntry* ops, uint32_t n) {
    engine.reset();
    void* last_ptr = nullptr;
    for (uint32_t i = 0; i < n; i++) {
        (void)engine.advance(ops[i].schema_hash, ops[i].shape_hash);
        last_ptr = engine.output_ptr(0);
    }
    return last_ptr;
}

void* walk_advance_both(ReplayEngine& engine,
                        const TraceEntry* ops, uint32_t n) {
    engine.reset();
    void* last_ptr = nullptr;
    for (uint32_t i = 0; i < n; i++) {
        (void)engine.advance(ops[i].schema_hash, ops[i].shape_hash);
        last_ptr = engine.output_ptr(0);
        bench::do_not_optimize(engine.input_ptr(0));
    }
    return last_ptr;
}

// CrucibleContext drives its internal ReplayEngine; no external reset.
void* walk_context(CrucibleContext& ctx,
                   const TraceEntry* ops, uint32_t n) {
    void* last_ptr = nullptr;
    for (uint32_t i = 0; i < n; i++) {
        auto s = ctx.advance(ops[i].schema_hash, ops[i].shape_hash);
        last_ptr = ctx.output_ptr(0);
        bench::do_not_optimize(s);
    }
    return last_ptr;
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== replay_engine ===\n");
    std::printf("  sizeof(ReplayEngine)    : %zu B\n", sizeof(ReplayEngine));
    std::printf("  sizeof(CrucibleContext) : %zu B\n", sizeof(CrucibleContext));
    std::printf("  sizeof(TraceEntry)      : %zu B\n\n", sizeof(TraceEntry));

    std::vector<bench::Report> reports;
    reports.reserve(12);  // 3 sizes × 4 scenarios

    for (uint32_t n : {8u, 100u, 1000u}) {
        // Each size scope owns a BenchRegion, a PoolAllocator, the
        // ReplayEngine, and a CrucibleContext — all shared across the 4
        // Runs for this N. IIFE-lambdas capture by reference.
        BenchRegion br{n};
        PoolAllocator pool;
        pool.init(&br.plan);

        ReplayEngine engine;
        engine.init(&br.region, &pool);

        CrucibleContext ctx;
        (void)ctx.activate(&br.region);

        char label1[64], label2[64], label3[64], label4[64];
        std::snprintf(label1, sizeof(label1), "advance only           [%4u ops]", n);
        std::snprintf(label2, sizeof(label2), "advance + output_ptr   [%4u ops]", n);
        std::snprintf(label3, sizeof(label3), "advance + out + in     [%4u ops]", n);
        std::snprintf(label4, sizeof(label4), "CrucibleContext        [%4u ops]", n);

        reports.push_back(bench::run(label1, [&]{
            auto s = walk_advance_only(engine, br.ops, n);
            bench::do_not_optimize(s);
        }));
        reports.push_back(bench::run(label2, [&]{
            auto p = walk_advance_output(engine, br.ops, n);
            bench::do_not_optimize(p);
        }));
        reports.push_back(bench::run(label3, [&]{
            auto p = walk_advance_both(engine, br.ops, n);
            bench::do_not_optimize(p);
        }));
        reports.push_back(bench::run(label4, [&]{
            auto p = walk_context(ctx, br.ops, n);
            bench::do_not_optimize(p);
        }));

        pool.destroy();
    }

    bench::emit_reports_text(reports);

    // Per-op amortized cost at N=1000 ops: report p50 / 1000 ns per op.
    // advance-only at 1000 ops vs. 100 ops should reveal whether the
    // walk is purely linear (ratio ≈ 10) or has startup overhead.
    std::printf("\n=== compare — per-op scaling (advance-only) ===\n  ");
    bench::compare(reports[4], reports[8]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
