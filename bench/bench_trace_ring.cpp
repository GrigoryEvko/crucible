// TraceRing SPSC hot path — per-sample tail-latency benchmark.

#include <cstdio>
#include <memory>

#include <crucible/TraceRing.h>

#include "bench_harness.h"

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== trace_ring ===\n");
    std::printf("  Entry size: %zu bytes (expect 64)\n", sizeof(crucible::TraceRing::Entry));
    std::printf("  Ring capacity: %u entries\n", crucible::TraceRing::CAPACITY);
    std::printf("\n");

    // A TraceRing is 6 MiB and the stack is 8 MiB.  Each ring lives on
    // the heap, as it does in Vigil.  The address of the ring has no
    // effect on the timed loop.
    bench::Report reports[] = {
        [&] {
            auto ring = std::make_unique<crucible::TraceRing>();
            crucible::TraceRing::Entry e{};
            uint64_t h = 0;
            // NOTE: body performs ring.reset() inside the timed region when
            // the ring saturates; tail samples (p99.9, max) therefore include
            // occasional reset costs, not pure try_append cost. Label mirrors
            // this trade-off.
            return bench::run("ring.try_append (+reset-on-full)", [&] {
                e.schema_hash = crucible::SchemaHash{++h};
                const bool ok = ring->try_append(e);
                bench::do_not_optimize(ok);
                if (!ok) ring->reset();  // prevent full-ring saturation
            });
        }(),
        [&] {
            auto ring = std::make_unique<crucible::TraceRing>();
            crucible::TraceRing::Entry e{};
            e.schema_hash = crucible::SchemaHash{0xABCDEF};
            // Same (+reset-on-full) trade-off as above; entry stays constant
            // so we measure the pure hot-path cost without ++h.
            return bench::run("ring.try_append (+reset-on-full, const entry)", [&] {
                const bool ok = ring->try_append(e);
                bench::do_not_optimize(ok);
                if (!ok) ring->reset();
            });
        }(),
        [&] {
            auto ring = std::make_unique<crucible::TraceRing>();
            return bench::run("ring.size()", [&] {
                const auto s = ring->size();
                bench::do_not_optimize(s);
            });
        }(),
        [&] {
            auto ring = std::make_unique<crucible::TraceRing>();
            return bench::run("ring.total_produced()", [&] {
                const auto s = ring->total_produced();
                bench::do_not_optimize(s);
            });
        }(),
        [&] {
            auto ring = std::make_unique<crucible::TraceRing>();
            return bench::run("ring.reset()", [&] { ring->reset(); });
        }(),
    };

    bench::emit_reports_text(reports);

    // A/B compare — size() vs total_produced() are both a single atomic
    // load; should be statistically indistinguishable.
    std::printf("\n=== compare ===\n");
    bench::compare(reports[2], reports[3]).print_text(stdout);

    // Bootstrap CI on a tail percentile, on demand.
    const auto ci99 = reports[0].ci(0.99);
    std::printf("  ring.try_append p99 95%% CI: [%.2f, %.2f] ns\n", ci99.lo, ci99.hi);

    bench::emit_reports_json(reports, json);
    return 0;
}
