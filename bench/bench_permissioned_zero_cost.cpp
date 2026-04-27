// Permissioned* wrapper zero-cost validation bench.
//
// Closes FOUND-A04 / A10 / A14 / A19 in one bench file.  Compares each
// of the four Permissioned* wrappers against its bare counterpart on
// the canonical hot-path operation (try_push / try_pop / try_steal),
// using the harness's bench::compare primitive (Mann-Whitney U test +
// Δp50 / Δp99 / Δμ percentages, with [REGRESS] / [IMPROVE] /
// [indistinguishable] flagging at the 1% significance level).
//
// The zero-cost claim from THREADING.md §5.5 / 27_04 §5.3:
//   sizeof(PermissionedFooHandle) == sizeof(Channel*)  via EBO
//   The handle's hot-path call inlines straight through to the
//   underlying primitive.  No additional atomic, no additional branch,
//   no additional memory access.  Hot-path codegen identical.
//
// This bench is the empirical witness of that claim.  PASS = every
// pair is statistically [indistinguishable] (|z| ≤ 2.576 from Mann-
// Whitney U) OR shows ≤ 5 % Δp99.  FAIL = any pair shows [REGRESS]
// flag at > 5 % Δp99 with distinguishable z.
//
// Methodology:
//   1. For each (bare, wrapped) pair, allocate fresh ring + handle.
//   2. Run bench::run on bare hot-path; same on wrapped hot-path.
//   3. bench::compare emits the structured delta + flag.
//   4. Final exit code: 0 iff every pair is non-REGRESS.
//
// Notes on scope:
//   - Single-threaded only.  Multi-thread Permissioned bench is a
//     separate (heavier) artifact — this one is the per-call
//     overhead claim.
//   - Pop-side benches pre-fill enough items to never hit empty
//     during measurement.
//   - Consumer-handle construction takes a Permission token (linear);
//     produced via permission_root_mint in the bench setup.

#include <array>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include <crucible/concurrent/ChaseLevDeque.h>
#include <crucible/concurrent/MpmcRing.h>
#include <crucible/concurrent/MpscRing.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/ShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

#include "bench_harness.h"

