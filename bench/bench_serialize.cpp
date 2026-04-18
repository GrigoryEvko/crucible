// Serialize / deserialize round-trip bench for RegionNode (CDAG v7).
//
// Cipher::store round-trips through serialize_region; Cipher::load uses
// deserialize_region.  These land on the persistence hot path during
// checkpoint / replay.  Target: sub-microsecond for small regions,
// linear in op count for large ones.

#include <crucible/Arena.h>
#include <crucible/Effects.h>
#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "bench_harness.h"

using namespace crucible;

static const fx::Bg BG;
static constexpr auto A = BG.alloc;

// Synthesize a RegionNode with N ops, each with 2 inputs + 1 output.
static RegionNode* make_region(Arena& arena, uint32_t num_ops) {
    auto* ops = arena.alloc_array<TraceEntry>(A, num_ops);
    std::uninitialized_value_construct_n(ops, num_ops);

    for (uint32_t i = 0; i < num_ops; i++) {
        auto& te = ops[i];
        te.schema_hash = SchemaHash{0xAAAA'0000'0000'0000ULL + i};
        te.shape_hash  = ShapeHash{0xBBBB'0000'0000'0000ULL + i};
        te.scope_hash  = ScopeHash{0xCCCC'0000'0000'0000ULL + i};
        te.callsite_hash = CallsiteHash{0xDDDD'0000'0000'0000ULL + i};
        te.num_inputs  = 2;
        te.num_outputs = 1;
        te.num_scalar_args = 0;
        te.kernel_id = CKernelId::GEMM_MM;

        te.input_metas = arena.alloc_array<TensorMeta>(A, 2);
        std::uninitialized_value_construct_n(te.input_metas, 2);
        for (uint16_t j = 0; j < 2; j++) {
            te.input_metas[j].ndim = 2;
            te.input_metas[j].sizes[0] = 128;
            te.input_metas[j].sizes[1] = 256;
            te.input_metas[j].strides[0] = 256;
            te.input_metas[j].strides[1] = 1;
            te.input_metas[j].dtype = ScalarType::Float;
        }
        te.output_metas = arena.alloc_array<TensorMeta>(A, 1);
        std::uninitialized_value_construct_n(te.output_metas, 1);
        te.output_metas[0].ndim = 2;
        te.output_metas[0].sizes[0] = 128;
        te.output_metas[0].sizes[1] = 256;
        te.output_metas[0].strides[0] = 256;
        te.output_metas[0].strides[1] = 1;
        te.output_metas[0].dtype = ScalarType::Float;

        te.input_trace_indices = arena.alloc_array<OpIndex>(A, 2);
        te.input_trace_indices[0] = (i >= 1) ? OpIndex{i - 1} : OpIndex::none();
        te.input_trace_indices[1] = OpIndex::none();
        te.input_slot_ids = arena.alloc_array<SlotId>(A, 2);
        te.input_slot_ids[0] = SlotId{i};
        te.input_slot_ids[1] = SlotId{100 + i};
        te.output_slot_ids = arena.alloc_array<SlotId>(A, 1);
        te.output_slot_ids[0] = SlotId{200 + i};
    }

    auto* region = make_region(A, arena, ops, num_ops);
    return region;
}

static void bench_for_size(uint32_t num_ops) {
    Arena arena{1 << 20};
    RegionNode* region = make_region(arena, num_ops);

    const size_t wire_cap = 64 + num_ops *
        (40 + 2 * sizeof(TensorMeta) + sizeof(TensorMeta) + 3 * 3 * sizeof(uint32_t));
    std::vector<uint8_t> buf(wire_cap);

    char label[64];
    // Iteration count scales inversely with op count so each bench
    // finishes in ~1 s regardless of region size.
    const uint64_t iters_ser = 1'000'000 / num_ops;
    const uint64_t iters_des = 100'000  / num_ops;

    std::snprintf(label, sizeof(label), "  serialize_region   (%u ops)", num_ops);
    BENCH(label, iters_ser, {
        size_t n = serialize_region(region, nullptr, std::span<uint8_t>{buf});
        bench::DoNotOptimize(n);
    });

    // Pre-serialize once for the deserialize bench.
    size_t serial_len = serialize_region(region, nullptr, std::span<uint8_t>{buf});
    if (serial_len == 0) {
        std::fprintf(stderr, "serialize failed\n");
        std::exit(1);
    }

    std::snprintf(label, sizeof(label), "  deserialize_region (%u ops)", num_ops);
    BENCH(label, iters_des, {
        Arena read_arena{1 << 20};
        auto* r = deserialize_region(
            A, std::span<const uint8_t>{buf.data(), serial_len}, read_arena);
        bench::DoNotOptimize(r);
    });

    std::printf("  (wire size: %zu bytes = %.1f B/op)\n",
                serial_len,
                static_cast<double>(serial_len) / num_ops);
}

int main() {
    std::printf("bench_serialize:\n");
    for (uint32_t n : {16u, 128u, 1024u}) {
        std::printf("\n── %u ops ──\n", n);
        bench_for_size(n);
    }
    return 0;
}
