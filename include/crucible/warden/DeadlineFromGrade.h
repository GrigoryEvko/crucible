#pragma once

// ── crucible::warden — DeadlineFromGrade.h (FIXY-G12) ────────────────
//
// Grade-to-deadline lookup helper.  Bridges fixy::wallclock_budget_v<F>
// from a fixy binding to the warden's deadline runtime — eliminating
// per-callsite deadline boilerplate.  When a binding declares
// `cg::wallclock_budget<Nanos>`, calling
// `warden::deadline_from_grade<F>()` returns the Nanos as a configured
// deadline.  The runtime arming machinery (DeadlineWatchdog) takes
// over from there.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   template <typename F>
//   inline constexpr std::uint64_t deadline_from_grade_v
//                                  — wallclock_budget_v<F> wired through
//                                    fn_wallclock_budget.  UINT64_MAX
//                                    when F has no wallclock_budget.
//
//   has_deadline_v<F>              — true iff deadline is set.
//
// ── References ──────────────────────────────────────────────────────
//
//   warden/Policy.h                — production deadline runtime
//   fixy/dim/Termination.h         — wallclock_budget grant + extractor

#include <crucible/fixy/dim/Termination.h>

#include <cstdint>

namespace crucible::warden {

template <typename F>
inline constexpr std::uint64_t deadline_from_grade_v =
    ::crucible::fixy::wallclock_budget_v<F>;

template <typename F>
inline constexpr bool has_deadline_v =
    deadline_from_grade_v<F> != UINT64_MAX;

// Production-side helper (template; declared inline) that reads a
// binding's wallclock_budget grade and returns the deadline.  Used at
// admission sites where a Bg-context task is being scheduled.  The
// caller decides what to do with the result — typically passes it to
// DeadlineWatchdog::arm_for_thread().

template <typename F>
[[nodiscard]] inline constexpr std::uint64_t deadline_nanos_for_binding() noexcept {
    return deadline_from_grade_v<F>;
}

}  // namespace crucible::warden
