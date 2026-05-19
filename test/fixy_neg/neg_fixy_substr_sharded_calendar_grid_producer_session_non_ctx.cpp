// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-11 negative fixture #5/8:
// `fixy::substr::sharded_calendar_grid::mint_producer_session<
//      Grid, S, Ctx>(ctx, handle)` rejects when the first (ctx)
// parameter is NOT an IsExecCtx.
//
// Distinct from CalendarGrid's producer_session_non_ctx fixture:
// the sharded variant carries the non-deducible shard index `S`
// (NOT producer-row `P`), preserved through the using-decl
// re-export.  Proves the IsExecCtx prerequisite holds on the
// sharded mint independently of the CalendarGrid instantiation.
//
// Distinct from fixture #6 (producer_session_wrong_handle): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the ProducerHandle<S> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_producer_session".

#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/fixy/Substr.h>

namespace fscal = ::crucible::fixy::substr::sharded_calendar_grid;
namespace conc  = ::crucible::concurrent;

namespace neg_fixy_scal_producer_session_non_ctx {
struct UserTag {};
struct Job {
    std::uint64_t deadline_ns = 0;
};
struct Key {
    static std::uint64_t key(Job const& job) noexcept {
        return job.deadline_ns;
    }
};
using Grid = conc::PermissionedShardedCalendarGrid<
    Job, 2, 8, 16, Key, 1'000'000ULL, UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_scal_producer_session_non_ctx::Grid::template ProducerHandle<0>*
        handle = nullptr;

    auto bad = fscal::mint_producer_session<
        neg_fixy_scal_producer_session_non_ctx::Grid, 0>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
