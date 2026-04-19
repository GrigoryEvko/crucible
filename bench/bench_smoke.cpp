// harness smoke-test; minimal deterministic ops exercise the output path
// end-to-end without depending on Crucible library internals.

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "bench_harness.h"

namespace {

// ── Tunables ───────────────────────────────────────────────────────────
// Magic numbers get named constants so they're documented + greppable.
constexpr int      kDefaultCore         = -1;           // -1 = let harness auto-pick
constexpr int      kEnvBase10           = 10;           // base for strtol(CRUCIBLE_BENCH_CORE)
constexpr uint64_t kXorMixConstant      = 0x9E3779B97F4A7C15ULL;  // golden ratio (Knuth)
constexpr uint64_t kAddInitialSink      = 0;            // seed for integer-increment body
constexpr uint64_t kXorInitialSink      = 1;            // seed for xor body
constexpr uint64_t kAtomicInitialValue  = 0;            // seed for relaxed-load body

// ── Environment probes (called outside timed regions) ──────────────────
// Deterministic w.r.t. the process environment at launch — no syscalls or
// wall-clock reads sneak into any bench body below.

[[nodiscard]] int env_core() noexcept {
    const char* s = std::getenv("CRUCIBLE_BENCH_CORE");
    if (s == nullptr) return kDefaultCore;
    return static_cast<int>(std::strtol(s, nullptr, kEnvBase10));
}

[[nodiscard]] bool env_json() noexcept {
    const char* s = std::getenv("CRUCIBLE_BENCH_JSON");
    if (s == nullptr || s[0] == '\0') return false;
    // Treat literal "0" as false; any other non-empty value as true. No
    // std::string dependency — a smoke test shouldn't drag <string> in.
    return !(s[0] == '0' && s[1] == '\0');
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const int  core = env_core();
    const bool json = env_json();

    auto run = [&](const char* name, auto&& body) {
        return bench::Run(name).core(core).measure(body);
    };

    std::printf("=== smoke ===\n");

    bench::Report reports[] = {
        // (1) Integer increment + do_not_optimize. One ALU add + register
        //     clobber per iteration; auto-batch lands near ~0.3-0.5 ns/op.
        [&]{
            uint64_t sink = kAddInitialSink;
            return run("add.u64 + do_not_optimize", [&]{
                sink = sink + 1;
                bench::do_not_optimize(sink);
            });
        }(),
        // (2) XOR variant — same latency class as ADD on any x86 ALU port
        //     since Haswell. Used below as the B-side of the A/B compare
        //     so Mann-Whitney U should return "[indistinguishable]".
        [&]{
            uint64_t sink = kXorInitialSink;
            return run("xor.u64 + do_not_optimize", [&]{
                sink = sink ^ kXorMixConstant;
                bench::do_not_optimize(sink);
            });
        }(),
        // (3) Thread-local atomic relaxed load. One MOV on x86 (TSO makes
        //     loads sequentially consistent for free); exercises the
        //     atomic-path shape the harness itself uses internally.
        [&]{
            static thread_local std::atomic<uint64_t> counter{kAtomicInitialValue};
            return run("atomic<u64>.load(relaxed)", [&]{
                const uint64_t v = counter.load(std::memory_order_relaxed);
                bench::do_not_optimize(v);
            });
        }(),
    };

    for (const auto& r : reports) r.print_text(stdout);

    // A/B compare: ADD vs XOR. Both are one-cycle ALU ops on every x86
    // microarchitecture since Haswell; Mann-Whitney U should return
    // |z| < 2.576 → "[indistinguishable]". If this ever reports
    // distinguishable with non-trivial Δp99, one of: (a) compiler folded
    // one side to a constant, (b) core migration mid-run, (c) harness bug.
    const auto& r_add = reports[0];
    const auto& r_xor = reports[1];
    const auto cmp = bench::compare(r_add, r_xor);
    std::printf("\n=== compare ===\n");
    cmp.print_text(stdout);

    // Bootstrap CI on a tail percentile — exercises the Efron path in
    // Report::ci() so a regression there doesn't sneak through silently.
    const auto ci99 = reports[0].ci(0.99);
    std::printf("  add.u64 p99 95%% CI: [%.2f, %.2f] ns\n", ci99.lo, ci99.hi);

    if (json) {
        std::printf("\n=== json ===\n[\n");
        const size_t n = sizeof(reports) / sizeof(reports[0]);
        for (size_t i = 0; i < n; ++i) {
            std::printf("  ");
            reports[i].print_json(stdout);
            std::printf("%s\n", (i + 1 < n) ? "," : "");
        }
        std::printf("]\n");
    }
    return 0;
}
