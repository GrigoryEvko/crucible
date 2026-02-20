// Benchmark: MerkleDag + BackgroundThread build_trace hot paths.
//
// Measures:
//   - compute_content_hash() over realistic op sequences
//   - build_trace() full pipeline (arena alloc, PtrMap, slot tracking, CSR)
//   - on_iteration_boundary() including tail retention
//   - make_region() construction
//
// Simulates ResNet-18 (481 ops) and GPT-4L (1110 ops) iteration sizes.

#include "bench_harness.h"

#include <crucible/Arena.h>
#include <crucible/BackgroundThread.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceLoader.h>
#include <crucible/TraceRing.h>

#include <cstdio>
#include <cstring>

using namespace crucible;

// ── Synthetic data generation ───────────────────────────────────────

// Deterministic PRNG for reproducible benchmarks.
static uint64_t bench_rng_state = 0xDEADBEEFCAFEBABEULL;
static uint64_t bench_rand() {
    bench_rng_state ^= bench_rng_state << 13;
    bench_rng_state ^= bench_rng_state >> 7;
    bench_rng_state ^= bench_rng_state << 17;
    return bench_rng_state;
}

// Create a realistic TraceEntry with arena-allocated metas.
static TraceEntry make_synthetic_entry(
    Arena& arena, uint16_t n_in, uint16_t n_out, uint16_t n_scalar, uint8_t ndim)
{
    TraceEntry te{};
    te.schema_hash = SchemaHash{bench_rand()};
    te.shape_hash = ShapeHash{bench_rand()};
    te.scope_hash = ScopeHash{bench_rand()};
    te.callsite_hash = CallsiteHash{bench_rand()};
    te.num_inputs = n_in;
    te.num_outputs = n_out;
    te.num_scalar_args = n_scalar;
    te.grad_enabled = true;
    te.kernel_id = CKernelId::OPAQUE;

    if (n_in > 0) {
        te.input_metas = arena.alloc_array<TensorMeta>(n_in);
        for (uint16_t j = 0; j < n_in; j++) {
            auto& m = te.input_metas[j];
            m.ndim = ndim;
            m.dtype = ScalarType::Float;
            m.device_type = DeviceType::CUDA;
            m.device_idx = 0;
            for (uint8_t d = 0; d < ndim; d++) {
                m.sizes[d] = static_cast<int64_t>(32 + (bench_rand() % 256));
                m.strides[d] = static_cast<int64_t>(1 + (bench_rand() % 64));
            }
            m.data_ptr = reinterpret_cast<void*>(0x7f0000000000ULL + bench_rand() % 0x100000);
        }
    }

    if (n_out > 0) {
        te.output_metas = arena.alloc_array<TensorMeta>(n_out);
        for (uint16_t j = 0; j < n_out; j++) {
            auto& m = te.output_metas[j];
            m.ndim = ndim;
            m.dtype = ScalarType::Float;
            m.device_type = DeviceType::CUDA;
            m.device_idx = 0;
            for (uint8_t d = 0; d < ndim; d++) {
                m.sizes[d] = static_cast<int64_t>(32 + (bench_rand() % 256));
                m.strides[d] = static_cast<int64_t>(1 + (bench_rand() % 64));
            }
            m.data_ptr = reinterpret_cast<void*>(0x7f0000000000ULL + bench_rand() % 0x100000);
        }
    }

    if (n_scalar > 0) {
        te.scalar_args = arena.alloc_array<int64_t>(n_scalar);
        for (uint16_t s = 0; s < n_scalar; s++)
            te.scalar_args[s] = static_cast<int64_t>(bench_rand());
    }

    return te;
}

// ── Benchmarks ───────────────────────────────────────────────────────

static void bench_content_hash_small() {
    std::printf("\n--- compute_content_hash ---\n");

    // Small region: 8 ops, 2 inputs/1 output each, 4 dims.
    Arena arena{1 << 16};
    constexpr uint32_t N = 8;
    TraceEntry ops[N];
    bench_rng_state = 0xDEADBEEFCAFEBABEULL;
    for (uint32_t i = 0; i < N; i++)
        ops[i] = make_synthetic_entry(arena, 2, 1, 2, 4);

    BENCH("content_hash (8 ops, 2in/1out, 4d)", 100'000, {
        auto h = compute_content_hash(std::span{ops, N});
        bench::DoNotOptimize(h);
    });
}

