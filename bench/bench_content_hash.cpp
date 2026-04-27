// Isolate compute_content_hash to inform wymix / fmix64 alternatives.
//
// vit_b.crtrace phase breakdown shows P2b (content hash) at ~30% of
// phase-2 cost.  Bench varies tensor rank (4D vs 8D) and input count
// (1 vs 3) to tell where the cost concentrates.

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/MerkleDag.h>

#include "bench_harness.h"

using namespace crucible;

static const effects::Bg BG;
static constexpr auto A = BG.alloc;

static std::vector<TraceEntry> make_ops(Arena& arena,
                                        uint32_t count,
                                        uint8_t ndim,
                                        uint16_t num_inputs) {
    std::vector<TraceEntry> ops(count);
    for (uint32_t i = 0; i < count; i++) {
        auto& te = ops[i];
        te.schema_hash  = SchemaHash{0xAAAA'0000'0000'0000ULL ^ i};
        te.num_inputs   = num_inputs;
        te.num_outputs  = 1;
        te.input_metas  = arena.alloc_array<TensorMeta>(A, num_inputs);
        std::uninitialized_value_construct_n(te.input_metas, num_inputs);
        for (uint16_t j = 0; j < num_inputs; j++) {
            auto& m = te.input_metas[j];
            m.ndim        = ndim;
            m.dtype       = ScalarType::Float;
            m.device_type = DeviceType::CUDA;
            for (uint8_t d = 0; d < ndim; d++) {
                m.sizes[d]   = (d == ndim - 1) ? 128 : 32;
                m.strides[d] = (d == ndim - 1) ? 1   : 128;
            }
        }
        te.output_metas = arena.alloc_array<TensorMeta>(A, 1);
        std::uninitialized_value_construct_n(te.output_metas, 1);
        te.output_metas[0] = te.input_metas[0];
    }
    return ops;
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    struct Config {
        uint32_t  n;
        uint8_t   ndim;
        uint16_t  ins;
        const char* label;
    };
    const Config cfgs[] = {
        {  128, 4, 1, "128 ops ×  1×4D"},
        {  128, 4, 3, "128 ops ×  3×4D"},
        {  128, 8, 3, "128 ops ×  3×8D"},
        { 1024, 4, 3, "1024 ops × 3×4D"},
        { 1024, 8, 3, "1024 ops × 3×8D"},
    };

    std::printf("=== content_hash ===\n\n");

    // One arena + ops vector per config; kept alive in outer scope so
    // the body can reference the span by value-into-reference.
    std::vector<bench::Report> reports;

    for (const auto& c : cfgs) {
        Arena arena{1 << 20};
        auto  ops  = make_ops(arena, c.n, c.ndim, c.ins);
        const std::span<const TraceEntry> span{ops.data(), ops.size()};

        reports.push_back(bench::run(c.label, [&]{
            auto h = compute_content_hash(span);
            bench::do_not_optimize(h);
        }));
    }

    bench::emit_reports(reports, json);
    return 0;
}
