// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-224 fixture #4 — sync<None> rejected.
//
// `CtxFitsSync<Ctx, SyncOp>` requires `SyncOp ≠ sync_op::None`.
// Calling `sync<sync_op::None>(ctx, handle)` is unambiguous
// programmer error — the type-tag namespace partitions sync ops
// across {None, Fdatasync, Fsync, FsyncParentDir, Msync}, and
// `None` is the "do nothing" sentinel that has no business
// reaching the syscall surface.  The type system refuses the call
// instead of letting the no-op syscall happen silently.
//
// Mismatch class: sync_op::None engaged — the sentinel tag is
// admitted to the type partition but explicitly rejected at the
// concept gate.  Distinct from fixtures #1-#3 (all on mint_file).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "no matching function" /
//   "CtxFitsSync" / "sync_op::None".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>           // fixy::tags::source::*
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwfs = ::crucible::fixy::wrap::fs;
    namespace so   = fwfs::sync_op;

    ::crucible::effects::TestRunnerCtx ctx{};

    // We don't need a real fd — the concept check fires BEFORE
    // the body runs; a default-constructed handle suffices.
    ::crucible::safety::FileHandle h{};

    // Should FAIL: sync<sync_op::None> — CtxFitsSync's
    // `!std::is_same_v<SyncOp, sync_op::None>` clause is false;
    // requires-clause refuses the instantiation.
    [[maybe_unused]] auto r = fwfs::sync<so::None>(ctx, h);
    return 0;
}