static void bench_content_hash_resnet() {
    // ResNet-like: 481 ops, varying input counts.
    Arena arena{1 << 20};
    constexpr uint32_t N = 481;
    auto* ops = arena.alloc_array<TraceEntry>(N);
    bench_rng_state = 0x12345678ABCDEF01ULL;
    for (uint32_t i = 0; i < N; i++) {
        uint16_t n_in = static_cast<uint16_t>(1 + (bench_rand() % 3));
        uint16_t n_out = static_cast<uint16_t>(1 + (bench_rand() % 2));
        ops[i] = make_synthetic_entry(arena, n_in, n_out, 1, 4);
    }

    BENCH("content_hash (481 ops, ResNet-like)", 1'000, {
        auto h = compute_content_hash(std::span{ops, N});
        bench::DoNotOptimize(h);
    });
}

static void bench_content_hash_gpt() {
    // GPT-like: 1110 ops, more inputs (attention has many).
    Arena arena{1 << 20};
    constexpr uint32_t N = 1110;
    auto* ops = arena.alloc_array<TraceEntry>(N);
    bench_rng_state = 0xABCDEF0123456789ULL;
    for (uint32_t i = 0; i < N; i++) {
        uint16_t n_in = static_cast<uint16_t>(1 + (bench_rand() % 4));
        uint16_t n_out = static_cast<uint16_t>(1 + (bench_rand() % 2));
        ops[i] = make_synthetic_entry(arena, n_in, n_out, 2, 4);
    }

    BENCH("content_hash (1110 ops, GPT-like)", 1'000, {
        auto h = compute_content_hash(std::span{ops, N});
        bench::DoNotOptimize(h);
    });
}

static void bench_make_region() {
    std::printf("\n--- make_region ---\n");

    // Pre-build ops, then benchmark just region creation.
    Arena data_arena{1 << 20};
    constexpr uint32_t N = 481;
    auto* ops = data_arena.alloc_array<TraceEntry>(N);
    bench_rng_state = 0x12345678ABCDEF01ULL;
    for (uint32_t i = 0; i < N; i++)
        ops[i] = make_synthetic_entry(data_arena, 2, 1, 1, 4);

    BENCH("make_region (481 ops)", 10'000, {
        Arena region_arena{1 << 20};
        auto* r = make_region(region_arena, ops, N);
        bench::DoNotOptimize(r);
    });
}

static void bench_merkle_hash() {
    std::printf("\n--- compute_merkle_hash ---\n");

    Arena arena{1 << 20};
    constexpr uint32_t N = 481;
    auto* ops = arena.alloc_array<TraceEntry>(N);
    bench_rng_state = 0xAAAABBBBCCCCDDDDULL;
    for (uint32_t i = 0; i < N; i++)
        ops[i] = make_synthetic_entry(arena, 2, 1, 1, 4);

    auto* region = make_region(arena, ops, N);

    BENCH("compute_merkle_hash (RegionNode)", 1'000'000, {
        auto h = compute_merkle_hash(region);
        bench::DoNotOptimize(h);
    });
}

