// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Sharded calendar-grid session mint negative fixture 8 of 8:
// `safety::proto::sharded_calendar_grid_session::mint_consumer_session<
//      Grid, S, Ctx>(ctx, handle)` rejects when the second (handle)
// parameter cannot bind to
// `typename Grid::template ConsumerHandle<S>&`.
//
// Mirrors fixture #6 (producer_session_wrong_handle) on the
// consumer side: proves the ConsumerHandle<S> reference binding
// fires INDEPENDENTLY of the producer-side instantiation.  Both
// sharded sides carry the non-deducible shard index `S` (unlike
// CalendarGrid where only the producer carries `P`).
//
// Distinct from fixture #7 (consumer_session_non_ctx): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ConsumerHandle<S> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "no matching function for call to
// 'mint_consumer_session'" / "cannot convert" / "ConsumerHandle"
// / "mint_consumer_session".

#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/ShardedCalendarGridSession.h>

namespace fscal = ::crucible::safety::proto::sharded_calendar_grid_session;
namespace conc = ::crucible::concurrent;
namespace eff = ::crucible::effects;

namespace neg_fixy_scal_consumer_session_wrong_handle {
struct UserTag {};
struct Job {
    std::uint64_t deadline_ns = 0;
};
struct Key {
    static std::uint64_t key(Job const& job) noexcept { return job.deadline_ns; }
};
using Grid = conc::PermissionedShardedCalendarGrid<Job, 2, 8, 16, Key, 1000000ULL, UserTag>;
}  // namespace neg_fixy_scal_consumer_session_wrong_handle

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fscal::mint_consumer_session<neg_fixy_scal_consumer_session_wrong_handle::Grid, 0>(ctx, not_a_handle);
    (void)bad;
    return 0;
}
