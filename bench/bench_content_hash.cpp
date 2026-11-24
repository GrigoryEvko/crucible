// Isolate compute_content_hash to measure the cost of its fmix64 chain.
//
// vit_b.crtrace phase breakdown shows P2b (content hash) at ~30% of
// phase-2 cost.  Bench varies tensor rank (4D vs 8D) and input count
// (1 vs 3) to tell where the cost concentrates.

#include <cstdint>

#include <crucible/MerkleDag.h>
#include <foundation/effects/Effect.h>

#include "bench_harness.h"

using namespace crucible;

static constexpr auto A = ::foundation::effects::testing::bg().alloc;

// Each input and the output of each op have one metadata.
static ::fixy::HeapArray<TraceEntry> make_ops(::fixy::BumpArena<DagArena>& arena, uint32_t count, uint8_t ndim,
                                              uint16_t num_inputs) {
    TensorMeta meta{};
    meta.ndim = ndim;
    meta.dtype = ScalarType::Float;
    meta.device_type = DeviceType::CUDA;
    for (uint8_t d = 0; d < ndim; d++) {
        meta.sizes[d] = tensor_dim((d == ndim - 1) ? 128 : 32);
        meta.strides[d] = tensor_dim((d == ndim - 1) ? 1 : 128);
    }
    ::fixy::HeapArray<TraceEntry> ops = ::fixy::mint_heap_array(A, count, TraceEntry{});
    auto const& meta_read = meta;
    ops = ::fixy::with_mut(std::move(ops), [&arena, &meta_read, count, num_inputs](auto run) noexcept {
        for (uint32_t i = 0; i < count; i++) {
            auto te = run.get_mut(i).expect("i is below count");
            te->schema_hash = SchemaHash{0xAAAA000000000000ULL ^ i};
            const auto input_metas_run = arena.array(A, num_inputs, meta_read);
            const auto producers_run = arena.array(A, num_inputs, OpIndex{});
            const auto input_slots_run = arena.array(A, num_inputs, SlotId{});
            te->set_inputs(input_metas_run, producers_run, input_slots_run);
            const auto output_metas_run = arena.array(A, 1, meta_read);
            const auto output_slots_run = arena.array(A, 1, SlotId{});
            te->set_outputs(output_metas_run, output_slots_run);
        }
    });
    return ops;
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    struct Config {
        uint32_t n = 0;
        uint8_t ndim = 0;
        uint16_t ins = 0;
        ::fixy::TextView label{};
    };
    constexpr ::fixy::FixedArray<Config, 5> cfgs{
        Config{128, 4, 1, "128 ops ×  1×4D"},  Config{128, 4, 3, "128 ops ×  3×4D"},
        Config{128, 8, 3, "128 ops ×  3×8D"},  Config{1024, 4, 3, "1024 ops × 3×4D"},
        Config{1024, 8, 3, "1024 ops × 3×8D"},
    };

    bench::print("=== content_hash ===\n\n");

    // One arena + ops vector per config; kept alive in outer scope so
    // the body can reference the span by value-into-reference.
    // One row for each of the five configs.
    ::fixy::FixedList<bench::Report, 5> reports{};

    for (const auto& c : cfgs) {
        ::fixy::BumpArena<DagArena> arena =
            ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 20});
        auto ops = make_ops(arena, c.n, c.ndim, c.ins);
        const ::fixy::View<const TraceEntry> span = ops.view();

        bench::add_report(reports, bench::run(c.label, [&] {
                              auto h = compute_content_hash(span);
                              bench::do_not_optimize(h);
                          }));
    }

    bench::emit_reports(reports.view(), json);
    return 0;
}
