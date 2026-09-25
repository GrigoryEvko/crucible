// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Calendar-grid session mint negative fixture 7 of 8:
// `safety::proto::calendar_grid_session::mint_consumer_session<
//      Grid, Ctx>(ctx, handle)` rejects when the first (ctx)
// parameter is NOT an IsExecCtx.
//
// Mirrors fixture #5 (producer_session_non_ctx) on the consumer
// side: proves the IsExecCtx prerequisite fires INDEPENDENTLY of
// the producer-side instantiation.  Distinct from the producer-side
// because the consumer signature does NOT carry the producer-row
// index P.
//
// Distinct from fixture #8 (consumer_session_wrong_handle): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ConsumerHandle reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_consumer_session".

#include <crucible/concurrent/_PermissionedCalendarGrid.h>
#include <crucible/sessions/_CalendarGridSession.h>

namespace fcal = ::crucible::safety::proto::calendar_grid_session;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_cal_consumer_session_non_ctx {
struct UserTag {};
struct Job {
    std::uint64_t deadline_ns = 0;
};
struct Key {
    static std::uint64_t key(Job const& job) noexcept { return job.deadline_ns; }
};
using Grid = conc::PermissionedCalendarGrid<Job, 2, 8, 16, Key, 1000000ULL, UserTag>;
}  // namespace neg_fixy_cal_consumer_session_non_ctx

int main() {
    int not_a_ctx = 0;
    neg_fixy_cal_consumer_session_non_ctx::Grid::ConsumerHandle* handle = nullptr;

    auto bad = fcal::mint_consumer_session<neg_fixy_cal_consumer_session_non_ctx::Grid>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
