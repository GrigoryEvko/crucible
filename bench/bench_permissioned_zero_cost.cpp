// Permissioned channel zero-cost validation bench.
//
// Compares the permissioned MPSC channel of fixy/concurrent against its
// bare MpscRing on the hot-path operations try_push and try_pop, with
// the harness's bench::compare primitive (Mann-Whitney U test +
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
// The new tree carries one permissioned channel with a bare ring beside
// it, the MPSC channel.  The permissioned SPSC channel has its own
// session bench.  The MPMC channel, the sharded grid and the
// permissioned Chase-Lev deque were dropped with no production
// consumer, so their pairs are gone.
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
//     produced via mint_permission_root in the bench setup.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>

#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

namespace {

using Item = std::uint64_t;
constexpr std::size_t kCap = 1U << 20;  // 1M slots — never fills

// The UserTag keeps the Permission tree of this bench distinct from any
// other channel in the process.
struct MpscBenchTag {};

using Channel = ::fixy::concurrent::PermissionedMpscChannel<Item, kCap, MpscBenchTag>;

// ─────────────────────────────────────────────────────────────────────
// MpscRing pair
// ─────────────────────────────────────────────────────────────────────

bench::Report bare_mpsc_push() {
    auto ring = std::make_unique<::fixy::concurrent::MpscRing<Item, kCap>>();
    Item i = 0;
    return bench::run("bare MpscRing.try_push", [&] {
        const bool ok = ring->try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report wrapped_mpsc_push() {
    auto ch = std::make_unique<Channel>();
    auto p_opt = ch->producer();
    if (!p_opt) std::abort();
    auto p = std::move(*p_opt);
    Item i = 0;
    return bench::run("wrapped Permissioned MPSC.ProducerHandle::try_push", [&] {
        const bool ok = p.try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report bare_mpsc_pop() {
    auto ring = std::make_unique<::fixy::concurrent::MpscRing<Item, kCap>>();
    for (Item i = 0; i < kCap / 2; ++i)
        (void)ring->try_push(i);
    return bench::run("bare MpscRing.try_pop", [&] {
        auto v = ring->try_pop();
        bench::do_not_optimize(v);
    });
}

bench::Report wrapped_mpsc_pop() {
    auto ch = std::make_unique<Channel>();
    {
        auto p_opt = ch->producer();
        if (!p_opt) std::abort();
        auto p = std::move(*p_opt);
        for (Item i = 0; i < kCap / 2; ++i)
            (void)p.try_push(i);
    }
    auto cons_perm = ::foundation::permissions::mint_permission_root<typename Channel::consumer_tag>();
    auto c = ch->consumer(std::move(cons_perm));
    return bench::run("wrapped Permissioned MPSC.ConsumerHandle::try_pop", [&] {
        auto v = c.try_pop();
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
    std::printf("  PASS:    no pair flagged [REGRESS] (Δp99 > %.0f%% with p < 0.01)\n\n",
                bench::Compare::kFlagDeltaP99Pct);

    bench::Report reports[] = {
        // 0..1   MPSC push
        bare_mpsc_push(),
        wrapped_mpsc_push(),
        // 2..3   MPSC pop
        bare_mpsc_pop(),
        wrapped_mpsc_pop(),
    };

    bench::emit_reports_text(reports);

    // ── Pair-by-pair comparison ───────────────────────────────────────
    std::printf("\n=== zero-cost claim — pair deltas ===\n");
    bench::Compare cmps[] = {
        bench::compare(reports[0], reports[1]),
        bench::compare(reports[2], reports[3]),
    };
    bench::emit_compares(cmps);

    // ── Headline verdict ──────────────────────────────────────────────
    std::printf("\n=== verdict ===\n");
    int regressions = 0;
    for (const auto& c : cmps) {
        if (c.is_regression()) {
            std::printf("  REGRESS: %s → %s  Δp99=%+.2f%%\n", c.a_name.c_str(), c.b_name.c_str(), c.delta_p99_pct);
            ++regressions;
        }
    }
    if (regressions == 0) {
        std::printf("  PASS — no permissioned MPSC pair is slower than\n");
        std::printf("         its bare counterpart by more than %.0f%% Δp99.\n", bench::Compare::kFlagDeltaP99Pct);
    } else {
        std::printf("  FAIL — %d wrapper(s) regressed beyond %.0f%% Δp99.\n", regressions,
                    bench::Compare::kFlagDeltaP99Pct);
    }

    bench::emit_reports_json(reports, json);
    return regressions;  // exit code = number of regressions
}
