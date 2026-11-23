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

#include <crucible/CrucibleContext.h>
#include <crucible/ReplayEngine.h>

#include "bench_harness.h"

using namespace crucible;

namespace {

// The views of the replay chain are minted on the thread that holds a
// Vigil's producer claim.  These benches run the chain without a Vigil,
// so they take that context from the test door.
constexpr VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<Vigil>();

// Synthetic region: N ops, one output + one input each. Slot layout:
// op[i] writes to slot i, reads from slot (i-1) mod N. Plan gives each
// slot 256 B of a dummy pool.
struct BenchRegion {
    // The slot ids of each output and input, the zero metadata of each
    // output and input, and no producer for each input.  Each op keeps a View
    // that writes them, so they are runs of the arena.  The replay reads only
    // the slot ids.
    ::fixy::BumpArena<DagArena> arena =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 20});
    ::fixy::HeapArray<TraceEntry> ops{};
    ::fixy::HeapArray<TensorSlot> slots{};
    MemoryPlan plan{};
    RegionNode region{};
    uint32_t num_ops = 0;

    explicit BenchRegion(uint32_t n) : num_ops{n} {
        auto const alloc = ::foundation::effects::testing::test().alloc;
        const auto out_slots_run = arena.array(alloc, n, SlotId{});
        const auto in_slots_run = arena.array(alloc, n, SlotId{});
        const auto out_metas_run = arena.array(alloc, n, TensorMeta{});
        const auto in_metas_run = arena.array(alloc, n, TensorMeta{});
        const auto in_producers_run = arena.array(alloc, n, OpIndex{});
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<SlotId> out_slots = out_slots_run.view();
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<SlotId> in_slots = in_slots_run.view();
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TensorMeta> out_metas = out_metas_run.view();
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TensorMeta> in_metas = in_metas_run.view();
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<OpIndex> in_producers = in_producers_run.view();
        ops = ::fixy::mint_heap_array(alloc, n, TraceEntry{});

        // One frame of the ops writes each op.
        ops = ::fixy::with_mut(
            std::move(ops), [out_slots, in_slots, out_metas, in_metas, in_producers, n](auto run) noexcept {
                for (uint32_t i = 0; i < n; i++) {
                    const ::fixy::View<SlotId> out_slot = out_slots.window(i, 1).expect("i is below n");
                    const ::fixy::View<SlotId> in_slot = in_slots.window(i, 1).expect("i is below n");
                    ::fixy::fill(out_slot, SlotId{i});
                    ::fixy::fill(in_slot, SlotId{(i > 0) ? (i - 1) : (n - 1)});

                    TraceEntry& op = *run.get(i).expect("i is below n");
                    op.schema_hash = SchemaHash{1000 + i};
                    op.shape_hash = ShapeHash{2000 + i};
                    op.set_outputs(out_metas.window(i, 1).expect("i is below n"), out_slot);
                    op.set_inputs(in_metas.window(i, 1).expect("i is below n"),
                                  in_producers.window(i, 1).expect("i is below n"), in_slot);
                }
            });

        slots = ::fixy::mint_heap_array_with<TensorSlot>(alloc, n, [n](std::size_t position) noexcept {
            const auto i = static_cast<uint32_t>(position);
            return TensorSlot{.offset_bytes = i * 256ULL,
                              .nbytes = 256,
                              .birth_op = OpIndex{0},
                              .death_op = OpIndex{n},
                              .dtype = ScalarType::Float,
                              .device_type = DeviceType::CPU,
                              .device_idx = 0,
                              .layout = Layout::Strided,
                              .is_external = false,
                              .pad = {},
                              .slot_id = SlotId{i},
                              .pad2 = {}};
        });

        plan.slots = slots.view();
        plan.num_external = 0;
        plan.pool_bytes = static_cast<uint64_t>(n) * 256;
        plan.device_type = DeviceType::CPU;
        plan.device_idx = 0;

        region.kind = TraceNodeKind::REGION;
        region.ops = ops.view();
        region.plan = make_plan_link(plan);
    }

    BenchRegion(const BenchRegion&) = delete;
    BenchRegion& operator=(const BenchRegion&) = delete;
};

// Walks — each calls engine.reset() at top so repeated invocation
// through auto-batch is stateless.  Each helper mints its own
// ActiveView; engine is already initialized before being passed in.
ReplayStatus walk_advance_only(ReplayEngine& engine, ::fixy::View<const TraceEntry> ops) {
    auto av = engine.mint_active_view(kVigilForeground);
    engine.reset(av);
    ReplayStatus last = ReplayStatus::MATCH;
    for (const TraceEntry& op : ops) {
        last = engine.advance(op.schema_hash, op.shape_hash, av).status();
    }
    return last;
}