static void bench_build_trace() {
    std::printf("\n--- build_trace (full pipeline) ---\n");

    // We can't easily benchmark build_trace directly because it reads from
    // current_trace vectors and MetaLog. Instead, simulate the setup.

    // Setup: populate ring + meta_log with synthetic data, then let
    // BackgroundThread process it.
    TraceRing ring;
    ring.reset();
    MetaLog meta_log;
    meta_log.reset();

    // Populate ring with N ops.
    bench_rng_state = 0x1111222233334444ULL;
    constexpr uint32_t N = 481;

    for (uint32_t i = 0; i < N; i++) {
        TraceRing::Entry e{};
        e.schema_hash = SchemaHash{bench_rand()};
        e.shape_hash = ShapeHash{bench_rand()};
        e.num_inputs = static_cast<uint16_t>(1 + (bench_rand() % 3));
        e.num_outputs = static_cast<uint16_t>(1 + (bench_rand() % 2));
        e.num_scalar_args = static_cast<uint16_t>(bench_rand() % 3);
        e.grad_enabled = true;

        // Write metas to MetaLog.
        uint16_t total_metas = e.num_inputs + e.num_outputs;
        TensorMeta metas[8]{};
        for (uint16_t j = 0; j < total_metas && j < 8; j++) {
            metas[j].ndim = 4;
            metas[j].dtype = ScalarType::Float;
            metas[j].device_type = DeviceType::CUDA;
            metas[j].device_idx = 0;
            for (uint8_t d = 0; d < 4; d++) {
                metas[j].sizes[d] = static_cast<int64_t>(32 + (bench_rand() % 256));
                metas[j].strides[d] = static_cast<int64_t>(1 + (bench_rand() % 64));
            }
            metas[j].data_ptr = reinterpret_cast<void*>(0x7f0000000000ULL + (i * 8 + j) * 0x1000);
        }

        MetaIndex meta_start = meta_log.try_append(metas, total_metas);
        ring.try_append(e, meta_start, ScopeHash{bench_rand()}, CallsiteHash{bench_rand()});
    }

    // Drain into BackgroundThread's vectors, then benchmark build_trace.
    BackgroundThread bg;
    bg.ring = &ring;
    bg.meta_log = &meta_log;

    TraceRing::Entry batch[4096];
    MetaIndex meta_batch[4096];
    ScopeHash scope_batch[4096];
    CallsiteHash callsite_batch[4096];
    uint32_t n = ring.drain(batch, 4096, meta_batch, scope_batch, callsite_batch);

    // Store drained data for repeated benchmarking.
    std::vector<TraceRing::Entry> saved_trace(batch, batch + n);
    std::vector<MetaIndex> saved_meta(meta_batch, meta_batch + n);
    std::vector<ScopeHash> saved_scope(scope_batch, scope_batch + n);
    std::vector<CallsiteHash> saved_callsite(callsite_batch, callsite_batch + n);

    // Populate MetaLog fully so build_trace can read from it.
    auto repopulate_meta_log = [&]() {
        meta_log.reset();
        bench_rng_state = 0x1111222233334444ULL; // deterministic
        for (uint32_t i = 0; i < n; i++) {
            uint16_t total_metas = saved_trace[i].num_inputs + saved_trace[i].num_outputs;
            TensorMeta metas[8]{};
            for (uint16_t j = 0; j < total_metas && j < 8; j++) {
                metas[j].ndim = 4;
                metas[j].dtype = ScalarType::Float;
                metas[j].device_type = DeviceType::CUDA;
                metas[j].data_ptr = reinterpret_cast<void*>(0x7f0000000000ULL + (i * 8 + j) * 0x1000);
            }
            meta_log.try_append(metas, total_metas);
        }
    };

    BENCH("build_trace (481 ops, full pipeline)", 1'000, {
        // Reset bg state for each iteration.
        bg.current_trace = saved_trace;
        bg.current_meta_starts = saved_meta;
        bg.current_scope_hashes = saved_scope;
        bg.current_callsite_hashes = saved_callsite;
        // Destroy and re-create arena (can't move-assign).
        bg.arena.~Arena();
        new (&bg.arena) Arena{1 << 20};

        repopulate_meta_log();

        auto* graph = bg.build_trace(N);
        bench::DoNotOptimize(graph);
    });
}

static void bench_build_csr() {
    std::printf("\n--- build_csr ---\n");

    // Synthetic edges for 481 ops with ~2 edges per op.
    Arena arena{1 << 20};
    constexpr uint32_t NUM_OPS = 481;
    constexpr uint32_t NUM_EDGES = 900;

    auto* edges = arena.alloc_array<Edge>(NUM_EDGES);
    bench_rng_state = 0x5555666677778888ULL;
    for (uint32_t e = 0; e < NUM_EDGES; e++) {
        uint32_t src = static_cast<uint32_t>(bench_rand() % NUM_OPS);
        uint32_t dst = std::min(src + 1 + static_cast<uint32_t>(bench_rand() % 5), NUM_OPS - 1);
        edges[e] = {OpIndex{src}, OpIndex{dst}, 0, 0, EdgeKind::DATA_FLOW, 0};
    }

    BENCH("build_csr (481 ops, 900 edges)", 10'000, {
        Arena csr_arena{1 << 16};
        auto* graph = csr_arena.alloc_obj<TraceGraph>();
        graph->ops = nullptr;
        graph->num_ops = NUM_OPS;
        build_csr(csr_arena, graph, edges, NUM_EDGES, NUM_OPS);
        bench::DoNotOptimize(graph);
    });
}

