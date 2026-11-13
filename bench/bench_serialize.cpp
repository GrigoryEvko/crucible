// Serialize / deserialize round-trip bench for RegionNode (CDAG v7).
//
// Cipher::store round-trips through serialize_region; Cipher::load uses
// deserialize_region. These land on the persistence hot path during
// checkpoint / replay. Target: sub-microsecond for small regions,
// linear in op count for large ones.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <foundation/effects/Effect.h>

#include "bench_harness.h"

using namespace crucible;

static const auto BG = ::foundation::effects::testing::bg();
static constexpr auto A = BG.alloc;

// Avoids collision with crucible::make_region — this one builds a region
// populated with realistic (schema/shape/scope/callsite, 2-input GEMM)
// op rows.  The ops, their arrays and the region are in dag_arena.
static RegionNode* synth_region(::fixy::BumpArena& dag_arena, uint32_t num_ops) {
    const ::fixy::View<TraceEntry> ops = dag_arena.array<TraceEntry>(A, num_ops, TraceEntry{});

    // Each input and the output of each op have one metadata.
    TensorMeta meta{};
    meta.ndim = 2;
    meta.sizes[0] = tensor_dim(128);
    meta.sizes[1] = tensor_dim(256);
    meta.strides[0] = tensor_dim(256);
    meta.strides[1] = tensor_dim(1);
    meta.dtype = ScalarType::Float;

    for (uint32_t i = 0; i < num_ops; i++) {
        auto& te = *ops.get(i).expect("i is below num_ops");
        te.schema_hash = SchemaHash{0xAAAA000000000000ULL + i};
        te.shape_hash = ShapeHash{0xBBBB000000000000ULL + i};
        te.scope_hash = ScopeHash{0xCCCC000000000000ULL + i};
        te.callsite_hash = CallsiteHash{0xDDDD000000000000ULL + i};
        te.set_num_inputs(2);
        te.set_num_outputs(1);
        te.set_num_scalar_args(0);
        te.kernel_id = CKernelId::GEMM_MM;

        te.set_input_metas(dag_arena.array(A, 2, meta));
        te.set_output_metas(dag_arena.array(A, 1, meta));

        const ::fixy::View<OpIndex> producers = dag_arena.array(A, 2, OpIndex::none());
        if (i >= 1) *producers.get(0).expect("the op has two inputs") = OpIndex{i - 1};
        te.set_input_trace_indices(producers);
        const ::fixy::View<SlotId> input_slots = dag_arena.array(A, 2, SlotId{i});
        *input_slots.get(1).expect("the op has two inputs") = SlotId{100 + i};
        te.set_input_slot_ids(input_slots);
        te.set_output_slot_ids(dag_arena.array(A, 1, SlotId{200 + i}));
    }

    return make_region(A, dag_arena, ops);
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== serialize ===\n\n");

    std::vector<bench::Report> reports;

    for (uint32_t num_ops : {16u, 128u, 1024u}) {
        // Per-size fixture: one arena, one region, one wire buffer sized
        // generously for the worst-case row shape. Lives across both
        // (ser, deser) Runs.
        ::fixy::BumpArena dag_arena{1 << 20};
        RegionNode* region = synth_region(dag_arena, num_ops);

        const size_t wire_cap =
            64 + num_ops * (40 + 2 * sizeof(TensorMeta) + sizeof(TensorMeta) + 3 * 3 * sizeof(uint32_t));
        std::vector<uint8_t> buf(wire_cap);

        char label_ser[64], label_des[64];
        std::snprintf(label_ser, sizeof(label_ser), "serialize_region   (%4u ops)", num_ops);
        std::snprintf(label_des, sizeof(label_des), "deserialize_region (%4u ops)", num_ops);

        reports.push_back(bench::run(label_ser, [&] {
            size_t n = serialize_region(SerializedRegion{*region}, SerialBuffer{buf});
            bench::do_not_optimize(n);
        }));

        // Prime the buffer once — deserialize consumes stable bytes.
        const size_t serial_len = serialize_region(SerializedRegion{*region}, SerialBuffer{buf});
        if (serial_len == 0) {
            std::fprintf(stderr, "serialize failed at num_ops=%u\n", num_ops);
            std::exit(1);
        }

        reports.push_back(bench::run(label_des, [&, serial_len] {
            ::fixy::BumpArena read_arena{1 << 20};
            const auto loaded_region =
                deserialize_region(A, std::span<const uint8_t>{buf.data(), serial_len}, read_arena);
            RegionNode* r = loaded_region ? loaded_region->value() : nullptr;
            bench::do_not_optimize(r);
        }));

        std::printf("  [%u ops] wire = %zu bytes (%.1f B/op)\n", num_ops, serial_len,
                    static_cast<double>(serial_len) / num_ops);
    }

    std::printf("\n");
    bench::emit_reports(reports, json);
    return 0;
}
