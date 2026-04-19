// TraceRing SPSC hot path — per-sample tail-latency benchmark.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <crucible/TraceRing.h>

#include "bench_harness.h"

// TODO: move to bench_harness.h::env
[[nodiscard]] static const char* env(const char* name) noexcept {
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
        // r.core() is [[nodiscard]] for the fluent builder idiom; here we
        // mutate r in place and ignore the returned self-reference.
        if (core >= 0) (void)r.core(core);   // else Pin::Auto default
        return r.measure(body);
    };

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
            // NOTE: body performs ring.reset() inside the timed region when
            // the ring saturates; tail samples (p99.9, max) therefore include
            // occasional reset costs, not pure try_append cost. Label mirrors
            // this trade-off.
            return run("ring.try_append (+reset-on-full)", [&]{
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
            // Same (+reset-on-full) trade-off as above; entry stays constant
            // so we measure the pure hot-path cost without ++h.
            return run("ring.try_append (+reset-on-full, const entry)", [&]{
                const bool ok = ring.try_append(e);
                bench::do_not_optimize(ok);
                if (!ok) ring.reset();
            });
        }(),
        [&]{
            crucible::TraceRing ring;
            return run("ring.size()", [&]{
                const auto s = ring.size();
                bench::do_not_optimize(s);
            });
        }(),
        [&]{
            crucible::TraceRing ring;
            return run("ring.total_produced()", [&]{
                const auto s = ring.total_produced();
                bench::do_not_optimize(s);
            });
        }(),
        [&]{
            crucible::TraceRing ring;
            return run("ring.reset()", [&]{ ring.reset(); });
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
