// Releasing the pages of a mapped region while a reader is still in flight
// is a race.  The exclusive token, which the pool hands back only once
// every shared guard is gone, is the proof that no reader remains.  This
// test walks that borrow discipline end to end.

#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/CollisionCatalog.h>

#include <cstdio>
#include <cstdlib>

namespace {

struct SenseHubRegion {};

// Pinning the catalog entry here puts both ends of the gate in one place:
// the rule entry and the composition that demonstrates it.
using M001 = ::crucible::safety::fn::collision::M001_DontNeedRequiresReleaseAware;
static_assert(M001::name == std::string_view{"M001_DontNeedRequiresReleaseAware"});
static_assert(::crucible::safety::fn::collision::rule_bijection_v<::crucible::safety::fn::collision::RuleCode::M001>);

// This rule is a routing rule, dispatched by advice category rather than
// walked per function.  The two concepts below are the whole of it, so
// both polarities are pinned here: a refactor that removes, renames or
// inverts either one leaves the rule enforcing nothing and reds this file
// on the way.
namespace fwmm_pin = ::crucible::fixy::wrap::mmap;
using SafeCtx = ::crucible::effects::TestRunnerCtx;
struct M001PinRegion {};
static_assert(fwmm_pin::CtxFitsSafeAdvise<SafeCtx, fwmm_pin::advice::HugePage>,
              "CtxFitsSafeAdvise must accept HugePage, which is not dangerous");
static_assert(!fwmm_pin::CtxFitsSafeAdvise<SafeCtx, fwmm_pin::advice::DontNeed>,
              "CtxFitsSafeAdvise must reject DontNeed; this is the call-site gate "
              "that routes dangerous advice to the release-aware surface");
static_assert(fwmm_pin::CtxFitsReleaseAwareAdvise<SafeCtx, fwmm_pin::advice::DontNeed, M001PinRegion>,
              "CtxFitsReleaseAwareAdvise must accept DontNeed");
static_assert(!fwmm_pin::CtxFitsReleaseAwareAdvise<SafeCtx, fwmm_pin::advice::HugePage, M001PinRegion>,
              "CtxFitsReleaseAwareAdvise must reject HugePage; the release-aware "
              "surface is reserved for advice that needs permission-witnessed "
              "exclusive access");

void check(bool cond, const char* label) {
    if (!cond) [[unlikely]] {
        std::fprintf(stderr, "release-aware advise test FAILED at: %s\n", label);
        std::abort();
    }
}

void integration_test() {
    namespace fwmm = ::crucible::fixy::wrap::mmap;
    namespace advice = fwmm::advice;
    namespace prot = fwmm::prot;
    namespace share = fwmm::share;

    using grant_prot = ::crucible::fixy::grant::mmap::with_prot<prot::ReadWrite>;
    using grant_share = ::crucible::fixy::grant::mmap::with_share<share::Anonymous>;

    ::crucible::effects::TestRunnerCtx ctx{};

    auto root_perm = ::crucible::safety::mint_permission_root<SenseHubRegion>();
    ::crucible::safety::SharedPermissionPool<SenseHubRegion> pool{std::move(root_perm)};

    check(pool.outstanding() == 0, "step 2: pool initial outstanding");
    check(!pool.is_exclusive_out(), "step 2: pool initial not-exclusive-out");

    auto guard_opt = pool.lend();
    check(guard_opt.has_value(), "step 3: lend yields guard");
    check(pool.outstanding() == 1, "step 3: outstanding == 1 after lend");

    auto upgrade_attempt_blocked = pool.try_upgrade();
    check(!upgrade_attempt_blocked.has_value(), "step 4: try_upgrade blocked by live share");
    check(pool.outstanding() == 1, "step 4: outstanding unchanged on failed upgrade");

    guard_opt.reset();
    check(pool.outstanding() == 0, "step 5: outstanding == 0 after guard drop");

    auto excl_opt = pool.try_upgrade();
    check(excl_opt.has_value(), "step 6: try_upgrade yields exclusive Permission");
    check(pool.is_exclusive_out(), "step 6: pool reports exclusive-out");

    auto region_or = fwmm::mint_mmap_anon<SenseHubRegion, grant_prot, grant_share>(ctx, 4096u);
    check(region_or.has_value(), "step 7a: mmap anon succeeds");

    auto& region_linear = region_or.value();
    auto& region = region_linear.peek_mut();
    check(region.is_mapped(), "step 7b: region is mapped");

    // The borrowed permission is the static proof that no shared reader is
    // in flight.  The pool's atomic state is what makes the proof true.
    auto advise_result = fwmm::advise_release_aware<advice::DontNeed, SenseHubRegion>(ctx, region, *excl_opt);
    check(advise_result.has_value(), "step 7c: advise_release_aware DontNeed succeeds");

    pool.deposit_exclusive(std::move(*excl_opt));
    check(!pool.is_exclusive_out(), "step 8: pool no longer exclusive-out");
    check(pool.outstanding() == 0, "step 8: outstanding == 0 after deposit");

    auto guard2_opt = pool.lend();
    check(guard2_opt.has_value(), "step 9: lend post-deposit succeeds");
    check(pool.outstanding() == 1, "step 9: outstanding == 1 after new lend");
}

}  // namespace

int main() {
    integration_test();
    return 0;
}