::fixy::Option<::fixy::RunAddress> walk_advance_output(ReplayEngine& engine, ::fixy::View<const TraceEntry> ops) {
    auto av = engine.mint_active_view(kVigilForeground);
    engine.reset(av);
    ::fixy::Option<::fixy::RunAddress> last_ptr{::fixy::none};
    for (const TraceEntry& op : ops) {
        (void)engine.advance(op.schema_hash, op.shape_hash, av);
        last_ptr = engine.output_ptr(0, av);
    }
    return last_ptr;
}

::fixy::Option<::fixy::RunAddress> walk_advance_both(ReplayEngine& engine, ::fixy::View<const TraceEntry> ops) {
    auto av = engine.mint_active_view(kVigilForeground);
    engine.reset(av);
    ::fixy::Option<::fixy::RunAddress> last_ptr{::fixy::none};
    for (const TraceEntry& op : ops) {
        (void)engine.advance(op.schema_hash, op.shape_hash, av);
        last_ptr = engine.output_ptr(0, av);
        bench::do_not_optimize(engine.input_ptr(0, av));
    }
    return last_ptr;
}

// CrucibleContext drives its internal ReplayEngine; no external reset.
::fixy::Option<::fixy::RunAddress> walk_context(CrucibleContext& ctx, ::fixy::View<const TraceEntry> ops) {
    auto cv = ctx.mint_compiled_view(kVigilForeground);
    ::fixy::Option<::fixy::RunAddress> last_ptr{::fixy::none};
    for (const TraceEntry& op : ops) {
        auto s = ctx.advance(op.schema_hash, op.shape_hash, cv);
        last_ptr = ctx.output_ptr(0, cv);
        bench::do_not_optimize(s);
    }
    return last_ptr;
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== replay_engine ===\n");
    bench::print("  sizeof(ReplayEngine)    : {} B\n", sizeof(ReplayEngine));
    bench::print("  sizeof(CrucibleContext) : {} B\n", sizeof(CrucibleContext));
    bench::print("  sizeof(TraceEntry)      : {} B\n\n", sizeof(TraceEntry));

    // Four rows for each of the three sizes.
    ::fixy::FixedList<bench::Report, 12> reports{};

    constexpr ::fixy::FixedArray<uint32_t, 3> kSizes{8u, 100u, 1000u};
    for (uint32_t const n : kSizes) {
        // Each size scope owns a BenchRegion, a PoolAllocator, the
        // ReplayEngine, and a CrucibleContext — all shared across the 4
        // Runs for this N. IIFE-lambdas capture by reference.
        BenchRegion br{n};
        PoolAllocator pool;
        pool.init(::foundation::effects::testing::test().alloc, br.plan);

        ReplayEngine engine;
        engine.init(br.region, ::fixy::to(pool));

        CrucibleContext ctx{::foundation::effects::testing::test().alloc};
        (void)ctx.activate(br.region);

        const bench::RunName label1 = bench::format_name("advance only           [{} ops]", ::fixy::Padded<4>{n});
        const bench::RunName label2 = bench::format_name("advance + output_ptr   [{} ops]", ::fixy::Padded<4>{n});
        const bench::RunName label3 = bench::format_name("advance + out + in     [{} ops]", ::fixy::Padded<4>{n});
        const bench::RunName label4 = bench::format_name("CrucibleContext        [{} ops]", ::fixy::Padded<4>{n});

        bench::add_report(reports, bench::run(label1, [&] {
                              auto s = walk_advance_only(engine, br.region.ops);
                              bench::do_not_optimize(s);
                          }));
        bench::add_report(reports, bench::run(label2, [&] {
                              auto p = walk_advance_output(engine, br.region.ops);
                              bench::do_not_optimize(p);
                          }));
        bench::add_report(reports, bench::run(label3, [&] {
                              auto p = walk_advance_both(engine, br.region.ops);
                              bench::do_not_optimize(p);
                          }));
        bench::add_report(reports, bench::run(label4, [&] {
                              auto p = walk_context(ctx, br.region.ops);
                              bench::do_not_optimize(p);
                          }));

        pool.destroy();
    }

    bench::emit_reports_text(reports.view());

    // Per-op amortized cost at N=1000 ops: report p50 / 1000 ns per op.
    // advance-only at 1000 ops vs. 100 ops should reveal whether the
    // walk is purely linear (ratio ≈ 10) or has startup overhead.
    bench::print("\n=== compare — per-op scaling (advance-only) ===\n  ");
    bench::compare(*reports.get(4).expect("the advance row of 100 ops"),
                   *reports.get(8).expect("the advance row of 1000 ops"))
        .print_text();

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
