// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-10 negative fixture #5/8:
// `fixy::substr::calendar_grid::mint_producer_session<
//      Grid, P, Ctx>(ctx, handle)` rejects when the first (ctx)
// parameter is NOT an IsExecCtx.
//
// `Grid` is supplied explicitly (it appears in non-deduced
// position via `typename Grid::template ProducerHandle<P>&`) and
// IS a known CalendarGridSessionSurface.  `P` is the producer
// row index (also non-deducible from the handle type).  The
// concept check on Grid passes; `Ctx` is deduced from the first
// argument as `int`; `IsExecCtx` rejects it.
//
// Distinct from fixture #6 (producer_session_wrong_handle): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the ProducerHandle<P> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_producer_session".

#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/fixy/Substr.h>

namespace fcal = ::crucible::fixy::substr::calendar_grid;
namespace conc = ::crucible::concurrent;

namespace neg_fixy_cal_producer_session_non_ctx {
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
    int not_a_ctx = 0;
    neg_fixy_cal_producer_session_non_ctx::Grid::ProducerHandle<0>* handle =
        nullptr;

    auto bad = fcal::mint_producer_session<
        neg_fixy_cal_producer_session_non_ctx::Grid, 0>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
