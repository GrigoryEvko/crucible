// The compile-time checks of crucible/ledger/RefreshDaemon.h.

#include <crucible/ledger/RefreshDaemon.h>

namespace crucible::ledger {

static_assert(CtxFitsRefreshDaemon<LedgerIoCtx>);
static_assert(!CtxFitsRefreshDaemon<::fixy::HotFgCtx>, "a foreground context claims nothing and cannot refresh");
static_assert(!CtxFitsRefreshDaemon<::fixy::ColdInitCtx>, "initialization claims IO but not Block");
static_assert(!CtxFitsRefreshDaemon<::fixy::TestRunnerCtx>,
              "a fixture context claims IO and Block but not Bg, so it cannot stand in for a background thread");
static_assert(!CtxFitsRefreshDaemon<::fixy::BgCompileCtx>, "the compile context claims Bg and IO but not Block");

namespace refresh_daemon_detail::self_test {

// The backoff doubles and then stops doubling.
inline constexpr RefreshSchedule s_schedule{
    .idle_period_seconds = 3600,
    .initial_backoff_seconds = 60,
    .max_backoff_seconds = 480,
    .backoff_multiplier = 2,
};
static_assert(s_schedule.is_well_formed());
static_assert(s_schedule.next_backoff(0) == 60, "the first backoff is the initial one, not twice it");
static_assert(s_schedule.next_backoff(60) == 120);
static_assert(s_schedule.next_backoff(120) == 240);
static_assert(s_schedule.next_backoff(240) == 480);
static_assert(s_schedule.next_backoff(480) == 480, "the ceiling holds");
// A previous wait already past the ceiling stays at the ceiling rather
// than overflowing back down.
static_assert(s_schedule.next_backoff(0xFFFFFFFFu) == 480);

// A multiplier of one would not back off, so it is not well formed and
// the mint's precondition refuses it.
static_assert(!RefreshSchedule{.backoff_multiplier = 1}.is_well_formed());
static_assert(!RefreshSchedule{.idle_period_seconds = 0}.is_well_formed());
static_assert(!RefreshSchedule{.initial_backoff_seconds = 0}.is_well_formed());
static_assert(!RefreshSchedule{.initial_backoff_seconds = 600, .max_backoff_seconds = 60}.is_well_formed(),
              "a ceiling below the floor is not a range");
static_assert(RefreshSchedule{}.is_well_formed(), "the default schedule has to be usable as written");

// Only a cycle that got somewhere resets the backoff. A skipped cycle on
// an unfit host must back off, or an unfit host is probed every idle
// period forever.
static_assert(outcome_resets_backoff(CycleResult::Admitted));
static_assert(outcome_resets_backoff(CycleResult::NothingToDo));
static_assert(!outcome_resets_backoff(CycleResult::AllRefused));
static_assert(!outcome_resets_backoff(CycleResult::SkippedUnfitHost));
static_assert(!outcome_resets_backoff(CycleResult::CommitFailed));

static_assert(!std::is_copy_constructible_v<RefreshDaemon>);
static_assert(!std::is_move_constructible_v<RefreshDaemon>);

}  // namespace refresh_daemon_detail::self_test

}  // namespace crucible::ledger
