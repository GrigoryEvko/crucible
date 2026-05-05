// AutoSplit strategy-comparison bench.
//
// Where bench_auto_split.cpp measures the *planner* (constexpr decision
// + hash digest, ~5 ns), this bench measures the *value* of routing:
// for each {scenario × body × strategy} triple, run the same logical
// workload through four execution strategies and report wall-time per
// iteration plus the router's "skill" — Δ to the best fixed strategy.
//
// Strategies:
//   sequential : dispatch_at_factor(pool, req, 1, body)  — runs inline
//   fixed_2    : dispatch_at_factor(pool, req, 2, body)  — forces 2 shards
//   fixed_max  : dispatch_at_factor(pool, req, scenario.max_shards, body)
//   router     : dispatch_auto_split(pool, req, profile, body)
//
// Bodies:
//   null    : single fetch_add(shard.size()) — minimal work; reveals
//             pool overhead vs inline overhead.
//   mem     : touch every cache line in shard.byte_count — bandwidth-
//             bound; benefits from per-shard NUMA locality.
//   compute : K LCG iterations per item — compute-bound; per-item cost
//             matches scenario.compute_per_item_ns (declared to router).
//
// What you're meant to read off the skill table:
//   skill > 0%   → router beats all fixed strategies for this case
//   skill = 0%   → router matches some fixed strategy (typically
//                  whichever shard-count it picked)
//   skill < 0%   → router lost to a fixed strategy; the cliff/break-
//                  even thresholds need calibration for this workload
//
// Companion benches:
//   bench_auto_split.cpp        — planner overhead (constexpr)
//   bench_concurrent_queues.cpp — per-queue per-op cost
//   bench_concurrent_saturation — SPSC L1d-port floor sweep
//
// Note on permissioned queues: AutoSplit dispatches lambdas through a
// Pool<Policy>; it does NOT push items through a Permissioned* queue.
// Queue cost is orthogonal to AutoSplit's parallelism choice — see
// bench_concurrent_queues.cpp for those numbers.

#include <crucible/concurrent/AutoSplit.h>
#include "bench_harness.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace cc = crucible::concurrent;
namespace cs = crucible::concurrent::scheduler;

namespace {

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;

// ── Workload bodies ──────────────────────────────────────────────────

struct NullBody {
    std::atomic<std::uint64_t>* visited;

    void operator()(cc::AutoSplitShard shard) const noexcept {
        visited->fetch_add(shard.size(), std::memory_order_relaxed);
    }
};

struct MemBody {
    std::atomic<std::uint64_t>* visited;
    const std::byte*            arena;

    void operator()(cc::AutoSplitShard shard) const noexcept {
        const std::byte* p = arena + shard.byte_offset;
        std::uint64_t sum = 0;
        // One byte per cache line — pure bandwidth probe.
        for (std::size_t i = 0; i < shard.byte_count; i += 64) {
            sum += static_cast<std::uint8_t>(p[i]);
        }
        bench::do_not_optimize(sum);
        visited->fetch_add(shard.size(), std::memory_order_relaxed);
    }
};

struct ComputeBody {
    std::atomic<std::uint64_t>* visited;
    std::size_t                 iterations;  // LCG passes per item

