// Cipher store/load round-trip — filesystem-backed persistence path.
//
// store() hits disk idempotently (one write per content_hash, skipped
// thereafter).  load() opens + reads + deserializes.  Bench measures
// cold-write, warm-write (dedup skip), and cold-read paths.

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/Effects.h>
#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include "bench_harness.h"

using namespace crucible;

static const fx::Bg BG;
static constexpr auto A = BG.alloc;

static RegionNode* synth_region(Arena& arena, uint32_t num_ops,
                                uint64_t salt) {
    auto* ops = arena.alloc_array<TraceEntry>(A, num_ops);
    std::uninitialized_value_construct_n(ops, num_ops);
    for (uint32_t i = 0; i < num_ops; i++) {
        auto& te = ops[i];
        te.schema_hash = SchemaHash{salt ^ (0xAAAAULL + i)};
        te.shape_hash  = ShapeHash{salt ^ (0xBBBBULL + i)};
        te.num_inputs = 1;
        te.num_outputs = 1;
        te.input_metas = arena.alloc_array<TensorMeta>(A, 1);
        std::uninitialized_value_construct_n(te.input_metas, 1);
        te.input_metas[0].ndim = 1;
        te.input_metas[0].sizes[0] = 64;
        te.input_metas[0].strides[0] = 1;
        te.input_metas[0].dtype = ScalarType::Float;
        te.output_metas = arena.alloc_array<TensorMeta>(A, 1);
        std::uninitialized_value_construct_n(te.output_metas, 1);
        te.output_metas[0] = te.input_metas[0];
        te.input_trace_indices = arena.alloc_array<OpIndex>(A, 1);
        te.input_slot_ids = arena.alloc_array<SlotId>(A, 1);
        te.output_slot_ids = arena.alloc_array<SlotId>(A, 1);
    }
    return make_region(A, arena, ops, num_ops);
}

int main() {
    std::printf("bench_cipher:\n");

    // Isolated tmp dir per run.
    char tmpl[] = "/tmp/crucible_bench_cipher_XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    if (!dir) { std::perror("mkdtemp"); return 1; }
    std::printf("  tmpdir: %s\n", dir);

    for (uint32_t num_ops : {16u, 256u}) {
        Arena arena{1 << 20};

        auto cipher = Cipher::open(dir);

        std::printf("\n── %u ops ──\n", num_ops);

        // ── Cold write: every call sees a distinct content_hash ──
        uint64_t salt = 0;
        BENCH("  cipher.store (cold, distinct)", 2'000, {
            Arena region_arena{1 << 18};
            auto* r = synth_region(region_arena, num_ops, ++salt);
            auto h = cipher.store(r, nullptr);
            bench::DoNotOptimize(h);
        });

        // ── Warm write: same region → filesystem::exists short-circuit ──
        Arena warm_arena{1 << 18};
        auto* warm_region = synth_region(warm_arena, num_ops, 0xC0FFEE);
        // Prime the write.
        (void)cipher.store(warm_region, nullptr);
        BENCH("  cipher.store (warm, dedup hit)", 10'000, {
            auto h = cipher.store(warm_region, nullptr);
            bench::DoNotOptimize(h);
        });

        // ── Cold read: open + read + deserialize ──
        const ContentHash warm_h = warm_region->content_hash;
        BENCH("  cipher.load (cold read)", 2'000, {
            Arena read_arena{1 << 18};
            auto* r = cipher.load(A, warm_h, read_arena);
            bench::DoNotOptimize(r);
        });
    }

    std::filesystem::remove_all(dir);
    std::printf("\nbench_cipher: clean-up ok\n");
    return 0;
}
