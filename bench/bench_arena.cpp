// Arena allocator — per-sample tail-latency benchmark.
//
// Auto-batching is enabled (bench_harness default) because every Arena
// op is sub-5 ns on modern x86; rdtsc would dominate per-op timing.
// Percentiles are over batch means, flagged as "[batch-avg]" in output.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <crucible/Arena.h>
#include <crucible/Effects.h>

#include "bench_harness.h"

// TODO: move to bench_harness.h::env
static const char* env(const char* name) noexcept {
    const char* s = std::getenv(name);
    return (s && s[0]) ? s : nullptr;
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const char* core_s = env("CRUCIBLE_BENCH_CORE");
    const int   core   = core_s ? static_cast<int>(std::strtol(core_s, nullptr, 10)) : -1;
    const char* json_s = env("CRUCIBLE_BENCH_JSON");
    const bool  json   = json_s && std::string(json_s) != "0";

    // Pin only if explicitly requested via env; else let harness auto-pick
    // (isolcpu if available, sched_getcpu() otherwise).
    auto run = [&](std::string name, auto&& body) {
        auto r = bench::Run(std::move(name));
        if (core >= 0) r.core(core);   // else Pin::Auto default
        return r.measure(body);
    };

    // Oversized-request slow path: each alloc triggers a new block. Auto-
    // batching would hide the per-call block allocation cost (one block =
    // one call), so sample explicitly. Each 8KB alloc triggers brk/mmap;
    // 20k samples × ~1µs ≈ 20ms wall time.
    constexpr size_t kSlowPathSamples = 20'000;
    constexpr size_t kSlowPathWarmup  = 100;

    std::printf("=== arena ===\n");

    // Each entry below builds a fresh Arena + fx::Test, measures one op,
    // and moves the Report into the array (Report's copy ctor is deleted,
    // move ctor is `= default` — the IIFE-lambda pattern works via NRVO +
    // move-construction into the aggregate-init slot).
    bench::Report reports[] = {
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::fx::Test test;
            return run("arena.alloc(8)", [&]{
                auto* p = arena.alloc(test.alloc, 8);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::fx::Test test;
            return run("arena.alloc(64)", [&]{
                auto* p = arena.alloc(test.alloc, 64);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::fx::Test test;
            return run("arena.alloc(64, align=64)", [&]{
                auto* p = arena.alloc(test.alloc, 64, 64);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::fx::Test test;
            return run("arena.alloc_obj<uint64_t>", [&]{
                auto* p = arena.alloc_obj<uint64_t>(test.alloc);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::fx::Test test;
            return run("arena.alloc_array<uint64_t>(100)", [&]{
                auto* p = arena.alloc_array<uint64_t>(test.alloc, 100);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::fx::Test test;
            return run("arena.alloc_array<uint64_t>(0) nullptr", [&]{
                auto* p = arena.alloc_array<uint64_t>(test.alloc, 0);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            // Oversized-request slow path: each alloc triggers a new block.
            // Explicit sample/warmup counts — see kSlowPathSamples above.
            crucible::Arena arena(4096);
            crucible::fx::Test test;
            auto r = bench::Run("arena.alloc(8192) slow-path")
                         .samples(kSlowPathSamples).warmup(kSlowPathWarmup);
            if (core >= 0) r.core(core);   // else Pin::Auto default
            return r.measure([&]{
                auto* p = arena.alloc(test.alloc, 8192);
                bench::do_not_optimize(p);
            });
        }(),
        [&]{
            crucible::Arena arena(1u << 24);
            crucible::fx::Test test;
            return run("arena.copy_string(\"relu\")", [&]{
                auto* p = arena.copy_string(test.alloc, "relu");
                bench::do_not_optimize(p);
            });
        }(),
    };

    for (const auto& r : reports) r.print_text(stdout);

    if (json) {
        std::printf("\n=== json ===\n[\n");
        for (size_t i = 0; i < sizeof(reports)/sizeof(reports[0]); ++i) {
            std::printf("  ");
            reports[i].print_json(stdout);
            std::printf("%s\n", (i + 1 < sizeof(reports)/sizeof(reports[0])) ? "," : "");
        }
        std::printf("]\n");
    }
    return 0;
}
