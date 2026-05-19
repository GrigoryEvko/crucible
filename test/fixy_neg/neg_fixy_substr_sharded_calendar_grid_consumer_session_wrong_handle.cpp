// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-11 negative fixture #8/8:
// `fixy::substr::sharded_calendar_grid::mint_consumer_session<
//      Grid, S, Ctx>(ctx, handle)` rejects when the second (handle)
// parameter cannot bind to
// `typename Grid::template ConsumerHandle<S>&`.
//
// Mirrors fixture #6 (producer_session_wrong_handle) on the
// consumer side: proves the ConsumerHandle<S> reference binding
// is preserved through the using-decl INDEPENDENTLY of the
// producer-side instantiation.  Both sharded sides carry the
// non-deducible shard index `S` (unlike CalendarGrid where only
// the producer carries `P`).
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
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fscal = ::crucible::fixy::substr::sharded_calendar_grid;
namespace conc  = ::crucible::concurrent;
namespace eff   = ::crucible::effects;

namespace neg_fixy_scal_consumer_session_wrong_handle {
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

    auto bad = fscal::mint_consumer_session<
        neg_fixy_scal_consumer_session_wrong_handle::Grid, 0>(
            ctx, not_a_handle);
    (void)bad;
    return 0;
}
