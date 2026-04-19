// Arena allocator — per-sample tail-latency benchmark.
//
// Auto-batching is enabled (bench_harness default) because every Arena
// op is sub-5 ns on modern x86; rdtsc would dominate per-op timing.
// Percentiles are over batch means, flagged as "[batch-avg]" in output.

#include <crucible/Arena.h>
#include <crucible/Effects.h>

#include "bench_harness.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static int env_core() {
    if (const char* s = std::getenv("CRUCIBLE_BENCH_CORE"))
        return static_cast<int>(std::strtol(s, nullptr, 10));
    return -1;
}

static bool env_json() {
    const char* s = std::getenv("CRUCIBLE_BENCH_JSON");
    return s && s[0] && std::string(s) != "0";
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const int  core = env_core();
    const bool json = env_json();

    auto run = [&](std::string name, auto&& body) {
        return bench::Run(std::move(name)).core(core).measure(body);
    };

    std::printf("=== arena ===\n");

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
            crucible::Arena arena(4096);
            crucible::fx::Test test;
            return bench::Run("arena.alloc(8192) slow-path")
                     .core(core).samples(20'000).warmup(100)
                     .measure([&]{
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
