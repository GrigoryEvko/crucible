// MerkleDag + BackgroundThread::build_trace hot-path benchmarks.
//
// Covers:
//   - compute_content_hash() at 3 region sizes (8, 481, 1110 ops)
//   - make_region() construction
//   - compute_merkle_hash() on a built RegionNode
//   - build_csr() adjacency construction
//   - compute_memory_plan() sweep-line
//   - build_trace() full pipeline (arena + PtrMap + slot tracking + CSR)
//   - optional: build_trace from loaded .crtrace file(s) (pass as argv)
//
// Simulates ResNet-18 (481 ops) and GPT-style (1110 ops) iteration sizes.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

#include <crucible/Arena.h>
#include <crucible/BackgroundThread.h>
#include <crucible/Effects.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceLoader.h>
#include <crucible/TraceRing.h>

#include "bench_harness.h"

using namespace crucible;

namespace {

// Deterministic xorshift PRNG, seeded per-bench for reproducibility.
uint64_t bench_rng_state = 0xDEADBEEFCAFEBABEULL;

uint64_t bench_rand() noexcept {
    bench_rng_state ^= bench_rng_state << 13;
    bench_rng_state ^= bench_rng_state >> 7;
    bench_rng_state ^= bench_rng_state << 17;
    return bench_rng_state;
}

TraceEntry make_synthetic_entry(fx::Alloc a, Arena& arena,
                                uint16_t n_in, uint16_t n_out,
                                uint16_t n_scalar, uint8_t ndim) {
    TraceEntry te{};
    te.schema_hash     = SchemaHash{bench_rand()};
    te.shape_hash      = ShapeHash{bench_rand()};
    te.scope_hash      = ScopeHash{bench_rand()};
    te.callsite_hash   = CallsiteHash{bench_rand()};
    te.num_inputs      = n_in;
    te.num_outputs     = n_out;
    te.num_scalar_args = n_scalar;
    te.grad_enabled    = true;
    te.kernel_id       = CKernelId::OPAQUE;

    if (n_in > 0) {
        te.input_metas = arena.alloc_array<TensorMeta>(a, n_in);
        for (uint16_t j = 0; j < n_in; j++) {
            auto& m = te.input_metas[j];
            m.ndim        = ndim;
            m.dtype       = ScalarType::Float;
            m.device_type = DeviceType::CUDA;
            m.device_idx  = 0;
            for (uint8_t d = 0; d < ndim; d++) {
                m.sizes[d]   = static_cast<int64_t>(32 + (bench_rand() % 256));
                m.strides[d] = static_cast<int64_t>(1 + (bench_rand() % 64));
            }
            m.data_ptr = reinterpret_cast<void*>(
                0x7f0000000000ULL + bench_rand() % 0x100000);
        }
    }
    if (n_out > 0) {
        te.output_metas = arena.alloc_array<TensorMeta>(a, n_out);
        for (uint16_t j = 0; j < n_out; j++) {
            auto& m = te.output_metas[j];
            m.ndim        = ndim;
            m.dtype       = ScalarType::Float;
            m.device_type = DeviceType::CUDA;
            m.device_idx  = 0;
            for (uint8_t d = 0; d < ndim; d++) {
                m.sizes[d]   = static_cast<int64_t>(32 + (bench_rand() % 256));
                m.strides[d] = static_cast<int64_t>(1 + (bench_rand() % 64));
            }
            m.data_ptr = reinterpret_cast<void*>(
                0x7f0000000000ULL + bench_rand() % 0x100000);
        }
    }
    if (n_scalar > 0) {
        te.scalar_args = arena.alloc_array<int64_t>(a, n_scalar);
        for (uint16_t s = 0; s < n_scalar; s++)
            te.scalar_args[s] = static_cast<int64_t>(bench_rand());
    }
    return te;
}

} // namespace

