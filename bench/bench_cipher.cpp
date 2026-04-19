// Cipher store/load round-trip — filesystem-backed persistence path.
//
// store() hits disk idempotently (one write per content_hash, skipped
// thereafter). load() opens + reads + deserializes. Bench measures
// cold-write, warm-write (dedup skip), and cold-read paths at two
// region sizes (16 and 256 ops).
//
// Each sample creates a fresh Arena where required; the Cipher opens
// a tmp dir that's cleaned on exit. Run is registered with BPF — the
// file-I/O surface makes the `disk_r / disk_w / pg_miss` counters
// informative even when paths are cache-hot.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/Effects.h>
#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>

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
        te.num_inputs  = 1;
        te.num_outputs = 1;
        te.input_metas = arena.alloc_array<TensorMeta>(A, 1);
        std::uninitialized_value_construct_n(te.input_metas, 1);
        te.input_metas[0].ndim       = 1;
        te.input_metas[0].sizes[0]   = 64;
        te.input_metas[0].strides[0] = 1;
        te.input_metas[0].dtype      = ScalarType::Float;
        te.output_metas = arena.alloc_array<TensorMeta>(A, 1);
        std::uninitialized_value_construct_n(te.output_metas, 1);
        te.output_metas[0] = te.input_metas[0];
        te.input_trace_indices = arena.alloc_array<OpIndex>(A, 1);
        te.input_slot_ids      = arena.alloc_array<SlotId>(A, 1);
        te.output_slot_ids     = arena.alloc_array<SlotId>(A, 1);
    }
    return make_region(A, arena, ops, num_ops);
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    // Isolated tmp dir per run.
    char tmpl[] = "/tmp/crucible_bench_cipher_XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    if (dir == nullptr) {
        std::perror("mkdtemp");
        return 1;
    }
    std::printf("=== cipher ===\n  tmpdir: %s\n\n", dir);

    // 3 Runs × 2 region sizes = 6 Reports. Each (num_ops) scope holds
    // its Cipher + warm-region fixture across three IIFE-lambdas.
    std::vector<bench::Report> reports;
    reports.reserve(6);

    for (uint32_t num_ops : {16u, 256u}) {
        // Cipher + warm_region live in THIS scope, shared across the
        // three benches for this num_ops.  Distinct 'salt' per size
        // avoids collisions in the dedup hash space.
        auto cipher = Cipher::open(dir);

        Arena warm_arena{1 << 18};
        auto* warm_region = synth_region(warm_arena, num_ops, 0xC0FFEE + num_ops);
        (void)cipher.store(warm_region, nullptr);   // prime the warm path
        const ContentHash warm_h = warm_region->content_hash;

        // Cold write: distinct content_hash per iteration, so every
        // sample hits the filesystem. Auto-batch is fine: each body
        // allocates its own arena + region, so state doesn't grow
        // unboundedly across batches.
        char label_cold[64], label_warm[64], label_read[64];
        std::snprintf(label_cold, sizeof(label_cold), "store cold  (%u ops, distinct)",  num_ops);
        std::snprintf(label_warm, sizeof(label_warm), "store warm  (%u ops, dedup hit)", num_ops);
        std::snprintf(label_read, sizeof(label_read), "load  cold  (%u ops)",            num_ops);

        reports.push_back([&]{
            uint64_t salt = static_cast<uint64_t>(num_ops) << 32;
            return bench::run(label_cold, [&]{
                Arena region_arena{1 << 18};
                auto* r = synth_region(region_arena, num_ops, ++salt);
                auto h = cipher.store(r, nullptr);
                bench::do_not_optimize(h);
            });
        }());

        reports.push_back(bench::run(label_warm, [&]{
            auto h = cipher.store(warm_region, nullptr);
            bench::do_not_optimize(h);
        }));

        reports.push_back(bench::run(label_read, [&]{
            Arena read_arena{1 << 18};
            auto* r = cipher.load(A, warm_h, read_arena);
            bench::do_not_optimize(r);
        }));
    }

    bench::emit_reports_text(reports);

    // warm vs. cold store at 256 ops — the dedup-skip path should be
    // orders of magnitude faster; filesystem::exists + compare vs. a
    // full write.
    std::printf("\n=== compare — store dedup win (256 ops) ===\n  ");
    bench::compare(reports[3], reports[4]).print_text(stdout);

    std::filesystem::remove_all(dir);
    std::printf("\n%s: cleaned\n", dir);

    bench::emit_reports_json(reports, json);
    return 0;
}
