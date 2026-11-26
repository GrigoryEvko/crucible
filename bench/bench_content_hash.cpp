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

// Each input and the output of each op have one metadata.  One frame on the
// arena builds the ops and their arrays, which live as long as the arena.
static ::fixy::ArenaRun<TraceEntry const, DagArena> make_ops(::fixy::BumpArena<DagArena>& arena, uint32_t count,
                                                             uint8_t ndim, uint16_t num_inputs) {
    TensorMeta meta{};
    meta.ndim = ndim;
    meta.dtype = ScalarType::Float;
    meta.device_type = DeviceType::CUDA;
    for (uint8_t d = 0; d < ndim; d++) {
        meta.sizes[d] = tensor_dim((d == ndim - 1) ? 128 : 32);
        meta.strides[d] = tensor_dim((d == ndim - 1) ? 1 : 128);
    }
    return ::foundation::core::with_arena_frame(arena, [&meta, count, num_inputs](auto& frame) noexcept {
        auto ops_run = make_op_run(A, frame, count);
        for (uint32_t i = 0; i < count; i++) {
            const auto op = hold_op(ops_run, i);
            op->schema_hash = SchemaHash{0xAAAA000000000000ULL ^ i};
            TraceEntry::set_inputs(frame, op, frame.array(A, num_inputs, meta), frame.array(A, num_inputs, OpIndex{}),
                                   frame.array(A, num_inputs, SlotId{}));
            TraceEntry::set_outputs(frame, op, frame.array(A, 1, meta), frame.array(A, 1, SlotId{}));
        }
        const auto& built = ops_run;
        return built.weaken();
    });
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
