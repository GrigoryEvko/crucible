// The compile-time checks of crucible/warden/DeadlineWatchdog.h.

#include <crucible/warden/DeadlineWatchdog.h>

namespace crucible::warden {

static_assert(sizeof(DeadlineWatchdog) <= 64, "DeadlineWatchdog must fit in one cache line");

namespace detail::deadline_watchdog_mint_check {
namespace fe = ::foundation::effects;
using ColdInit = fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO>>;
using InitLoad = ::fixy::InitLoadCtx;
using BgLoad = ::fixy::BgLoadCtx;
using TestRunner = ::fixy::TestRunnerCtx;
static_assert(CtxFitsDeadlineWatchdogMint<ColdInit> && CtxFitsDeadlineWatchdogMint<InitLoad>);
static_assert(!CtxFitsDeadlineWatchdogMint<BgLoad> && !CtxFitsDeadlineWatchdogMint<TestRunner>);
static_assert(!CtxFitsDeadlineWatchdogMint<fe::ExecCtx<>>);
// An observation reads a clock, so its context holds Nondet.  The cold
// init row and the drain row hold none.
static_assert(CtxFitsDeadlineWatchdog<BgLoad> && CtxFitsDeadlineWatchdog<InitLoad>
              && CtxFitsDeadlineWatchdog<TestRunner> && CtxFitsDeadlineWatchdog<::fixy::BgObserveCtx>);
static_assert(!CtxFitsDeadlineWatchdog<ColdInit> && !CtxFitsDeadlineWatchdog<::fixy::BgDrainCtx>);
static_assert(!CtxFitsDeadlineWatchdog<::fixy::HotFgCtx> && !CtxFitsDeadlineWatchdog<int>);
static_assert(!std::is_constructible_v<DeadlineWatchdog, WatchdogSource, const Policy&>,
              "The constructor is private.  A watchdog comes only from mint_deadline_watchdog.");
static_assert(sizeof(WatchdogSource) == 2 * sizeof(void*),
              "The source of a watchdog is the Box of its Senses and the tag of the Option.");
static_assert(!std::is_convertible_v<const ::crucible::perf::Senses*, WatchdogSource>,
              "A raw pointer is no source.  A source is a Box of a Senses that the watchdog owns, or it is none.");
static_assert(!std::is_copy_constructible_v<WatchdogSource>,
              "The watchdog owns its source, so a source does not copy.");
}  // namespace detail::deadline_watchdog_mint_check

}  // namespace crucible::warden
