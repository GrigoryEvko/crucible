// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-11 negative fixture #6/8:
// `fixy::substr::sharded_calendar_grid::mint_producer_session<
//      Grid, S, Ctx>(ctx, handle)` rejects when the second (handle)
// parameter cannot bind to
// `typename Grid::template ProducerHandle<S>&`.
//
// Distinct from CalendarGrid's producer_session_wrong_handle: the
// sharded variant carries the non-deducible shard index `S` (NOT
// producer-row `P`).  Proves the ProducerHandle<S> reference
// binding is preserved through the using-decl re-export.
//
// Distinct from fixture #5 (producer_session_non_ctx): #5
// exercises the IsExecCtx prerequisite (first parameter slot);
// #6 exercises the ProducerHandle<S> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_producer_session'" / "cannot convert" / "ProducerHandle"
// / "mint_producer_session".

#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fscal = ::crucible::fixy::substr::sharded_calendar_grid;
namespace conc  = ::crucible::concurrent;
namespace eff   = ::crucible::effects;

namespace neg_fixy_scal_producer_session_wrong_handle {
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
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fscal::mint_producer_session<
        neg_fixy_scal_producer_session_wrong_handle::Grid, 0>(
            ctx, not_a_handle);
    (void)bad;
    return 0;
}