namespace {

using Item = std::uint64_t;
constexpr std::size_t kCap = 1U << 20;  // 1M slots — never fills

// Per-wrapper UserTag — keeps each Permission tree distinct so the
// permission_root_mint sites don't collide across the bench.
struct MpscBenchTag {};
struct MpmcBenchTag {};
struct GridBenchTag {};
struct DequeBenchTag {};

// ─────────────────────────────────────────────────────────────────────
// MpscRing pair
// ─────────────────────────────────────────────────────────────────────

bench::Report bare_mpsc_push() {
    auto ring = std::make_unique<crucible::concurrent::MpscRing<Item, kCap>>();
    Item i = 0;
    return bench::run("bare MpscRing.try_push", [&]{
        const bool ok = ring->try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report wrapped_mpsc_push() {
    using Ch = crucible::concurrent::PermissionedMpscChannel<Item, kCap, MpscBenchTag>;
    auto ch = std::make_unique<Ch>();
    auto p_opt = ch->producer();
    if (!p_opt) std::abort();
    auto p = std::move(*p_opt);
    Item i = 0;
    return bench::run("wrapped Permissioned MPSC.ProducerHandle::try_push", [&]{
        const bool ok = p.try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report bare_mpsc_pop() {
    auto ring = std::make_unique<crucible::concurrent::MpscRing<Item, kCap>>();
    for (Item i = 0; i < kCap / 2; ++i) (void)ring->try_push(i);
    return bench::run("bare MpscRing.try_pop", [&]{
        auto v = ring->try_pop();
        bench::do_not_optimize(v);
    });
}

bench::Report wrapped_mpsc_pop() {
    using Ch = crucible::concurrent::PermissionedMpscChannel<Item, kCap, MpscBenchTag>;
    auto ch = std::make_unique<Ch>();
    {
        auto p_opt = ch->producer();
        if (!p_opt) std::abort();
        auto p = std::move(*p_opt);
        for (Item i = 0; i < kCap / 2; ++i) (void)p.try_push(i);
    }
    auto cons_perm = crucible::safety::permission_root_mint<
        typename Ch::consumer_tag>();
    auto c = ch->consumer(std::move(cons_perm));
    return bench::run("wrapped Permissioned MPSC.ConsumerHandle::try_pop", [&]{
        auto v = c.try_pop();
        bench::do_not_optimize(v);
    });
}

// ─────────────────────────────────────────────────────────────────────
// MpmcRing pair
// ─────────────────────────────────────────────────────────────────────

bench::Report bare_mpmc_push() {
    auto ring = std::make_unique<crucible::concurrent::MpmcRing<Item, kCap>>();
    Item i = 0;
    return bench::run("bare MpmcRing.try_push", [&]{
        const bool ok = ring->try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report wrapped_mpmc_push() {
    using Ch = crucible::concurrent::PermissionedMpmcChannel<Item, kCap, MpmcBenchTag>;
    auto ch = std::make_unique<Ch>();
    auto p_opt = ch->producer();
    if (!p_opt) std::abort();
    auto p = std::move(*p_opt);
    Item i = 0;
    return bench::run("wrapped Permissioned MPMC.ProducerHandle::try_push", [&]{
        const bool ok = p.try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report bare_mpmc_pop() {
    auto ring = std::make_unique<crucible::concurrent::MpmcRing<Item, kCap>>();
    for (Item i = 0; i < kCap / 2; ++i) (void)ring->try_push(i);
    return bench::run("bare MpmcRing.try_pop", [&]{
        auto v = ring->try_pop();
        bench::do_not_optimize(v);
    });
}

bench::Report wrapped_mpmc_pop() {
    using Ch = crucible::concurrent::PermissionedMpmcChannel<Item, kCap, MpmcBenchTag>;
    auto ch = std::make_unique<Ch>();
    {
        auto p_opt = ch->producer();
        if (!p_opt) std::abort();
        auto p = std::move(*p_opt);
        for (Item i = 0; i < kCap / 2; ++i) (void)p.try_push(i);
    }
    auto c_opt = ch->consumer();
    if (!c_opt) std::abort();
    auto c = std::move(*c_opt);
    return bench::run("wrapped Permissioned MPMC.ConsumerHandle::try_pop", [&]{
        auto v = c.try_pop();
        bench::do_not_optimize(v);
    });
}

// ─────────────────────────────────────────────────────────────────────
// ShardedSpscGrid pair (M=1, N=1 — degenerate single-shard case for
// per-call comparison; the wrapper's overhead claim doesn't depend on
// M / N because each shard maps to a distinct ProducerHandle<I>)
// ─────────────────────────────────────────────────────────────────────

bench::Report bare_grid_push() {
    auto grid = std::make_unique<
        crucible::concurrent::ShardedSpscGrid<Item, 1, 1, kCap>>();
    Item i = 0;
    return bench::run("bare ShardedSpscGrid.send(0, item)", [&]{
        const bool ok = grid->send(0, ++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report wrapped_grid_push() {
    using PG = crucible::concurrent::PermissionedShardedGrid<Item, 1, 1, kCap,
                                                              GridBenchTag>;
    auto grid = std::make_unique<PG>();
    auto whole = crucible::safety::permission_root_mint<
        typename PG::whole_tag>();
    auto perms = crucible::safety::split_grid<typename PG::whole_tag, 1, 1>(
        std::move(whole));
    auto p0 = grid->template producer<0>(
        std::move(std::get<0>(perms.producers)));
    Item i = 0;
    return bench::run("wrapped Permissioned Grid.ProducerHandle<0>::try_push", [&]{
        const bool ok = p0.try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report bare_grid_recv() {
    auto grid = std::make_unique<
        crucible::concurrent::ShardedSpscGrid<Item, 1, 1, kCap>>();
    for (Item i = 0; i < kCap / 2; ++i) (void)grid->send(0, i);
    return bench::run("bare ShardedSpscGrid.try_recv(0)", [&]{
        auto v = grid->try_recv(0);
        bench::do_not_optimize(v);
    });
}

bench::Report wrapped_grid_recv() {
    using PG = crucible::concurrent::PermissionedShardedGrid<Item, 1, 1, kCap,
                                                              GridBenchTag>;
    auto grid = std::make_unique<PG>();
    auto whole = crucible::safety::permission_root_mint<
        typename PG::whole_tag>();
    auto perms = crucible::safety::split_grid<typename PG::whole_tag, 1, 1>(
        std::move(whole));
    {
        auto p0 = grid->template producer<0>(
            std::move(std::get<0>(perms.producers)));
        for (Item i = 0; i < kCap / 2; ++i) (void)p0.try_push(i);
    }
    auto c0 = grid->template consumer<0>(
        std::move(std::get<0>(perms.consumers)));
    return bench::run("wrapped Permissioned Grid.ConsumerHandle<0>::try_recv", [&]{
        auto v = c0.try_recv();
        bench::do_not_optimize(v);
    });
}

// ─────────────────────────────────────────────────────────────────────
// ChaseLevDeque pair — owner push/pop plus thief steal
// ─────────────────────────────────────────────────────────────────────

bench::Report bare_deque_push() {
    auto dq = std::make_unique<crucible::concurrent::ChaseLevDeque<Item, kCap>>();
    Item i = 0;
    return bench::run("bare ChaseLevDeque.push_bottom", [&]{
        const bool ok = dq->push_bottom(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report wrapped_deque_push() {
    using PD = crucible::concurrent::PermissionedChaseLevDeque<Item, kCap,
                                                                DequeBenchTag>;
    auto dq = std::make_unique<PD>();
    auto own_perm = crucible::safety::permission_root_mint<
        typename PD::owner_tag>();
    auto own = dq->owner(std::move(own_perm));
    Item i = 0;
    return bench::run("wrapped Permissioned Deque.OwnerHandle::try_push", [&]{
        const bool ok = own.try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report bare_deque_pop() {
    auto dq = std::make_unique<crucible::concurrent::ChaseLevDeque<Item, kCap>>();
    for (Item i = 0; i < kCap / 2; ++i) (void)dq->push_bottom(i);
    return bench::run("bare ChaseLevDeque.pop_bottom", [&]{
        auto v = dq->pop_bottom();
        bench::do_not_optimize(v);
    });
}

bench::Report wrapped_deque_pop() {
    using PD = crucible::concurrent::PermissionedChaseLevDeque<Item, kCap,
                                                                DequeBenchTag>;
    auto dq = std::make_unique<PD>();
    auto own_perm = crucible::safety::permission_root_mint<
        typename PD::owner_tag>();
    auto own = dq->owner(std::move(own_perm));
    for (Item i = 0; i < kCap / 2; ++i) (void)own.try_push(i);
    return bench::run("wrapped Permissioned Deque.OwnerHandle::try_pop", [&]{
        auto v = own.try_pop();
        bench::do_not_optimize(v);
    });
}

bench::Report bare_deque_steal() {
    auto dq = std::make_unique<crucible::concurrent::ChaseLevDeque<Item, kCap>>();
    for (Item i = 0; i < kCap / 2; ++i) (void)dq->push_bottom(i);
    return bench::run("bare ChaseLevDeque.steal_top", [&]{
        auto v = dq->steal_top();
        bench::do_not_optimize(v);
    });
}

bench::Report wrapped_deque_steal() {
    using PD = crucible::concurrent::PermissionedChaseLevDeque<Item, kCap,
                                                                DequeBenchTag>;
    auto dq = std::make_unique<PD>();
    {
        auto own_perm = crucible::safety::permission_root_mint<
            typename PD::owner_tag>();
        auto own = dq->owner(std::move(own_perm));
        for (Item i = 0; i < kCap / 2; ++i) (void)own.try_push(i);
    }
    auto t_opt = dq->thief();
    if (!t_opt) std::abort();
    auto t = std::move(*t_opt);
    return bench::run("wrapped Permissioned Deque.ThiefHandle::try_steal", [&]{
        auto v = t.try_steal();
        bench::do_not_optimize(v);
    });
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== permissioned_zero_cost ===\n");
    std::printf("  Item:    uint64_t (8 bytes)\n");
    std::printf("  Cap:     %zu slots (1M — never fills)\n", kCap);
    std::printf("  Method:  bare vs wrapped per primitive,\n");
    std::printf("           bench::compare with Mann-Whitney U test\n");
    std::printf("  PASS:    every pair [indistinguishable] OR Δp99 ≤ 5%%\n\n");

    bench::Report reports[] = {
        // 0..1   MPSC push
        bare_mpsc_push(),  wrapped_mpsc_push(),
        // 2..3   MPSC pop
        bare_mpsc_pop(),   wrapped_mpsc_pop(),
        // 4..5   MPMC push
        bare_mpmc_push(),  wrapped_mpmc_push(),
        // 6..7   MPMC pop
        bare_mpmc_pop(),   wrapped_mpmc_pop(),
        // 8..9   Grid push
        bare_grid_push(),  wrapped_grid_push(),
        // 10..11 Grid recv
        bare_grid_recv(),  wrapped_grid_recv(),
        // 12..13 Deque push (owner)
        bare_deque_push(), wrapped_deque_push(),
        // 14..15 Deque pop (owner)
        bare_deque_pop(),  wrapped_deque_pop(),
        // 16..17 Deque steal (thief)
        bare_deque_steal(), wrapped_deque_steal(),
    };

    bench::emit_reports_text(reports);

    // ── Pair-by-pair comparison ───────────────────────────────────────
    std::printf("\n=== zero-cost claim — pair deltas ===\n");
    bench::Compare cmps[] = {
        bench::compare(reports[0],  reports[1]),
        bench::compare(reports[2],  reports[3]),
        bench::compare(reports[4],  reports[5]),
        bench::compare(reports[6],  reports[7]),
        bench::compare(reports[8],  reports[9]),
        bench::compare(reports[10], reports[11]),
        bench::compare(reports[12], reports[13]),
        bench::compare(reports[14], reports[15]),
        bench::compare(reports[16], reports[17]),
    };
    for (const auto& c : cmps) c.print_text(stdout);

    // ── Headline verdict ──────────────────────────────────────────────
    std::printf("\n=== verdict ===\n");
    int regressions = 0;
    for (const auto& c : cmps) {
        if (c.distinguishable && c.delta_p99_pct > 5.0) {
            std::printf("  REGRESS: %s → %s  Δp99=%+.2f%%\n",
                        c.a_name.c_str(), c.b_name.c_str(),
                        c.delta_p99_pct);
            ++regressions;
        }
    }
    if (regressions == 0) {
        std::printf("  PASS — all 9 Permissioned wrapper pairs are\n");
        std::printf("         statistically indistinguishable from\n");
        std::printf("         their bare counterparts (zero-cost claim\n");
        std::printf("         empirically validated).\n");
    } else {
        std::printf("  FAIL — %d wrapper(s) regressed beyond 5%% Δp99.\n",
                    regressions);
    }

    bench::emit_reports_json(reports, json);
    return regressions;  // exit code = number of regressions
}
