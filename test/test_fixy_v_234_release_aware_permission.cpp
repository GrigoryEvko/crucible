// FIXY-V-234 positive integration test — SharedPermission + advise_release_aware.
//
// Exercises the full type-system-+-runtime composition that closes
// Agent 9 Bug 5 (MAP_SHARED reader-race vs MADV_DONTNEED):
//
//   step 1.  mint_permission_root<RegionTag>()
//                                — fresh exclusive Permission<RegionTag>
//   step 2.  SharedPermissionPool<RegionTag>{std::move(perm)}
//                                — pool now owns the exclusive token,
//                                  state = 0 (no shares out, not parked-out)
//   step 3.  pool.lend()         — yields a SharedPermissionGuard<RegionTag>
//                                — outstanding() == 1
//   step 4.  pool.try_upgrade()  — MUST FAIL: shares are out
//                                  (CAS guard observes count > 0)
//   step 5.  guard goes out of scope → count decrements to 0
//   step 6.  pool.try_upgrade()  — succeeds; returns Permission<RegionTag>
//   step 7.  advise_release_aware<DontNeed, RegionTag>(ctx, region, perm)
//                                — the type system witnesses unique
//                                  exclusive access via the const& borrow;
//                                  madvise(MADV_DONTNEED) succeeds
//   step 8.  pool.deposit_exclusive(std::move(perm))
//                                — pool back to lendable state
//   step 9.  pool.lend()         — succeeds again post-deposit
//
// The whole chain is the CSL borrow-discipline composition over an
// mmap'd region.  Linear semantics of Permission + atomic CAS state
// machine in SharedPermissionPool prove "no live reader at advise()".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/CollisionCatalog.h>

#include <cstdio>
#include <cstdlib>

namespace {

struct SenseHubRegion {};

// Pin the M001 catalog entry visible to this TU so a reviewer
// scanning the diff sees BOTH ends of the gate documented:
// the substrate-side rule entry AND the production-side
// composition demonstrating the gate.
using M001 = ::crucible::safety::fn::collision::M001_DontNeedRequiresReleaseAware;
static_assert(M001::name == std::string_view{"M001_DontNeedRequiresReleaseAware"});
static_assert(::crucible::safety::fn::collision::rule_bijection_v<
                  ::crucible::safety::fn::collision::RuleCode::M001>);

// ── FIXY-FOUND-013: phantom-claim refutation ───────────────────────────
// The task premise asserts M001 has "ZERO enforcement" — that's wrong.
// M001 is a ROUTING-CLASS collision rule (dispatched by Advice category),
// not a per-Fn pack-iteration rule.  Its enforcement model is concept-
// gated at the production-callsite of advise<>/advise_release_aware<>
// in fixy/Mmap.h, NOT a CollisionRules<Fn>::validate() pack walk.  The
// two concepts below ARE the M001 gate; deleting either silently flips
// M001 to actually-phantom.  We pin both polarities (safe-side rejects
// dangerous Advice, release-aware-side rejects non-dangerous Advice)
// so a future refactor that removed/renamed/inverted them red-lines
// THIS TU directly.
namespace fwmm_pin = ::crucible::fixy::wrap::mmap;
using SafeCtx   = ::crucible::effects::TestRunnerCtx;
struct M001PinRegion {};
// (1) safe surface accepts non-dangerous Advice
static_assert(fwmm_pin::CtxFitsSafeAdvise<SafeCtx, fwmm_pin::advice::HugePage>,
    "FIXY-FOUND-013: CtxFitsSafeAdvise must accept HugePage (non-dangerous).");
// (2) safe surface rejects dangerous Advice — M001's half-A
static_assert(!fwmm_pin::CtxFitsSafeAdvise<SafeCtx, fwmm_pin::advice::DontNeed>,
    "FIXY-FOUND-013: CtxFitsSafeAdvise must REJECT DontNeed; this is the "
    "production-callsite gate that routes dangerous Advice to "
    "advise_release_aware<>.  If this asserts: M001 became phantom.");
// (3) release-aware surface accepts dangerous Advice
static_assert(fwmm_pin::CtxFitsReleaseAwareAdvise<SafeCtx, fwmm_pin::advice::DontNeed,
                                                  M001PinRegion>,
    "FIXY-FOUND-013: CtxFitsReleaseAwareAdvise must accept DontNeed.");
// (4) release-aware surface rejects non-dangerous Advice — M001's half-B
static_assert(!fwmm_pin::CtxFitsReleaseAwareAdvise<SafeCtx, fwmm_pin::advice::HugePage,
                                                   M001PinRegion>,
    "FIXY-FOUND-013: CtxFitsReleaseAwareAdvise must REJECT HugePage; the "
    "release-aware surface is reserved for Advice that requires Permission-"
    "witnessed exclusive access.  If this asserts: M001's routing collapsed.");

void check(bool cond, const char* label) {
    if (!cond) [[unlikely]] {
        std::fprintf(stderr, "FIXY-V-234 positive test FAILED at: %s\n", label);
        std::abort();
    }
}

void integration_test() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace advice = fwmm::advice;
    namespace prot   = fwmm::prot;
    namespace share  = fwmm::share;