    void operator()(cc::AutoSplitShard shard) const noexcept {
        std::uint64_t acc = 0xDEADBEEFULL ^
                            static_cast<std::uint64_t>(shard.index);
        for (std::size_t i = shard.begin; i < shard.end; ++i) {
            std::uint64_t x = static_cast<std::uint64_t>(i) ^ acc;
            for (std::size_t k = 0; k < iterations; ++k) {
                x = (x * 0x9E3779B97F4A7C15ULL) ^ (x >> 13);
            }
            acc ^= x;
        }
        bench::do_not_optimize(acc);
        visited->fetch_add(shard.size(), std::memory_order_relaxed);
    }
};

// ── Scenarios ────────────────────────────────────────────────────────

struct Scenario {
    const char*   name;
    std::size_t   items;
    std::size_t   bytes_per_item;
    std::size_t   max_shards;
    // ComputeBody knob.  0 → compute body skipped for this scenario.
    std::size_t   compute_iterations;
    // Per-body hints fed to AutoSplitRequest::per_item_compute_ns.
    // Setting a non-zero hint triggers the break-even gate, which can
    // demote shard_count → 1 when fanout overhead dominates the work.
    std::uint64_t null_per_item_ns;     // ~1 — atomic fetch_add cost
    std::uint64_t mem_per_item_ns;      // ~bytes_per_item/64 (L1-DRAM avg)
    std::uint64_t compute_per_item_ns;  // ~iterations × 0.3 ns @ 4.5 GHz
};

// Six scenarios spanning the byte-tier spectrum and compute densities.
// Sized so total work fits in 64 MiB arena.
constexpr std::array<Scenario, 6> scenarios = {{
    // L1-resident: 2 KiB total — sequential should always win.
    Scenario{
        .name = "L1.tiny",
        .items = 256,
        .bytes_per_item = 8,
        .max_shards = 16,
        .compute_iterations = 0,
        .null_per_item_ns = 1,
        .mem_per_item_ns = 1,
        .compute_per_item_ns = 0,
    },
    // L2-resident: 128 KiB — below the 256 KiB cliff; sequential wins.
    Scenario{
        .name = "L2.batch",
        .items = 2048,
        .bytes_per_item = 64,
        .max_shards = 16,
        .compute_iterations = 0,
        .null_per_item_ns = 1,
        .mem_per_item_ns = 1,
        .compute_per_item_ns = 0,
    },
    // L3-resident bandwidth: 1 MiB total — byte-tier wants 4 shards.
    Scenario{
        .name = "L3.scan",
        .items = 8192,
        .bytes_per_item = 128,
        .max_shards = 8,
        .compute_iterations = 0,
        .null_per_item_ns = 1,
        .mem_per_item_ns = 2,
        .compute_per_item_ns = 0,
    },
    // DRAM bandwidth: 16 MiB total — byte-tier wants 16 shards.
    Scenario{
        .name = "DRAM.stream",
        .items = 65536,
        .bytes_per_item = 256,
        .max_shards = 16,
        .compute_iterations = 0,
        .null_per_item_ns = 1,
        .mem_per_item_ns = 3,
        .compute_per_item_ns = 0,
    },
    // L3 + heavy compute: 4 MiB working set, ~100 ns/item.
    // Sequential model: 16384 × 100 ns ≈ 1.6 ms. Fan-out should help.
    Scenario{
        .name = "L3.compute",
        .items = 16384,
        .bytes_per_item = 256,
        .max_shards = 8,
        .compute_iterations = 100,
        .null_per_item_ns = 1,
        .mem_per_item_ns = 3,
        .compute_per_item_ns = 100,
    },
    // DRAM + heavy compute: 16 MiB working set, ~400 ns/item.
    // Sequential model: 65536 × 400 ns ≈ 26 ms. Big parallel win.
    Scenario{
        .name = "DRAM.compute",
        .items = 65536,
        .bytes_per_item = 256,
        .max_shards = 16,
        .compute_iterations = 500,
        .null_per_item_ns = 1,
        .mem_per_item_ns = 3,
        .compute_per_item_ns = 400,
    },
}};

// ── Strategies ───────────────────────────────────────────────────────

enum class Strategy : std::uint8_t {
    Sequential,
    Fixed2,
    FixedMax,
    Router,
};

[[nodiscard]] constexpr const char* strategy_name(Strategy s) noexcept {
    switch (s) {
    case Strategy::Sequential: return "seq";
    case Strategy::Fixed2:     return "fixed_2";
    case Strategy::FixedMax:   return "fixed_max";
    case Strategy::Router:     return "router";
    }
    return "?";
}

enum class BodyKind : std::uint8_t { Null, Mem, Compute };

[[nodiscard]] constexpr const char* body_name(BodyKind b) noexcept {
    switch (b) {
    case BodyKind::Null:    return "null";
    case BodyKind::Mem:     return "mem";
    case BodyKind::Compute: return "compute";
    }
    return "?";
}

// ── Strategy runner ──────────────────────────────────────────────────

[[nodiscard]] cc::AutoSplitRequest
make_request(const Scenario& sc,
             BodyKind body,
             cc::SchedulingIntent intent = cc::SchedulingIntent::Throughput) noexcept {
    const std::uint64_t per_item_ns =
        body == BodyKind::Null    ? sc.null_per_item_ns
      : body == BodyKind::Mem     ? sc.mem_per_item_ns
                                  : sc.compute_per_item_ns;
    return cc::AutoSplitRequest{
        .item_count          = sc.items,
        .bytes_per_item      = sc.bytes_per_item,
        .max_shards          = sc.max_shards,
        .producers           = 1,
        .consumers           = 1,
        .per_item_compute_ns = per_item_ns,
        .intent              = intent,
        .touches_memory      = body == BodyKind::Mem,
    };
}

template <typename Body>
void run_strategy(cc::Pool<cs::Fifo>&                pool,
                  Strategy                            strategy,
                  const Scenario&                     sc,
                  BodyKind                            body_kind,
                  const cc::AutoSplitRuntimeProfile&  profile,
                  Body                                body) {
    const cc::AutoSplitRequest req = make_request(sc, body_kind);

    switch (strategy) {
    case Strategy::Sequential:
        // factor=1 runs inline on the calling thread; no pool involvement.
        (void)cc::dispatch_at_factor(pool, req, 1, std::move(body));
        break;
    case Strategy::Fixed2:
        (void)cc::dispatch_at_factor(pool, req, 2, std::move(body));
        pool.wait_idle();
        break;
    case Strategy::FixedMax:
        (void)cc::dispatch_at_factor(pool, req, sc.max_shards, std::move(body));
        pool.wait_idle();
        break;
    case Strategy::Router:
        (void)cc::dispatch_auto_split(pool, req, profile, std::move(body));
        pool.wait_idle();
        break;
    }
}

template <typename Body>
[[nodiscard]] bench::Report
run_one(std::string                        name,
        cc::Pool<cs::Fifo>&                pool,
        Strategy                            strategy,
        const Scenario&                     sc,
        BodyKind                            body_kind,
        const cc::AutoSplitRuntimeProfile&  profile,
        Body                                body,
        std::size_t                         samples) {
    bench::Run run{std::move(name)};
    if (const int core = bench::env_core(); core >= 0) {
        (void)run.core(core);
    }
    return run.samples(samples)
        .warmup(std::max<std::size_t>(10, samples / 10))
        .max_wall_ms(3'000)
        .measure([&] {
            run_strategy(pool, strategy, sc, body_kind, profile, body);
        });
}

// ── Skill table ──────────────────────────────────────────────────────

void print_skill_table(const std::vector<bench::Report>& reports,
                       const std::array<Scenario, 6>&    sc_arr,
                       const cc::AutoSplitRuntimeProfile& profile) {
    std::printf("\n=== auto_split_compare: strategy skill table ===\n");
    std::printf("  body / strategy times below are p50 wall time per iteration.\n");
    std::printf("  best_fix = min(seq, fixed_2, fixed_max).\n");
    std::printf("  skill    = (best_fix - router) / best_fix × 100   (positive = router wins)\n\n");
    std::printf("  %-18s %-7s %10s %10s %10s %10s   %5s %10s %10s %8s %14s\n",
                "scenario", "body",
                "seq[µs]", "fixed_2[µs]", "fixed_max[µs]", "router[µs]",
                "rfact", "best_fix[µs]", "rCPU[µs]", "reff", "skill_vs_best");

    auto fmt_us = [](double ns) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%9.2f", ns / 1000.0);
        return std::string(buf);
    };

    std::size_t idx = 0;
    int   wins   = 0;
    int   ties   = 0;
    int   losses = 0;
    for (const auto& sc : sc_arr) {
        for (BodyKind body : {BodyKind::Null, BodyKind::Mem, BodyKind::Compute}) {
            if (body == BodyKind::Compute && sc.compute_iterations == 0) continue;

            const double seq = reports[idx + 0].pct.p50;
            const double fx2 = reports[idx + 1].pct.p50;
            const double fxm = reports[idx + 2].pct.p50;
            const double rtr = reports[idx + 3].pct.p50;
            idx += 4;

            const double best_fixed = std::min({seq, fx2, fxm});
            const double skill_pct = best_fixed > 0
                ? (best_fixed - rtr) / best_fixed * 100.0
                : 0.0;

            const auto plan = cc::auto_split_plan(make_request(sc, body), profile);
            const double router_cpu = rtr * static_cast<double>(
                std::max<std::size_t>(1, plan.shard_count));
            const double router_eff = router_cpu > 0.0
                ? std::min(999.9, seq / router_cpu * 100.0)
                : 0.0;

            const char* outcome = "tie";
            if (skill_pct >  3.0) { outcome = "WIN"; ++wins; }
            else if (skill_pct < -3.0) { outcome = "LOSS"; ++losses; }
            else { ++ties; }

            std::printf("  %-18s %-7s %10s %10s %10s %10s   %5zu %10s %10s %7.1f%%   %+6.1f%%  %s\n",
                        sc.name, body_name(body),
                        fmt_us(seq).c_str(),
                        fmt_us(fx2).c_str(),
                        fmt_us(fxm).c_str(),
                        fmt_us(rtr).c_str(),
                        plan.shard_count,
                        fmt_us(best_fixed).c_str(),
                        fmt_us(router_cpu).c_str(),
                        router_eff,
                        skill_pct, outcome);
        }
    }

    std::printf("\n  router decisions: %d wins, %d ties, %d losses\n",
                wins, ties, losses);
}

void print_intent_matrix(const std::array<Scenario, 6>&    sc_arr,
                         const cc::AutoSplitRuntimeProfile& profile) {
    std::printf("\n=== auto_split_compare: intent factor matrix ===\n");
    std::printf("  %-18s %-7s %7s %7s %7s %7s %7s\n",
                "scenario", "body", "thr", "lat", "bg", "seq", "adapt");

    for (const auto& sc : sc_arr) {
        for (BodyKind body : {BodyKind::Null, BodyKind::Mem, BodyKind::Compute}) {
            if (body == BodyKind::Compute && sc.compute_iterations == 0) continue;
            const auto throughput = cc::auto_split_plan(
                make_request(sc, body, cc::SchedulingIntent::Throughput), profile);
            const auto latency = cc::auto_split_plan(
                make_request(sc, body, cc::SchedulingIntent::LatencyCritical), profile);
            const auto background = cc::auto_split_plan(
                make_request(sc, body, cc::SchedulingIntent::Background), profile);
            const auto sequential = cc::auto_split_plan(
                make_request(sc, body, cc::SchedulingIntent::Sequential), profile);
            const auto adaptive = cc::auto_split_plan(
                make_request(sc, body, cc::SchedulingIntent::Adaptive), profile);

            std::printf("  %-18s %-7s %7zu %7zu %7zu %7zu %7zu\n",
                        sc.name, body_name(body),
                        throughput.shard_count,
                        latency.shard_count,
                        background.shard_count,
                        sequential.shard_count,
                        adaptive.shard_count);
        }
    }
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const cc::AutoSplitRuntimeProfile& profile =
        cc::auto_split_runtime_profile_once();
    cc::Pool<cs::Fifo> pool{cc::CoreCount{8}};

    // 64 MiB arena covers the largest scenario (DRAM.compute = 16 MiB).
    constexpr std::size_t arena_bytes = 64 * MiB;
    auto* arena = static_cast<std::byte*>(std::aligned_alloc(64, arena_bytes));
    if (!arena) {
        std::fprintf(stderr, "bench_auto_split_compare: arena alloc failed\n");
        std::abort();
    }
    std::memset(arena, 0xCC, arena_bytes);

    std::atomic<std::uint64_t> visited{0};

    std::printf("=== auto_split_compare ===\n");
    std::printf("  l2_per_core=%zu  huge=%zu  workers=%zu  dispatch_cost_ns=%llu\n",
                profile.route.l2_per_core_bytes,
                profile.route.huge_bytes,
                profile.available_workers,
                static_cast<unsigned long long>(profile.dispatch_cost_ns));
    std::printf("  arena=%zu MiB  pool_workers=%zu\n\n",
                arena_bytes / MiB, pool.worker_count());

    std::printf("  scenarios:\n");
    for (const auto& sc : scenarios) {
        const std::size_t total = sc.items * sc.bytes_per_item;
        std::printf("    %-18s items=%-7zu itemB=%-4zu workB=%-9zu max=%-3zu compIter=%-4zu hints(null/mem/compute)=%llu/%llu/%lluns\n",
                    sc.name, sc.items, sc.bytes_per_item, total,
                    sc.max_shards, sc.compute_iterations,
                    static_cast<unsigned long long>(sc.null_per_item_ns),
                    static_cast<unsigned long long>(sc.mem_per_item_ns),
                    static_cast<unsigned long long>(sc.compute_per_item_ns));
    }
    std::putchar('\n');

    std::vector<bench::Report> reports;
    reports.reserve(scenarios.size() * 3 * 4);

    constexpr std::size_t kSamples = 250;

    for (const auto& sc : scenarios) {
        for (BodyKind body_kind : {BodyKind::Null, BodyKind::Mem, BodyKind::Compute}) {
            if (body_kind == BodyKind::Compute && sc.compute_iterations == 0) continue;

            for (Strategy s : {Strategy::Sequential, Strategy::Fixed2,
                               Strategy::FixedMax, Strategy::Router}) {
                std::string name = "compare.";
                name += sc.name;
                name += '.';
                name += body_name(body_kind);
                name += '.';
                name += strategy_name(s);

                switch (body_kind) {
                case BodyKind::Null: {
                    NullBody body{&visited};
                    reports.push_back(run_one(std::move(name), pool, s, sc,
                                              body_kind, profile, body, kSamples));
                    break;
                }
                case BodyKind::Mem: {
                    MemBody body{&visited, arena};
                    reports.push_back(run_one(std::move(name), pool, s, sc,
                                              body_kind, profile, body, kSamples));
                    break;
                }
                case BodyKind::Compute: {
                    ComputeBody body{&visited, sc.compute_iterations};
                    reports.push_back(run_one(std::move(name), pool, s, sc,
                                              body_kind, profile, body, kSamples));
                    break;
                }
                }
            }
        }
    }

    if (pool.failed() != 0) {
        std::fprintf(stderr, "bench_auto_split_compare: pool failures=%llu\n",
                     static_cast<unsigned long long>(pool.failed()));
        std::abort();
    }

    bench::emit_reports_text(reports);
    print_skill_table(reports, scenarios, profile);
    print_intent_matrix(scenarios, profile);
    bench::emit_reports_json(reports, bench::env_json());

    std::free(arena);
    return 0;
}
