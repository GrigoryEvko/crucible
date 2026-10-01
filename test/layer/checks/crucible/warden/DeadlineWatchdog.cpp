// The compile-time checks of crucible/warden/DeadlineWatchdog.h.

#include <crucible/warden/DeadlineWatchdog.h>

namespace crucible::warden {

static_assert(sizeof(DeadlineWatchdog) <= 64, "DeadlineWatchdog must fit in one cache line");

namespace detail::deadline_watchdog_mint_check {
namespace fe = ::foundation::effects;
using ColdInit = fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO>>;
using InitLoad = fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO, fe::Effect::Block>>;
using BgLoad = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Alloc, fe::Effect::IO, fe::Effect::Block>>;
using TestRunner =
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::Alloc, fe::Effect::IO, fe::Effect::Block>>;
static_assert(CtxFitsDeadlineWatchdogMint<ColdInit> && CtxFitsDeadlineWatchdogMint<InitLoad>);
static_assert(!CtxFitsDeadlineWatchdogMint<BgLoad> && !CtxFitsDeadlineWatchdogMint<TestRunner>);
static_assert(!CtxFitsDeadlineWatchdogMint<fe::ExecCtx<>>);
static_assert(CtxFitsDeadlineWatchdog<BgLoad> && CtxFitsDeadlineWatchdog<ColdInit>
              && CtxFitsDeadlineWatchdog<TestRunner>);
static_assert(!CtxFitsDeadlineWatchdog<::fixy::HotFgCtx> && !CtxFitsDeadlineWatchdog<int>);
static_assert(!std::is_constructible_v<DeadlineWatchdog, const ::crucible::perf::Senses*, const Policy&>,
              "The constructor is private.  A watchdog comes only from mint_deadline_watchdog.");
}  // namespace detail::deadline_watchdog_mint_check

}  // namespace crucible::warden
