// ── test_fixy_termination — FIXY-G12 positive test ────────────────────
//
// Pins the bounded-resource axis + R014/R019/R020 + warden integration
// + cost-budget cross-check.  Covers:
//   * Per-grant projection (wallclock_budget_v, bounded_alloc_v, etc.)
//   * R014 BgWorker observable demands bounded_alloc + wallclock_budget
//   * R019 hot-path-strictest demands terminating + ≤4KB + io==0
//   * R020 federation-peer demands terminating + wallclock_budget
//   * warden::deadline_from_grade_v wires budget to runtime
//   * cost_within_budget_v passes when budget ≥ predicted cost

#include <crucible/cog/CogIdentity.h>
#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/dim/Cost.h>
#include <crucible/fixy/dim/Termination.h>
#include <crucible/warden/DeadlineFromGrade.h>

#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;
namespace cc = crucible::cog;
namespace cw = crucible::warden;

namespace {

// ── Worked binding: Forge Phase D Fuse — bounded resources + cost ─

using ForgePhaseDFuse_BoundedAll = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::terminating,
    cg::bounded_alloc<65536>,
    cg::wallclock_budget<5'000'000>,         // 5 ms budget
    cg::bounded_io<0>,
    cg::cost_polynomial<200, 5>>;            // 200 + 5n ns

// ── Per-grant projection ─────────────────────────────────────────

static_assert(cf::is_terminating_v<ForgePhaseDFuse_BoundedAll>);
static_assert(cf::bounded_alloc_v<ForgePhaseDFuse_BoundedAll> == 65536);
static_assert(cf::wallclock_budget_v<ForgePhaseDFuse_BoundedAll> == 5'000'000);
static_assert(cf::bounded_io_v<ForgePhaseDFuse_BoundedAll> == 0);
static_assert(cf::HasResourcesGrant<ForgePhaseDFuse_BoundedAll>);

// ── Warden integration ──────────────────────────────────────────

static_assert(cw::has_deadline_v<ForgePhaseDFuse_BoundedAll>);
static_assert(cw::deadline_from_grade_v<ForgePhaseDFuse_BoundedAll> == 5'000'000);
static_assert(cw::deadline_nanos_for_binding<ForgePhaseDFuse_BoundedAll>()
              == 5'000'000);

// ── R019 hot-path — needs alloc ≤ 4 KB.  This 65 KB binding fails. ─
//
// Sanity: alloc IS > 4 KB.
static_assert(cf::bounded_alloc_v<ForgePhaseDFuse_BoundedAll> > 4096);
static_assert(!cr::R019_hot_path_resources_v<ForgePhaseDFuse_BoundedAll>);

// ── Cost-budget cross-check: predicted cost ≤ budget at n=64 ────
//
// cost = 200 + 5*64 = 520 ns (on Gpu mult=1).
// budget = 5'000'000 ns.  520 << 5'000'000 — passes.
static_assert(
    cr::cost_within_budget_v<ForgePhaseDFuse_BoundedAll, cc::CogKind::Gpu, 64>);

// At n=1'000'000, cost = 200 + 5'000'000 = 5'000'200 ns > 5'000'000 budget.
// Should fail.
static_assert(
    !cr::cost_within_budget_v<ForgePhaseDFuse_BoundedAll, cc::CogKind::Gpu, 1'000'000>);

// ── R014 BgWorker observable + bounded ──────────────────────────

using BgObservableBounded = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cg::with<crucible::effects::Effect::Bg, crucible::effects::Effect::Alloc>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cg::observability_visible,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::bounded_alloc<32768>,
    cg::wallclock_budget<1'000'000>>;        // both alloc + budget

static_assert(cr::R014_bg_observable_bounded_v<BgObservableBounded>);

// ── R019 hot-path — strict profile ─────────────────────────────

using HotStrict = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::terminating,
    cg::bounded_alloc<2048>,
    cg::bounded_io<0>>;

static_assert(cr::R019_hot_path_resources_v<HotStrict>);

// ── R020 federation-peer ────────────────────────────────────────

using FederationPeer = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::terminating,
    cg::wallclock_budget<10'000'000>>;       // 10 ms peer-step

static_assert(cr::R020_federation_peer_bounded_v<FederationPeer>);

}  // namespace

int main() {
    // Runtime smoke — read warden deadline from a grade.
    constexpr auto deadline =
        cw::deadline_nanos_for_binding<ForgePhaseDFuse_BoundedAll>();
    if (deadline != 5'000'000) {
        std::fprintf(stderr, "deadline mismatch: got %lu, want 5000000\n",
                     static_cast<unsigned long>(deadline));
        return 1;
    }

    std::fputs("test_fixy_termination: OK\n", stdout);
    return 0;
}
