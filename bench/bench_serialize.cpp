// Serialize / deserialize round-trip bench for RegionNode (CDAG v7).
//
// Cipher::store round-trips through serialize_region; Cipher::load uses
// deserialize_region. These land on the persistence hot path during
// checkpoint / replay. Target: sub-microsecond for small regions,
// linear in op count for large ones.

#include <cstddef>
#include <cstdint>

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
static ::fixy::Ref<RegionNode> synth_region(::fixy::BumpArena& dag_arena, uint32_t num_ops) {
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
        te.kernel_id = CKernelId::GEMM_MM;

        const ::fixy::View<OpIndex> producers = dag_arena.array(A, 2, OpIndex::none());
        if (i >= 1) *producers.get(0).expect("the op has two inputs") = OpIndex{i - 1};
        const ::fixy::View<SlotId> input_slots = dag_arena.array(A, 2, SlotId{i});
        *input_slots.get(1).expect("the op has two inputs") = SlotId{100 + i};
        te.set_inputs(dag_arena.array(A, 2, meta), producers, input_slots);
        te.set_outputs(dag_arena.array(A, 1, meta), dag_arena.array(A, 1, SlotId{200 + i}));
    }

    return make_region(A, dag_arena, ops);
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== serialize ===\n\n");

    // Two rows for each of the three sizes.
    ::fixy::FixedList<bench::Report, 6> reports{};

    constexpr ::fixy::FixedArray<uint32_t, 3> kSizes{16u, 128u, 1024u};
    for (uint32_t const num_ops : kSizes) {
        // Per-size fixture: one arena, one region, one wire buffer sized
        // generously for the worst-case row shape. Lives across both
        // (ser, deser) Runs.
        ::fixy::BumpArena dag_arena{1 << 20};
        const ::fixy::Ref<RegionNode> region = synth_region(dag_arena, num_ops);

        const size_t wire_cap =
            64 + num_ops * (40 + 2 * sizeof(TensorMeta) + sizeof(TensorMeta) + 3 * 3 * sizeof(uint32_t));
        ::fixy::HeapArray<std::byte> buf = ::fixy::mint_heap_array(A, wire_cap, std::byte{0});

        const bench::RunName label_ser = bench::format_name("serialize_region   ({} ops)", ::fixy::Padded<4>{num_ops});
        const bench::RunName label_des = bench::format_name("deserialize_region ({} ops)", ::fixy::Padded<4>{num_ops});

        bench::add_report(reports, bench::run(label_ser, [&] {
                              size_t n = serialize_region(SerializedRegion{*region}, buf.view());
                              bench::do_not_optimize(n);
                          }));

        // Prime the buffer once — deserialize consumes stable bytes.
        const size_t serial_len = serialize_region(SerializedRegion{*region}, buf.view());
        if (serial_len == 0) {
            ::fixy::fatal("serialize_region wrote no byte for a region of {} ops", num_ops);
        }
        const ::fixy::View<const std::byte> image =
            buf.view().window(0, serial_len).expect("the writer writes inside its buffer");

        bench::add_report(reports, bench::run(label_des, [&, image] {
                              ::fixy::BumpArena read_arena{1 << 20};
                              const ::fixy::Result<LoadedRegionNode, LoadError> loaded_region =
                                  deserialize_region(A, image, read_arena);
                              const bool is_loaded = loaded_region.is_ok();
                              bench::do_not_optimize(is_loaded);
                          }));

        bench::print("  [{} ops] wire = {} bytes ({} B/op)\n", num_ops, serial_len,
                     ::fixy::Fixed<1>{static_cast<double>(serial_len) / num_ops});
    }

    bench::print("\n");
    bench::emit_reports(reports.view(), json);
    return 0;
}
