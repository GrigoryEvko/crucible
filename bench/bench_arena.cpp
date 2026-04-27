// Arena allocator — per-sample tail-latency benchmark.
//
// Auto-batching is enabled (bench_harness default) because every Arena
// op is sub-5 ns on modern x86; rdtsc would dominate per-op timing.
// Percentiles are over batch means, flagged as "[batch-avg]" in output.

#include <cstdio>
#include <cstdlib>

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>

#include "bench_harness.h"

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    // bench::run() pins from CRUCIBLE_BENCH_CORE (else Pin::Auto default).

    // Oversized-request slow path: each alloc triggers a new block. Auto-
    // batching would hide the per-call block allocation cost (one block =
    // one call), so sample explicitly. Each 8KB alloc triggers brk/mmap;
    // 20k samples × ~1µs ≈ 20ms wall time.
    constexpr size_t kSlowPathSamples = 20'000;
    constexpr size_t kSlowPathWarmup  = 100;

    std::printf("=== arena ===\n");

    // Each entry below builds a fresh Arena + effects::Test, measures one op,
    // and moves the Report into the array (Report's copy ctor is deleted,
    // move ctor is `= default` — the IIFE-lambda pattern works via NRVO +
    // move-construction into the aggregate-init slot).
    bench::Report reports[] = {
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::effects::Test test;
            return bench::run("arena.alloc(8)", [&]{
                auto* p = arena.alloc(test.alloc,
                    crucible::safety::Positive<size_t>{8});
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::effects::Test test;
            return bench::run("arena.alloc(64)", [&]{
                auto* p = arena.alloc(test.alloc,
                    crucible::safety::Positive<size_t>{64});
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::effects::Test test;
            return bench::run("arena.alloc(64, align=64)", [&]{
                auto* p = arena.alloc(test.alloc,
                    crucible::safety::Positive<size_t>{64},
                    crucible::safety::PowerOfTwo<size_t>{64});
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::effects::Test test;
            return bench::run("arena.alloc_obj<uint64_t>", [&]{
                auto* p = arena.alloc_obj<uint64_t>(test.alloc);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::effects::Test test;
            return bench::run("arena.alloc_array<uint64_t>(100)", [&]{
                auto* p = arena.alloc_array<uint64_t>(test.alloc, 100);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::effects::Test test;
            return bench::run("arena.alloc_array<uint64_t>(0) nullptr", [&]{
                auto* p = arena.alloc_array<uint64_t>(test.alloc, 0);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            // Oversized-request slow path: each alloc triggers a new block.
            // Explicit sample/warmup counts — see kSlowPathSamples above.
            // Fluent-builder form because we override samples/warmup; the
            // one-shot bench::run(name, body) helper has no override hook.
            crucible::Arena arena(4096);
            crucible::effects::Test test;
            auto r = bench::Run("arena.alloc(8192) slow-path")
                         .samples(kSlowPathSamples).warmup(kSlowPathWarmup);
            if (const int c = bench::env_core(); c >= 0) (void)r.core(c);
            return r.measure([&]{
                auto* p = arena.alloc(test.alloc,
                    crucible::safety::Positive<size_t>{8192});
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::effects::Test test;
            return bench::run("arena.copy_string(\"relu\")", [&]{
                auto* p = arena.copy_string(test.alloc, "relu");
                bench::do_not_optimize(p);
            });
        }(),
    };

    bench::emit_reports(reports, json);
    return 0;
}
