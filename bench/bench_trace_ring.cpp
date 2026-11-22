// TraceRing SPSC hot path — per-sample tail-latency benchmark.

#include <cstdint>

#include <crucible/TraceRing.h>

#include "bench_harness.h"

namespace {

// The allocation capability of the ring of each row.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== trace_ring ===\n");
    bench::print("  Entry size: {} bytes (expect 64)\n", sizeof(crucible::TraceRing::Entry));
    bench::print("  Ring capacity: {} entries\n", crucible::TraceRing::CAPACITY);
    bench::print("\n");

    // A TraceRing is 6 MiB and the stack is 8 MiB.  Each ring lives on
    // the heap, as it does in Vigil.  The address of the ring has no
    // effect on the timed loop.  Each timed body reads the ring through a
    // Ref, so no test of the Box is in the timed loop.
    ::fixy::FixedArray<bench::Report, 5> reports{
        [&] {
            ::fixy::Box<crucible::TraceRing> ring_box = ::fixy::mint_box<crucible::TraceRing>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            crucible::TraceRing::Entry e{};
            std::uint64_t h = 0;
            // Note: body performs ring.reset() inside the timed region when
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
            ::fixy::Box<crucible::TraceRing> ring_box = ::fixy::mint_box<crucible::TraceRing>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
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
            ::fixy::Box<crucible::TraceRing> ring_box = ::fixy::mint_box<crucible::TraceRing>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            return bench::run("ring.size()", [&] {
                const auto s = ring->size();
                bench::do_not_optimize(s);
            });
        }(),
        [&] {
            ::fixy::Box<crucible::TraceRing> ring_box = ::fixy::mint_box<crucible::TraceRing>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            return bench::run("ring.total_produced()", [&] {
                const auto s = ring->total_produced();
                bench::do_not_optimize(s);
            });
        }(),
        [&] {
            ::fixy::Box<crucible::TraceRing> ring_box = ::fixy::mint_box<crucible::TraceRing>(kAlloc);
            auto const ring = ::fixy::to(*ring_box);
            return bench::run("ring.reset()", [&] { ring->reset(); });
        }(),
    };

    bench::emit_reports_text(reports.view());

    // A/B compare — size() vs total_produced() are both a single atomic
    // load; should be statistically indistinguishable.
    bench::print("\n=== compare ===\n");
    bench::compare(reports.at(::fixy::index<2>()), reports.at(::fixy::index<3>())).print_text();

    // Bootstrap CI on a tail percentile, on demand.
    const auto ci99 = reports.at(::fixy::index<0>()).ci(0.99);
    bench::print("  ring.try_append p99 95% CI: [{}, {}] ns\n", ::fixy::Fixed<2>{ci99.lo}, ::fixy::Fixed<2>{ci99.hi});

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
