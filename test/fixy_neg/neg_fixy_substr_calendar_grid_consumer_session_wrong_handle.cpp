// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Calendar-grid session mint negative fixture 8 of 8:
// `safety::proto::calendar_grid_session::mint_consumer_session<
//      Grid, Ctx>(ctx, handle)` rejects when the second (handle)
// parameter cannot bind to `typename Grid::ConsumerHandle&`.
//
// Mirrors fixture #6 (producer_session_wrong_handle) on the
// consumer side: proves the ConsumerHandle reference binding
// fires INDEPENDENTLY of the producer-side instantiation.  Distinct
// from the producer-side because the consumer signature does NOT
// carry the producer-row index P.
//
// Distinct from fixture #7 (consumer_session_non_ctx): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ConsumerHandle reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_consumer_session'" / "cannot convert" / "ConsumerHandle"
// / "mint_consumer_session".

#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/CalendarGridSession.h>

namespace fcal = ::crucible::safety::proto::calendar_grid_session;
namespace conc = ::crucible::concurrent;
namespace eff = ::crucible::effects;

namespace neg_fixy_cal_consumer_session_wrong_handle {
struct UserTag {};
struct Job {
    std::uint64_t deadline_ns = 0;
};
struct Key {
    static std::uint64_t key(Job const& job) noexcept { return job.deadline_ns; }
};
using Grid = conc::PermissionedCalendarGrid<Job, 2, 8, 16, Key, 1000000ULL, UserTag>;
}  // namespace neg_fixy_cal_consumer_session_wrong_handle

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fcal::mint_consumer_session<neg_fixy_cal_consumer_session_wrong_handle::Grid>(ctx, not_a_handle);
    (void)bad;
    return 0;
}
