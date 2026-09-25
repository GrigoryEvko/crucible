// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Snapshot session mint negative fixture 1 of 2 (HS14 ≥2 floor):
// role-mismatch route.
//
// `safety::proto::snapshot_session::mint_snapshot_writer_session<Snap>(
// ctx, handle)` takes `typename Snap::WriterHandle&`.  Passing a
// `ReaderHandle` (wrong role) fails type match at the call site.  The
// fixture proves the role-discriminating signature of the §XXI
// Universal Mint Pattern.
//
// Reject sequence: template instantiation begins with Snap fixed →
// non-deducible second-parameter type is `Snap::WriterHandle&` →
// caller-provided argument is `ReaderHandle&` (the wrong nested type)
// → no implicit conversion exists → overload resolution fails.
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at WriterHandle vs ReaderHandle.

#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_SnapshotSession.h>

#include <utility>

namespace fsnap = ::crucible::safety::proto::snapshot_session;
namespace conc = crucible::concurrent;
namespace eff = crucible::effects;
namespace fsafe = crucible::safety;

namespace neg_fixy_substr_snapshot_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_snapshot_wrong_handle

int main() {
    using Snap = conc::PermissionedSnapshot<int, neg_fixy_substr_snapshot_wrong_handle::UserTag>;

    Snap snap{};
    auto writer = snap.writer(fsafe::mint_permission_root<typename Snap::writer_tag>());
    auto reader_opt = snap.reader();
    (void)writer;

    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    // Pass the (optional unwrapped) ReaderHandle to the WRITER session
    // mint — fails because mint_snapshot_writer_session expects
    // `Snap::WriterHandle&`.
    [[maybe_unused]] auto bad = fsnap::mint_snapshot_writer_session<Snap>(ctx, *reader_opt);
    return 0;
}