    using grant_prot   = ::crucible::fixy::grant::mmap::with_prot<prot::ReadWrite>;
    using grant_share  = ::crucible::fixy::grant::mmap::with_share<share::Anonymous>;

    ::crucible::effects::TestRunnerCtx ctx{};

    // ── steps 1-2 ── Mint root Permission, hand to pool ───────────────
    auto root_perm = ::crucible::safety::mint_permission_root<SenseHubRegion>();
    ::crucible::safety::SharedPermissionPool<SenseHubRegion> pool{std::move(root_perm)};

    check(pool.outstanding() == 0,                 "step 2: pool initial outstanding");
    check(!pool.is_exclusive_out(),                "step 2: pool initial not-exclusive-out");

    // ── step 3 ── Lend a shared guard; outstanding goes to 1 ─────────
    auto guard_opt = pool.lend();
    check(guard_opt.has_value(),                   "step 3: lend yields guard");
    check(pool.outstanding() == 1,                 "step 3: outstanding == 1 after lend");

    // ── step 4 ── try_upgrade must fail while a share is out ─────────
    auto upgrade_attempt_blocked = pool.try_upgrade();
    check(!upgrade_attempt_blocked.has_value(),    "step 4: try_upgrade blocked by live share");
    check(pool.outstanding() == 1,                 "step 4: outstanding unchanged on failed upgrade");

    // ── step 5 ── Drop the guard → outstanding goes back to 0 ────────
    guard_opt.reset();
    check(pool.outstanding() == 0,                 "step 5: outstanding == 0 after guard drop");

    // ── step 6 ── try_upgrade succeeds; we now hold exclusive proof ──
    auto excl_opt = pool.try_upgrade();
    check(excl_opt.has_value(),                    "step 6: try_upgrade yields exclusive Permission");
    check(pool.is_exclusive_out(),                 "step 6: pool reports exclusive-out");

    // ── step 7 ── mmap an anon region + advise_release_aware ─────────
    auto region_or = fwmm::mint_mmap_anon<SenseHubRegion, grant_prot, grant_share>(ctx, 4096u);
    check(region_or.has_value(),                   "step 7a: mmap anon succeeds");

    auto& region_linear = region_or.value();
    auto& region        = region_linear.peek_mut();  // advise mutates OwnedMmap state? no — but it takes T& signature; peek_mut yields T&
    check(region.is_mapped(),                      "step 7b: region is mapped");

    // The Permission<SenseHubRegion> const& borrow is the static proof
    // that no shared reader is in flight.  Pool atomicity confirms it.
    auto advise_result = fwmm::advise_release_aware<advice::DontNeed,
                                                    SenseHubRegion>(ctx, region, *excl_opt);
    check(advise_result.has_value(),               "step 7c: advise_release_aware DontNeed succeeds");

    // ── step 8 ── Deposit exclusive back to pool ─────────────────────
    pool.deposit_exclusive(std::move(*excl_opt));
    check(!pool.is_exclusive_out(),                "step 8: pool no longer exclusive-out");
    check(pool.outstanding() == 0,                 "step 8: outstanding == 0 after deposit");

    // ── step 9 ── Pool is lendable again ─────────────────────────────
    auto guard2_opt = pool.lend();
    check(guard2_opt.has_value(),                  "step 9: lend post-deposit succeeds");
    check(pool.outstanding() == 1,                 "step 9: outstanding == 1 after new lend");
}

}  // namespace

int main() {
    integration_test();
    return 0;
}