int main(int argc, char* argv[]) {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== merkle_dag ===\n\n");

    std::vector<bench::Report> reports;
    reports.reserve(16);

    // ── compute_content_hash at three region sizes ───────────────────
    reports.push_back([]{
        fx::Test test;
        Arena arena{1 << 16};
        constexpr uint32_t N = 8;
        TraceEntry ops[N]{};
        bench_rng_state = 0xDEADBEEFCAFEBABEULL;
        for (uint32_t i = 0; i < N; i++)
            ops[i] = make_synthetic_entry(test.alloc, arena, 2, 1, 2, 4);
        return bench::run("content_hash (8 ops, 2in/1out, 4d)", [&]{
            auto h = compute_content_hash(std::span{ops, N});
            bench::do_not_optimize(h);
        });
    }());

    reports.push_back([]{
        fx::Test test;
        Arena arena{1 << 20};
        constexpr uint32_t N = 481;
        auto* ops = arena.alloc_array<TraceEntry>(test.alloc, N);
        bench_rng_state = 0x12345678ABCDEF01ULL;
        for (uint32_t i = 0; i < N; i++) {
            const uint16_t n_in  = static_cast<uint16_t>(1 + (bench_rand() % 3));
            const uint16_t n_out = static_cast<uint16_t>(1 + (bench_rand() % 2));
            ops[i] = make_synthetic_entry(test.alloc, arena, n_in, n_out, 1, 4);
        }
        return bench::run("content_hash (481 ops, ResNet-like)", [&]{
            auto h = compute_content_hash(std::span{ops, N});
            bench::do_not_optimize(h);
        });
    }());

    reports.push_back([]{
        fx::Test test;
        Arena arena{1 << 20};
        constexpr uint32_t N = 1110;
        auto* ops = arena.alloc_array<TraceEntry>(test.alloc, N);
        bench_rng_state = 0xABCDEF0123456789ULL;
        for (uint32_t i = 0; i < N; i++) {
            const uint16_t n_in  = static_cast<uint16_t>(1 + (bench_rand() % 4));
            const uint16_t n_out = static_cast<uint16_t>(1 + (bench_rand() % 2));
            ops[i] = make_synthetic_entry(test.alloc, arena, n_in, n_out, 2, 4);
        }
        return bench::run("content_hash (1110 ops, GPT-like)", [&]{
            auto h = compute_content_hash(std::span{ops, N});
            bench::do_not_optimize(h);
        });
    }());

    // ── make_region (481 ops) ────────────────────────────────────────
    reports.push_back([]{
        fx::Test test;
        Arena data_arena{1 << 20};
        constexpr uint32_t N = 481;
        auto* ops = data_arena.alloc_array<TraceEntry>(test.alloc, N);
        bench_rng_state = 0x12345678ABCDEF01ULL;
        for (uint32_t i = 0; i < N; i++)
            ops[i] = make_synthetic_entry(test.alloc, data_arena, 2, 1, 1, 4);
        return bench::run("make_region (481 ops)", [&]{
            Arena region_arena{1 << 20};
            auto* r = make_region(test.alloc, region_arena, ops, N);
            bench::do_not_optimize(r);
        });
    }());

    // ── compute_merkle_hash ──────────────────────────────────────────
    reports.push_back([]{
        fx::Test test;
        Arena arena{1 << 20};
        constexpr uint32_t N = 481;
        auto* ops = arena.alloc_array<TraceEntry>(test.alloc, N);
        bench_rng_state = 0xAAAABBBBCCCCDDDDULL;
        for (uint32_t i = 0; i < N; i++)
            ops[i] = make_synthetic_entry(test.alloc, arena, 2, 1, 1, 4);
        auto* region = make_region(test.alloc, arena, ops, N);
        return bench::run("compute_merkle_hash (RegionNode)", [&]{
            auto h = compute_merkle_hash(region);
            bench::do_not_optimize(h);
        });
    }());

    // ── build_csr (481 ops, 900 edges) ────────────────────────────────
    reports.push_back([]{
        fx::Test test;
        Arena arena{1 << 20};
        constexpr uint32_t NUM_OPS   = 481;
        constexpr uint32_t NUM_EDGES = 900;
        auto* edges = arena.alloc_array<Edge>(test.alloc, NUM_EDGES);
        bench_rng_state = 0x5555666677778888ULL;
        for (uint32_t e = 0; e < NUM_EDGES; e++) {
            const uint32_t src = static_cast<uint32_t>(bench_rand() % NUM_OPS);
            const uint32_t dst = std::min(
                src + 1 + static_cast<uint32_t>(bench_rand() % 5),
                NUM_OPS - 1);
            edges[e] = {
                .src = OpIndex{src}, .dst = OpIndex{dst},
                .src_port = 0, .dst_port = 0,
                .kind = EdgeKind::DATA_FLOW, .pad = 0,
            };
        }
        return bench::run("build_csr (481 ops, 900 edges)", [&]{
            Arena csr_arena{1 << 16};
            auto* graph = csr_arena.alloc_obj<TraceGraph>(test.alloc);
            graph->ops     = nullptr;
            graph->num_ops = NUM_OPS;
            build_csr(test.alloc, csr_arena, graph, edges, NUM_EDGES, NUM_OPS);
            bench::do_not_optimize(graph);
        });
    }());

    // ── compute_memory_plan (300 slots) ──────────────────────────────
    reports.push_back([]{
        fx::Test test;
        Arena arena{1 << 20};
        constexpr uint32_t NUM_SLOTS = 300;
        auto* slots = arena.alloc_array<TensorSlot>(test.alloc, NUM_SLOTS);
        bench_rng_state = 0x9999AAAABBBBCCCCULL;
        for (uint32_t s = 0; s < NUM_SLOTS; s++) {
            slots[s].slot_id     = SlotId{s};
            slots[s].birth_op    = OpIndex{static_cast<uint32_t>(bench_rand() % 400)};
            slots[s].death_op    = OpIndex{slots[s].birth_op.raw()
                                         + static_cast<uint32_t>(1 + bench_rand() % 80)};
            slots[s].nbytes      = 256 * (1 + bench_rand() % 64);
            slots[s].dtype       = ScalarType::Float;
            slots[s].device_type = DeviceType::CUDA;
            slots[s].device_idx  = 0;
            slots[s].is_external = (s < 20);  // first 20 are params
        }
        BackgroundThread bg;
        return bench::run("compute_memory_plan (300 slots)", [&]{
            bg.arena.~Arena();
            new (&bg.arena) Arena{1 << 20};
            auto* s = bg.arena.alloc_array<TensorSlot>(test.alloc, NUM_SLOTS);
            std::memcpy(s, slots, NUM_SLOTS * sizeof(TensorSlot));
            auto* plan = bg.compute_memory_plan(test.alloc, s, NUM_SLOTS);
            bench::do_not_optimize(plan);
        });
    }());

    // ── build_trace full pipeline (481 synthetic ops) ────────────────
    reports.push_back([]{
        fx::Test test;

        TraceRing ring;
        ring.reset();
        MetaLog meta_log;
        meta_log.reset();

        bench_rng_state = 0x1111222233334444ULL;
        constexpr uint32_t N = 481;

        for (uint32_t i = 0; i < N; i++) {
            TraceRing::Entry e{};
            e.schema_hash     = SchemaHash{bench_rand()};
            e.shape_hash      = ShapeHash{bench_rand()};
            e.num_inputs      = static_cast<uint16_t>(1 + (bench_rand() % 3));
            e.num_outputs     = static_cast<uint16_t>(1 + (bench_rand() % 2));
            e.num_scalar_args = static_cast<uint16_t>(bench_rand() % 3);
            e.grad_enabled    = true;

            const uint16_t total_metas = e.num_inputs + e.num_outputs;
            TensorMeta metas[8]{};
            for (uint16_t j = 0; j < total_metas && j < 8; j++) {
                metas[j].ndim        = 4;
                metas[j].dtype       = ScalarType::Float;
                metas[j].device_type = DeviceType::CUDA;
                metas[j].device_idx  = 0;
                for (uint8_t d = 0; d < 4; d++) {
                    metas[j].sizes[d]   = static_cast<int64_t>(32 + (bench_rand() % 256));
                    metas[j].strides[d] = static_cast<int64_t>(1 + (bench_rand() % 64));
                }
                metas[j].data_ptr = reinterpret_cast<void*>(
                    0x7f0000000000ULL + (i * 8 + j) * 0x1000);
            }
            MetaIndex meta_start = meta_log.try_append(metas, total_metas);
            ring.try_append(e, meta_start,
                            ScopeHash{bench_rand()},
                            CallsiteHash{bench_rand()});
        }

        BackgroundThread bg;
        bg.ring.set(&ring);
        bg.meta_log.set(&meta_log);

        // Drain buffers heap-allocated; 4× 4096-element arrays on the
        // stack blow past our 512 kB stack-usage budget.
        std::vector<TraceRing::Entry> batch(4096);
        std::vector<MetaIndex>        meta_batch(4096);
        std::vector<ScopeHash>        scope_batch(4096);
        std::vector<CallsiteHash>     callsite_batch(4096);
        const uint32_t n = ring.drain(batch.data(), 4096, meta_batch.data(),
                                      scope_batch.data(), callsite_batch.data());

        std::vector<TraceRing::Entry> saved_trace(batch.begin(),
                                                   batch.begin() + n);
        std::vector<MetaIndex>        saved_meta(meta_batch.begin(),
                                                  meta_batch.begin() + n);
        std::vector<ScopeHash>        saved_scope(scope_batch.begin(),
                                                   scope_batch.begin() + n);
        std::vector<CallsiteHash>     saved_callsite(callsite_batch.begin(),
                                                      callsite_batch.begin() + n);

        auto repopulate_meta_log = [&]{
            meta_log.reset();
            bench_rng_state = 0x1111222233334444ULL;
            for (uint32_t i = 0; i < n; i++) {
                const uint16_t total_metas = saved_trace[i].num_inputs
                                           + saved_trace[i].num_outputs;
                TensorMeta metas[8]{};
                for (uint16_t j = 0; j < total_metas && j < 8; j++) {
                    metas[j].ndim        = 4;
                    metas[j].dtype       = ScalarType::Float;
                    metas[j].device_type = DeviceType::CUDA;
                    metas[j].data_ptr = reinterpret_cast<void*>(
                        0x7f0000000000ULL + (i * 8 + j) * 0x1000);
                }
                meta_log.try_append(metas, total_metas);
            }
        };

        return bench::run("build_trace (481 ops, full pipeline)", [&]{
            bg.current_trace           = saved_trace;
            bg.current_meta_starts     = saved_meta;
            bg.current_scope_hashes    = saved_scope;
            bg.current_callsite_hashes = saved_callsite;
            bg.arena.~Arena();
            new (&bg.arena) Arena{1 << 20};
            repopulate_meta_log();

            auto* graph = bg.build_trace(test.alloc, N);
            bench::do_not_optimize(graph);
        });
    }());

    // ── Optional: build_trace from real .crtrace argv paths ──────────
    for (int i = 1; i < argc; i++) {
        reports.push_back([path = argv[i]]{
            fx::Test test;
            auto trace = load_trace(path);
            if (!trace) {
                std::fprintf(stderr, "[skip] could not load %s\n", path);
                return bench::run("build_trace (skipped — load fail)",
                                  []{ asm volatile("" ::: "memory"); });
            }
            std::fprintf(stderr, "[trace] %s: %u ops, %u metas\n",
                         path, trace->num_ops, trace->num_metas);

            MetaLog meta_log;
            meta_log.reset();
            auto repopulate = [&]{
                meta_log.reset();
                uint32_t cursor = 0;
                for (uint32_t i = 0; i < trace->num_ops; i++) {
                    const uint16_t n = trace->entries[i].num_inputs
                                     + trace->entries[i].num_outputs;
                    if (n > 0 && cursor + n <= trace->num_metas) {
                        meta_log.try_append(&trace->metas[cursor], n);
                        cursor += n;
                    }
                }
            };

            BackgroundThread bg;
            bg.meta_log.set(&meta_log);
            const size_t arena_bytes = std::max(
                size_t{1} << 20,
                static_cast<size_t>(trace->num_ops) * 256);

            char label[128];
            std::snprintf(label, sizeof(label),
                          "build_trace (%u ops, from %s)",
                          trace->num_ops, path);
            return bench::run(label, [&]{
                bg.current_trace.assign(trace->entries.begin(),
                                        trace->entries.end());
                bg.current_meta_starts.assign(trace->meta_starts.begin(),
                                              trace->meta_starts.end());
                bg.current_scope_hashes.assign(trace->scope_hashes.begin(),
                                               trace->scope_hashes.end());
                bg.current_callsite_hashes.assign(trace->callsite_hashes.begin(),
                                                  trace->callsite_hashes.end());
                bg.arena.~Arena();
                new (&bg.arena) Arena{arena_bytes};
                repopulate();
                auto* graph = bg.build_trace(test.alloc, trace->num_ops);
                bench::do_not_optimize(graph);
            });
        }());
    }

    bench::emit_reports_text(reports);

    // Scaling check: content_hash goes 8 → 481 → 1110 ops.
    // Expected roughly linear since every op does the same work.
    std::printf("\n=== compare — content_hash scaling ===\n");
    std::printf("  481 / 8   (expect ~60×):\n  ");
    bench::compare(reports[0], reports[1]).print_text(stdout);
    std::printf("  1110 / 481 (expect ~2.3×):\n  ");
    bench::compare(reports[1], reports[2]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
