// TraceRing SPSC hot path — per-sample tail-latency benchmark.

#include <crucible/TraceRing.h>

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

    std::printf("=== trace_ring ===\n");
    std::printf("  Entry size: %zu bytes (expect 64)\n",
                sizeof(crucible::TraceRing::Entry));
    std::printf("  Ring capacity: %u entries\n",
                crucible::TraceRing::CAPACITY);
    std::printf("\n");

    bench::Report reports[] = {
        [&]{
            crucible::TraceRing ring;
            crucible::TraceRing::Entry e{};
            uint64_t h = 0;
            return bench::Run("ring.try_append (drain-amortized)")
                     .core(core)
                     .measure([&]{
                         e.schema_hash = crucible::SchemaHash{++h};
                         const bool ok = ring.try_append(e);
                         bench::do_not_optimize(ok);
                         if (!ok) ring.reset();  // prevent full-ring saturation
                     });
        }(),
        [&]{
            crucible::TraceRing ring;
            crucible::TraceRing::Entry e{};
            e.schema_hash = crucible::SchemaHash{0xABCDEF};
            return bench::Run("ring.try_append (defaults)")
                     .core(core)
                     .measure([&]{
                         const bool ok = ring.try_append(e);
                         bench::do_not_optimize(ok);
                         if (!ok) ring.reset();
                     });
        }(),
        [&]{
            crucible::TraceRing ring;
            return bench::Run("ring.size()")
                     .core(core)
                     .measure([&]{
                         const auto s = ring.size();
                         bench::do_not_optimize(s);
                     });
        }(),
        [&]{
            crucible::TraceRing ring;
            return bench::Run("ring.total_produced()")
                     .core(core)
                     .measure([&]{
                         const auto s = ring.total_produced();
                         bench::do_not_optimize(s);
                     });
        }(),
        [&]{
            crucible::TraceRing ring;
            return bench::Run("ring.reset()")
                     .core(core)
                     .measure([&]{ ring.reset(); });
        }(),
    };

    for (const auto& r : reports) r.print_text(stdout);

    // A/B compare — size() vs total_produced() are both a single atomic
    // load; should be statistically indistinguishable.
    const auto& r_size   = reports[2];
    const auto& r_totprd = reports[3];
    const auto cmp = bench::compare(r_size, r_totprd);
    std::printf("\n=== compare ===\n");
    cmp.print_text(stdout);

    // Bootstrap CI on a tail percentile, on demand.
    const auto ci99 = reports[0].ci(0.99);
    std::printf("  ring.try_append p99 95%% CI: [%.2f, %.2f] ns\n",
                ci99.lo, ci99.hi);

    if (json) {
        std::printf("\n=== json ===\n[\n");
        const size_t n = sizeof(reports)/sizeof(reports[0]);
        for (size_t i = 0; i < n; ++i) {
            std::printf("  ");
            reports[i].print_json(stdout);
            std::printf("%s\n", (i + 1 < n) ? "," : "");
        }
        std::printf("]\n");
    }
    return 0;
}
