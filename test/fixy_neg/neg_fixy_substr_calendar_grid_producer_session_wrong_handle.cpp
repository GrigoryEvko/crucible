// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-10 negative fixture #6/8:
// `fixy::substr::calendar_grid::mint_producer_session<
//      Grid, P, Ctx>(ctx, handle)` rejects when the second
// (handle) parameter cannot bind to
// `typename Grid::template ProducerHandle<P>&`.
//
// `Grid` and `P` are supplied explicitly; Grid IS a known
// CalendarGridSessionSurface.  `HotFgCtx` IS IsExecCtx.  All
// upstream concept gates pass; the second parameter `int`
// cannot bind to `Grid::ProducerHandle<P>&` (a class reference).
//
// Distinct from fixture #5 (producer_session_non_ctx): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the ProducerHandle<P> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_producer_session'" / "cannot convert" / "ProducerHandle"
// / "mint_producer_session".

#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fcal = ::crucible::fixy::substr::calendar_grid;
namespace conc = ::crucible::concurrent;
namespace eff  = ::crucible::effects;

namespace neg_fixy_cal_producer_session_wrong_handle {
struct UserTag {};
struct Job {
    std::uint64_t deadline_ns = 0;
};
struct Key {
    static std::uint64_t key(Job const& job) noexcept {
        return job.deadline_ns;
    }
};
using Grid = conc::PermissionedCalendarGrid<
    Job, 2, 8, 16, Key, 1'000'000ULL, UserTag>;
}

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fcal::mint_producer_session<
        neg_fixy_cal_producer_session_wrong_handle::Grid, 0>(
            ctx, not_a_handle);
    (void)bad;
    return 0;
}
