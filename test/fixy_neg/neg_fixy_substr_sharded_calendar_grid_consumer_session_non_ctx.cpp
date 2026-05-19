// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-11 negative fixture #7/8:
// `fixy::substr::sharded_calendar_grid::mint_consumer_session<
//      Grid, S, Ctx>(ctx, handle)` rejects when the first (ctx)
// parameter is NOT an IsExecCtx.
//
// Mirrors fixture #5 (producer_session_non_ctx) on the consumer
// side: proves the IsExecCtx prerequisite is preserved through
// the using-decl INDEPENDENTLY of the producer-side
// instantiation.  Both sharded sides carry the non-deducible
// shard index `S` (unlike CalendarGrid where only the producer
// carries `P`).
//
// Distinct from fixture #8 (consumer_session_wrong_handle): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ConsumerHandle<S> reference binding (second
// parameter slot) AFTER IsExecCtx succeeds.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_consumer_session".

#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/fixy/Substr.h>

namespace fscal = ::crucible::fixy::substr::sharded_calendar_grid;
namespace conc  = ::crucible::concurrent;

namespace neg_fixy_scal_consumer_session_non_ctx {
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
    neg_fixy_scal_consumer_session_non_ctx::Grid::template ConsumerHandle<0>*
        handle = nullptr;

    auto bad = fscal::mint_consumer_session<
        neg_fixy_scal_consumer_session_non_ctx::Grid, 0>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
