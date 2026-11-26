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
    // The arena holds the ops, the slot ids of each output and input, the
    // zero metadata of each output and input, no producer for each input,
    // the slots, the plan and the region.  The replay reads only the slot
    // ids.
    ::fixy::BumpArena<DagArena> arena =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 20});
    DagRef<MemoryPlan> plan;
    DagRef<RegionNode> region;
    uint32_t num_ops = 0;

    explicit BenchRegion(uint32_t n)
        : plan{arena.make<MemoryPlan>(::foundation::effects::testing::test().alloc)},
          region{build_region_(arena, n)},
          num_ops{n} {
        auto const alloc = ::foundation::effects::testing::test().alloc;
        auto slots_run = arena.array(alloc, n, TensorSlot{});
        for (uint32_t i = 0; i < n; i++) {
            *slots_run.view().get(i).expect("i is below n") = TensorSlot{.offset_bytes = i * 256ULL,
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
        }

        plan->slots = slots_run;
        plan->num_external = 0;
        plan->pool_bytes = static_cast<uint64_t>(n) * 256;
        plan->device_type = DeviceType::CPU;
        plan->device_idx = 0;

        region->plan = make_plan_link(plan);
    }

    BenchRegion(const BenchRegion&) = delete;
    BenchRegion& operator=(const BenchRegion&) = delete;

private:
    // A region of n ops, which one frame on the arena builds with its ops and
    // their runs.
    [[nodiscard]] static DagRef<RegionNode> build_region_(::fixy::BumpArena<DagArena>& arena, uint32_t n) {
        return ::foundation::core::with_arena_frame(arena, [n](auto& frame) noexcept {
            auto const alloc = ::foundation::effects::testing::test().alloc;
            auto out_slots_frame_run = frame.array(alloc, n, SlotId{});
            auto in_slots_frame_run = frame.array(alloc, n, SlotId{});
            ::fixy::ArenaRun<SlotId, DagArena> out_slots_run = out_slots_frame_run.weaken();
            ::fixy::ArenaRun<SlotId, DagArena> in_slots_run = in_slots_frame_run.weaken();
            const auto out_metas_run = frame.array(alloc, n, TensorMeta{}).weaken();
            const auto in_metas_run = frame.array(alloc, n, TensorMeta{}).weaken();
            const auto in_producers_run = frame.array(alloc, n, OpIndex{}).weaken();
            auto ops_run = frame.template build_array<TraceEntry>(
                alloc, ::fixy::ElementCount{n}, [](::fixy::RunStart) noexcept { return TraceEntry{}; });

            for (uint32_t i = 0; i < n; i++) {
                const ::fixy::RunStart at_op{::fixy::ElementCount{i}};
                const ::fixy::ElementCount one{1};
                auto out_slot = out_slots_run.window(at_op, one).expect("i is below n");
                auto in_slot = in_slots_run.window(at_op, one).expect("i is below n");
                ::fixy::fill(out_slot.view(), SlotId{i});
                ::fixy::fill(in_slot.view(), SlotId{(i > 0) ? (i - 1) : (n - 1)});

                TraceEntry& op = *ops_run.view().get(i).expect("i is below n");
                op.schema_hash = SchemaHash{1000 + i};
                op.shape_hash = ShapeHash{2000 + i};
                op.set_outputs(out_metas_run.window(at_op, one).expect("i is below n"), out_slot);
                op.set_inputs(in_metas_run.window(at_op, one).expect("i is below n"),
                              in_producers_run.window(at_op, one).expect("i is below n"), in_slot);
            }

            const auto made = mint_dag_node<RegionNode>(alloc, frame);
            made->kind = TraceNodeKind::REGION;
            frame.store(made, &RegionNode::ops, ops_run);
            return made.weaken();
        });
    }
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

// CrucibleContext drives its internal ReplayEngine; no external reset.  One
// compiled scope holds the walk of the region.  No address leaves the scope,
// so the body keeps the last address from the optimizer and gives the last
// status back.
ReplayStatus walk_context(CrucibleContext& ctx, crucible::ContextKey& key, ::fixy::View<const TraceEntry> ops) {
    return ctx
        .with_compiled(key,
                       [ops](crucible::CompiledScope& scope) noexcept {
                           ReplayStatus last = ReplayStatus::MATCH;
                           ::fixy::Option<::fixy::RunAddress> last_ptr{::fixy::none};
                           for (const TraceEntry& op : ops) {
                               auto s = scope.advance(op.schema_hash, op.shape_hash);
                               last_ptr = scope.output_ptr(crucible::OutputPosition{0});
                               bench::do_not_optimize(s);
                               last = s.status();
                           }
                           bench::do_not_optimize(last_ptr);
                           return last;
                       })
        .expect("the bench activates the context first");
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
        // Each size scope owns a BenchRegion, the context of the rows of
        // the engine alone, and the context of the fourth row — all shared
        // across the 4 Runs for this N. IIFE-lambdas capture by reference.
        BenchRegion br{n};
        const ::foundation::effects::Test test = ::foundation::effects::testing::test();
        CrucibleContext engine_owner{test.alloc};
        auto context_key = crucible::mint_context_key(test);
        (void)engine_owner.activate(context_key, *br.region);
        ReplayEngine& engine = engine_owner.engine(test);

        CrucibleContext ctx{test.alloc};
        (void)ctx.activate(context_key, *br.region);

        // The ops of the region, which each walk reads.
        const ::fixy::View<const TraceEntry> region_ops = br.region->ops.view();

        const bench::RunName label1 = bench::format_name("advance only           [{} ops]", ::fixy::Padded<4>{n});
        const bench::RunName label2 = bench::format_name("advance + output_ptr   [{} ops]", ::fixy::Padded<4>{n});
        const bench::RunName label3 = bench::format_name("advance + out + in     [{} ops]", ::fixy::Padded<4>{n});
        const bench::RunName label4 = bench::format_name("CrucibleContext        [{} ops]", ::fixy::Padded<4>{n});

        bench::add_report(reports, bench::run(label1, [&] {
                              auto s = walk_advance_only(engine, region_ops);
                              bench::do_not_optimize(s);
                          }));
        bench::add_report(reports, bench::run(label2, [&] {
                              auto p = walk_advance_output(engine, region_ops);
                              bench::do_not_optimize(p);
                          }));
        bench::add_report(reports, bench::run(label3, [&] {
                              auto p = walk_advance_both(engine, region_ops);
                              bench::do_not_optimize(p);
                          }));
        bench::add_report(reports, bench::run(label4, [&] {
                              auto p = walk_context(ctx, context_key, region_ops);
                              bench::do_not_optimize(p);
                          }));
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
