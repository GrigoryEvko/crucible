// Arena allocator benchmarks.
//
// Targets: alloc() <= 2ns, alloc_obj<T>() <= 3ns.
// Measures fast-path (within block) and slow-path (block exhaustion).

#include "bench_harness.h"
#include <crucible/Arena.h>

#include <crucible/Effects.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

// Test structs of various sizes for alloc_obj benchmarks.
struct Tiny { uint64_t a = 0; };                                    // 8B
struct CacheLine { uint64_t data[8] = {}; };                        // 64B
struct Medium { uint64_t data[32] = {}; };                          // 256B
struct Large { uint64_t data[128] = {}; };                          // 1024B

static_assert(sizeof(Tiny) == 8);
static_assert(sizeof(CacheLine) == 64);
static_assert(sizeof(Medium) == 256);
static_assert(sizeof(Large) == 1024);

int main() {
    crucible::fx::Test test;
    std::printf("=== Arena Allocator Benchmarks ===\n");
    std::printf("    Target: alloc() <= 2ns, alloc_obj<T>() <= 3ns\n\n");

    // ── Raw alloc() at various sizes ──
    std::printf("--- alloc(size, align=16) fast path ---\n");
    {
        // Use a large arena so we never hit the slow path during measurement.
        crucible::Arena arena(1 << 24);  // 16MB

        BENCH_CHECK("alloc(8)", 10'000'000, 1.2, {
            auto* p = arena.alloc(test.alloc, 8);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc(64)", 10'000'000, 1.2, {
            auto* p = arena.alloc(test.alloc, 64);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc(256)", 10'000'000, 1.2, {
            auto* p = arena.alloc(test.alloc, 256);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc(1024)", 5'000'000, 1.7, {
            auto* p = arena.alloc(test.alloc, 1024);
            bench::DoNotOptimize(p);
        });
    }

    // ── alloc() with alignment=1 (no padding) ──
    std::printf("\n--- alloc(size, align=1) no alignment padding ---\n");
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc(8, align=1)", 10'000'000, 0.8, {
            auto* p = arena.alloc(test.alloc, 8, 1);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc(64, align=1)", 10'000'000, 0.8, {
            auto* p = arena.alloc(test.alloc, 64, 1);
            bench::DoNotOptimize(p);
        });
    }

    // ── alloc() with 64B alignment (cache-line) ──
    std::printf("\n--- alloc(size, align=64) cache-line aligned ---\n");
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc(64, align=64)", 10'000'000, 1.2, {
            auto* p = arena.alloc(test.alloc, 64, 64);
            bench::DoNotOptimize(p);
        });
    }

    // ── Typed alloc_obj<T>() ──
    std::printf("\n--- alloc_obj<T>() typed allocation ---\n");
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc_obj<Tiny> (8B)", 10'000'000, 1.2, {
            auto* p = arena.alloc_obj<Tiny>(test.alloc);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc_obj<CacheLine> (64B)", 10'000'000, 1.2, {
            auto* p = arena.alloc_obj<CacheLine>(test.alloc);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc_obj<Medium> (256B)", 10'000'000, 1.4, {
            auto* p = arena.alloc_obj<Medium>(test.alloc);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc_obj<Large> (1024B)", 5'000'000, 1.7, {
            auto* p = arena.alloc_obj<Large>(test.alloc);
            bench::DoNotOptimize(p);
        });
    }

    // ── alloc_array<T>(n) ──
    std::printf("\n--- alloc_array<T>(n) bulk allocation ---\n");
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc_array<uint64_t>(8)", 10'000'000, 1.4, {
            auto* p = arena.alloc_array<uint64_t>(test.alloc, 8);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc_array<uint64_t>(100)", 5'000'000, 1.5, {
            auto* p = arena.alloc_array<uint64_t>(test.alloc, 100);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc_array<CacheLine>(10)", 5'000'000, 1.4, {
            auto* p = arena.alloc_array<CacheLine>(test.alloc, 10);
            bench::DoNotOptimize(p);
        });
    }

    // ── alloc_array<T>(0) edge case ──
    std::printf("\n--- alloc_array<T>(0) nullptr fast path ---\n");
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("alloc_array<uint64_t>(0)", 10'000'000, 0.3, {
            auto* p = arena.alloc_array<uint64_t>(test.alloc, 0);
            bench::DoNotOptimize(p);
        });
    }

    // ── copy_string() ──
    std::printf("\n--- copy_string() ---\n");
    {
        crucible::Arena arena(1 << 24);
        const char* short_str = "relu";
        BENCH_CHECK("copy_string(4 chars)", 5'000'000, 2.9, {
            auto* p = arena.copy_string(test.alloc, short_str);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        const char* medium_str = "aten::native_layer_norm";
        BENCH_CHECK("copy_string(23 chars)", 5'000'000, 9.5, {
            auto* p = arena.copy_string(test.alloc, medium_str);
            bench::DoNotOptimize(p);
        });
    }
    {
        crucible::Arena arena(1 << 24);
        BENCH_CHECK("copy_string(nullptr)", 10'000'000, 0.3, {
            auto* p = arena.copy_string(test.alloc, nullptr);
            bench::DoNotOptimize(p);
        });
    }

    // ── Block exhaustion: measure slow-path amortized cost ──
    std::printf("\n--- Block exhaustion (slow path) ---\n");
    {
        // 4KB blocks, allocate 64B each -> 64 allocs per block.
        // After 64 allocs, the 65th triggers a new block.
        // Measure amortized cost including block switches.
        constexpr size_t BLOCK_SIZE = 4096;
        constexpr size_t ALLOC_SIZE = 64;
        constexpr size_t ALLOCS_PER_BLOCK = BLOCK_SIZE / ALLOC_SIZE;  // ~64
        constexpr uint64_t TOTAL_ALLOCS = 1'000'000;

        crucible::Arena arena(BLOCK_SIZE);
        BENCH_ROUNDS_CHECK("alloc(64) amortized w/ block switch", TOTAL_ALLOCS, 11, 25.5, {
            auto* p = arena.alloc(test.alloc, ALLOC_SIZE, 1);
            bench::DoNotOptimize(p);
        });

        std::printf("    (includes ~1 block switch per %zu allocs)\n", ALLOCS_PER_BLOCK);
    }

    // ── Sequential burst: many small allocs to simulate build_trace ──
    std::printf("\n--- Sequential burst (simulates build_trace) ---\n");
    {
        crucible::Arena arena(1 << 24);
        // Simulate the pattern from build_trace: many small allocs
        // of different sizes in sequence.
        BENCH_CHECK("burst: 8+64+16+64+16 bytes", 2'000'000, 5.7, {
            auto* a = arena.alloc(test.alloc, 8, 8);
            auto* b = arena.alloc(test.alloc, 64, 8);
            auto* c = arena.alloc(test.alloc, 16, 8);
            auto* d = arena.alloc(test.alloc, 64, 8);
            auto* e = arena.alloc(test.alloc, 16, 8);
            bench::DoNotOptimize(a);
            bench::DoNotOptimize(b);
            bench::DoNotOptimize(c);
            bench::DoNotOptimize(d);
            bench::DoNotOptimize(e);
        });
    }

    // ── total_allocated / block_count query latency (pure getters) ──
    std::printf("\n--- total_allocated() / block_count() accessors ---\n");
    {
        crucible::Arena arena(1 << 16);
        for (int i = 0; i < 100; ++i) (void)arena.alloc(test.alloc, 128, 8);
        BENCH_CHECK("total_allocated()", 20'000'000, 0.5, {
            auto n = arena.total_allocated();
            bench::DoNotOptimize(n);
        });
        BENCH_CHECK("block_count()", 20'000'000, 0.5, {
            auto n = arena.block_count();
            bench::DoNotOptimize(n);
        });
    }

    // ── Baseline: malloc/free for comparison ──
    std::printf("\n--- Baseline: malloc/free comparison ---\n");
    {
        BENCH_CHECK("malloc(64)+free", 5'000'000, 6.5, {
            auto* p = std::malloc(64);
            bench::DoNotOptimize(p);
            std::free(p);
        });
    }
    {
        BENCH_CHECK("malloc(256)+free", 5'000'000, 6.6, {
            auto* p = std::malloc(256);
            bench::DoNotOptimize(p);
            std::free(p);
        });
    }

    std::printf("\nDone.\n");
    return 0;
}
