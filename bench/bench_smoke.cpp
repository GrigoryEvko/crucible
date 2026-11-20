// harness smoke-test; minimal deterministic ops exercise the output path
// end-to-end without depending on Crucible library internals.

#include <atomic>

#include <foundation/Quarantine.h>

#include "bench_harness.h"

namespace {

// ── Tunables ───────────────────────────────────────────────────────────
// Magic numbers get named constants so they're documented + greppable.
constexpr uint64_t kXorMixConstant = 0x9E3779B97F4A7C15ULL;  // golden ratio (Knuth)
constexpr uint64_t kAddInitialSink = 0;  // seed for integer-increment body
constexpr uint64_t kXorInitialSink = 1;  // seed for xor body
constexpr uint64_t kAtomicInitialValue = 0;  // seed for relaxed-load body

}  // namespace

// The third row measures the relaxed load of std::atomic itself, the shape
// of an atomic read, so the atomic and its load stay raw.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the relaxed load of std::atomic is the row of the atomic read")
namespace {

[[nodiscard]] bench::Report atomic_load_row(int core) {
    static std::atomic<uint64_t> counter{kAtomicInitialValue};
    return bench::Run("atomic<u64>.load(relaxed)").core(core).measure([&] {
        const uint64_t v = counter.load(std::memory_order_relaxed);
        bench::do_not_optimize(v);
    });
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    // The harness reads CRUCIBLE_BENCH_CORE and CRUCIBLE_BENCH_JSON outside
    // the timed regions.
    const int core = bench::env_core();
    const bench::JsonTail json = bench::env_json();

    auto run = [&](::fixy::TextView name, auto&& body) { return bench::Run(name).core(core).measure(body); };

    bench::print("=== smoke ===\n");

    ::fixy::FixedArray<bench::Report, 3> reports{
        // (1) Integer increment + do_not_optimize. One ALU add + register
        //     clobber per iteration; auto-batch lands near ~0.3-0.5 ns/op.
        [&] {
            uint64_t sink = kAddInitialSink;
            return run("add.u64 + do_not_optimize", [&] {
                sink = sink + 1;
                bench::do_not_optimize(sink);
            });
        }(),
        // (2) XOR variant — same latency class as ADD on any x86 ALU port
        //     since Haswell. Used below as the B-side of the A/B compare
        //     so Mann-Whitney U should return "[indistinguishable]".
        [&] {
            uint64_t sink = kXorInitialSink;
            return run("xor.u64 + do_not_optimize", [&] {
                sink = sink ^ kXorMixConstant;
                bench::do_not_optimize(sink);
            });
        }(),
        // (3) Process-wide atomic relaxed load. One MOV on x86 (TSO makes
        //     loads sequentially consistent for free); exercises the
        //     atomic-path shape the harness itself uses internally. NOT
        //     thread_local: an atomic op on a per-thread object has no
        //     synchronization meaning (one instance per thread → no peer to
        //     order against), so the storage is process-wide `static`.
        atomic_load_row(core),
    };

    bench::emit_reports_text(reports.view());

    // A/B compare: ADD vs XOR. Both are one-cycle ALU ops on every x86
    // microarchitecture since Haswell; Mann-Whitney U should return
    // |z| < 2.576 → "[indistinguishable]". If this ever reports
    // distinguishable with non-trivial Δp99, one of: (a) compiler folded
    // one side to a constant, (b) core migration mid-run, (c) harness bug.
    const bench::Report& r_add = reports.at(::fixy::index<0>());
    const bench::Report& r_xor = reports.at(::fixy::index<1>());
    const auto cmp = bench::compare(r_add, r_xor);
    bench::print("\n=== compare ===\n");
    cmp.print_text();

    // Bootstrap CI on a tail percentile — exercises the Efron path in
    // Report::ci() so a regression there doesn't sneak through silently.
    const auto ci99 = r_add.ci(0.99);
    bench::print("  add.u64 p99 95% CI: [{}, {}] ns\n", ::fixy::Fixed<2>{ci99.lo}, ::fixy::Fixed<2>{ci99.hi});

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