static void bench_memory_plan() {
    std::printf("\n--- compute_memory_plan ---\n");

    // Synthetic slots for 300 unique tensors.
    Arena arena{1 << 20};
    constexpr uint32_t NUM_SLOTS = 300;
    auto* slots = arena.alloc_array<TensorSlot>(NUM_SLOTS);

    bench_rng_state = 0x9999AAAABBBBCCCCULL;
    for (uint32_t s = 0; s < NUM_SLOTS; s++) {
        slots[s].slot_id = SlotId{s};
        slots[s].birth_op = OpIndex{static_cast<uint32_t>(bench_rand() % 400)};
        slots[s].death_op = OpIndex{slots[s].birth_op.raw() +
                                    static_cast<uint32_t>(1 + bench_rand() % 80)};
        slots[s].nbytes = 256 * (1 + bench_rand() % 64);
        slots[s].dtype = ScalarType::Float;
        slots[s].device_type = DeviceType::CUDA;
        slots[s].device_idx = 0;
        slots[s].is_external = (s < 20); // first 20 are external (params)
    }

    BackgroundThread bg;
    BENCH("compute_memory_plan (300 slots)", 10'000, {
        bg.arena.~Arena();
        new (&bg.arena) Arena{1 << 20};
        // Copy slots into bg arena.
        auto* s = bg.arena.alloc_array<TensorSlot>(NUM_SLOTS);
        std::memcpy(s, slots, NUM_SLOTS * sizeof(TensorSlot));
        auto* plan = bg.compute_memory_plan(s, NUM_SLOTS);
        bench::DoNotOptimize(plan);
    });
}

static void bench_build_trace_from_file(const char* path) {
    std::printf("\n--- build_trace (loaded from %s) ---\n", path);

    auto trace = load_trace(path);
    if (!trace) {
        std::printf("  SKIP: could not load %s\n", path);
        return;
    }

    std::printf("  %u ops, %u metas\n", trace->num_ops, trace->num_metas);

    // Setup: MetaLog with the loaded metas.
    MetaLog meta_log;
    meta_log.reset();

    // Pre-populate MetaLog with loaded metas.
    auto repopulate = [&]() {
        meta_log.reset();
        uint32_t cursor = 0;
        for (uint32_t i = 0; i < trace->num_ops; i++) {
            uint16_t n = trace->entries[i].num_inputs +
                         trace->entries[i].num_outputs;
            if (n > 0 && cursor + n <= trace->num_metas) {
                meta_log.try_append(&trace->metas[cursor], n);
                cursor += n;
            }
        }
    };

    BackgroundThread bg;
    bg.meta_log = &meta_log;

    char label[128];
    std::snprintf(label, sizeof(label),
                  "build_trace (%u ops, real ViT-B)", trace->num_ops);
    BENCH(label, 1'000, {
        bg.current_trace.assign(trace->entries.begin(), trace->entries.end());
        bg.current_meta_starts.assign(trace->meta_starts.begin(), trace->meta_starts.end());
        bg.current_scope_hashes.assign(trace->scope_hashes.begin(), trace->scope_hashes.end());
        bg.current_callsite_hashes.assign(trace->callsite_hashes.begin(), trace->callsite_hashes.end());
        bg.arena.~Arena();
        new (&bg.arena) Arena{1 << 20};
        repopulate();
        auto* graph = bg.build_trace(trace->num_ops);
        bench::DoNotOptimize(graph);
    });
}

int main(int argc, char* argv[]) {
    std::printf("=== Crucible MerkleDag Benchmark Suite ===\n");

    bench_content_hash_small();
    bench_content_hash_resnet();
    bench_content_hash_gpt();
    bench_make_region();
    bench_merkle_hash();
    bench_build_csr();
    bench_memory_plan();

    // build_trace is the big one — run last.
    bench_build_trace();

    // If a .crtrace file is given, benchmark with real trace data.
    if (argc > 1) {
        bench_build_trace_from_file(argv[1]);
    }

    std::printf("\nDone.\n");
    return 0;
}
