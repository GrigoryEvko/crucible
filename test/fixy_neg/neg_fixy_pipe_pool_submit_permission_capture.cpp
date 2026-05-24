// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-215 HS14 fixture #1 of 2 for fixy::pipe::pool_submit:
// PermissionFreeJob rejects when the supplied closure captures a
// move-only Permission<Tag> token by value.
//
// Violation: a closure capturing `Permission<MyTag>` by value
// inherits the captured type's deleted copy ctor — the closure's
// std::is_copy_constructible_v is FALSE.  PermissionFreeJob's
// std::is_copy_constructible_v requirement therefore fails, and
// CtxFitsPoolSubmit's PermissionFreeJob conjunct rejects.
//
// THIS IS THE CANONICAL §IX permission-bypass shape that V-215
// exists to reject.  Without the fixy:: gate the closure would
// silently transfer ownership of perm to a Pool worker thread,
// bypassing the CSL parallel-rule discipline (splits_into_pack +
// structured fork-join + RAII rejoin) that `mint_permission_fork`
// enforces.
//
// Distinct from fixture #2 (ctx_no_bg):
//   * Fixture #1 — Ctx admits Bg (BgDrainCtx::row contains Bg) but
//     the JOB shape is wrong (closure non-copyable due to captured
//     Permission).  PermissionFreeJob fails; CtxOwnsCapability<Bg>
//     would have passed.
//   * Fixture #2 — Ctx does NOT admit Bg (HotFgCtx::row = Row<>),
//     but the JOB is copy-constructible.  CtxOwnsCapability<Bg>
//     fails; PermissionFreeJob would have passed.
// Two distinct CtxFitsPoolSubmit rejection axes ⇒ HS14 floor
// satisfied.
//
// Expected diagnostic: CtxFitsPoolSubmit / PermissionFreeJob /
// std::is_copy_constructible_v constraint is not satisfied.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>
#include <crucible/permissions/Permission.h>

#include <utility>

namespace neg_fixy_pipe_pool_submit_permission_capture {

// Phantom tag for the permission token captured by the bypass
// closure.  The exact tag does not matter — the rejection rides
// only on Permission<Tag>'s deleted copy ctor.
struct PermTag {};

}  // namespace neg_fixy_pipe_pool_submit_permission_capture

int main() {
    namespace tags  = neg_fixy_pipe_pool_submit_permission_capture;
    namespace eff   = ::crucible::effects;
    namespace fpipe = ::crucible::fixy::pipe;
    namespace safe  = ::crucible::safety;

    eff::BgDrainCtx bg{};
    fpipe::Pool<> pool{fpipe::CoreCount{1}};

    // Mint a Permission token in the foreground.  In production this
    // would be the CSL parent that `mint_permission_fork` consumes.
    auto perm = safe::mint_permission_root<tags::PermTag>();

    // THE BYPASS CLOSURE: captures perm by value via std::move.  The
    // closure's implicit copy ctor is now deleted, so
    // std::is_copy_constructible_v<decltype(bypass)> == false →
    // PermissionFreeJob<decltype(bypass)> == false →
    // CtxFitsPoolSubmit<BgDrainCtx, decltype(bypass)> == false →
    // pool_submit's requires-clause rejects.
    auto bypass = [captured = std::move(perm)]() mutable noexcept {
        (void)captured;
    };

    // This call MUST fail to compile.  If it ever succeeds, the §IX
    // permission-bypass closure has slipped through the fixy:: gate
    // and a worker jthread will silently inherit `perm` without the
    // CSL parallel-rule's static splits_into_pack check.
    fpipe::pool_submit(bg, pool, std::move(bypass));

    pool.wait_idle();
    return 0;
}
