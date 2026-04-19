// Harness smoke-test.
//
// This bench exists to prove the bench::Run / Report / Compare machinery
// produces well-formed output end-to-end, not to measure anything
// interesting about Crucible itself. It exercises the cheapest possible
// operations (an integer increment, an inline hash, a relaxed atomic
// load, a steady_clock read) so that the reported percentiles,
// cycles/op, the optional sensory grid, the JSON dump, and the A/B
// compare path all get hit on every CI run.
//
// Every body is deterministic — no RNG seeded from wall time, no file
// I/O, no syscalls other than the VDSO-backed steady_clock read. On a
// pinned core with no background load the numbers should repeat to
// within a few percent across runs (> 90 % of invocations).
//
// The mix covers the two auto-batching regimes:
//   • sub-ns ops (add, xor, hash) — auto-batch ramps to 2^N until one
//     batch exceeds 1000 cycles, then reports batch-averaged percentiles
//     (marked "[batch-avg]" in the text output).
//   • ~10-20 ns ops (steady_clock::now) — one-shot, reported per-sample.

#include "bench_harness.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

namespace {

[[nodiscard]] int env_core() noexcept {
    if (const char* s = std::getenv("CRUCIBLE_BENCH_CORE"))
        return static_cast<int>(std::strtol(s, nullptr, 10));
    return -1;
}

[[nodiscard]] bool env_json() noexcept {
    const char* s = std::getenv("CRUCIBLE_BENCH_JSON");
    return s && s[0] && std::string(s) != "0";
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const int  core = env_core();
    const bool json = env_json();

    auto run = [&](std::string name, auto&& body) {
        return bench::Run(std::move(name)).core(core).measure(body);
    };

    std::printf("=== smoke ===\n");

    bench::Report reports[] = {
        // ── (1) Integer increment + do_not_optimize. After auto-batch
        //        this should land at ~0.3-0.5 ns/op — a single add +
        //        register-dependency barrier per iteration.
        [&]{
            uint64_t sink = 0;
            return run("add.u64 + do_not_optimize", [&]{
                sink = sink + 1;
                bench::do_not_optimize(sink);
            });
        }(),
        // ── (2) XOR variant — different arithmetic op, same latency
        //        class. Used below for the A/B compare demonstration:
        //        on x86 both ADD and XOR retire in 1 cycle on any ALU
        //        port, so Mann-Whitney U should call it indistinguishable.
        [&]{
            uint64_t sink = 1;
            return run("xor.u64 + do_not_optimize", [&]{
                sink = sink ^ 0x9E3779B97F4A7C15ull;
                bench::do_not_optimize(sink);
            });
        }(),
        // ── (3) std::hash<uint64_t> — libstdc++/libc++ both specialize
        //        this to the identity function, so the measured cost is
        //        essentially one move + the clobber. Guards against a
        //        future stdlib change that would quietly make this much
        //        slower.
        [&]{
            uint64_t x = 0x0123456789ABCDEFull;
            std::hash<uint64_t> h{};
            return run("std::hash<u64>", [&]{
                const uint64_t v = h(x);
                x += 1;
                bench::do_not_optimize(v);
            });
        }(),
        // ── (4) Thread-local atomic relaxed load. One MOV on x86 (TSO
        //        gives sequentially consistent loads for free); ~1-2 ns
        //        per load after auto-batch. The "relaxed" ordering
        //        matches what bench_harness uses internally for its
        //        head/tail atomics.
        [&]{
            static thread_local std::atomic<uint64_t> counter{0};
            return run("atomic<u64>.load(relaxed)", [&]{
                const uint64_t v = counter.load(std::memory_order_relaxed);
                bench::do_not_optimize(v);
            });
        }(),
        // ── (5) steady_clock::now() — VDSO-backed, ~10-20 ns. Already
        //        exceeds the auto-batch threshold on its own, so the
        //        harness reports per-sample percentiles rather than
        //        batch-averaged ones. Good cross-check that the
        //        non-batched path also works.
        [&]{
            return run("steady_clock::now()", [&]{
                const auto t = std::chrono::steady_clock::now();
                bench::do_not_optimize(t);
            });
        }(),
    };

    for (const auto& r : reports) r.print_text(stdout);

    // ── A/B compare: ADD vs XOR. Both are one-cycle ALU ops on every
    //    x86 microarchitecture since Haswell; Mann-Whitney U should
    //    return |z| < 2.576 → "[indistinguishable]". If this ever
    //    reports distinguishable with non-trivial Δp99, one of three
    //    things went wrong: (a) compiler folded one side to a constant,
    //    (b) core migration mid-run, (c) harness bug. All three are
    //    worth a loud flag.
    const auto& r_add = reports[0];
    const auto& r_xor = reports[1];
    const auto cmp = bench::compare(r_add, r_xor);
    std::printf("\n=== compare ===\n");
    cmp.print_text(stdout);

    // ── Bootstrap CI on a tail percentile — exercises the Efron path
    //    in Report::ci() so a regression there doesn't sneak through.
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
