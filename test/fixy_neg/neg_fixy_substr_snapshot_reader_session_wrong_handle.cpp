// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Snapshot session mint fixture for
// safety::proto::snapshot_session::mint_snapshot_reader_session:
// rejects a WriterHandle (wrong role).  mint_snapshot_reader_session
// takes `typename Snap::ReaderHandle&` (sessions/SnapshotSession.h);
// passing a WriterHandle fails type match — the role-inverse of
// neg_fixy_substr_snapshot_wrong_handle.cpp (which passes a
// ReaderHandle to mint_snapshot_writer_session).
//
// Distinct mismatch class from
// neg_fixy_substr_snapshot_reader_session_non_ctx.cpp (role swap vs
// non-ExecCtx).
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at WriterHandle vs ReaderHandle.

#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_SnapshotSession.h>

namespace fsnap = ::crucible::safety::proto::snapshot_session;
namespace conc = crucible::concurrent;
namespace eff = crucible::effects;
namespace fsafe = crucible::safety;

namespace neg_fixy_substr_snapshot_reader_session_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_snapshot_reader_session_wrong_handle

int main() {
    using Snap = conc::PermissionedSnapshot<int, neg_fixy_substr_snapshot_reader_session_wrong_handle::UserTag>;

    Snap snap{};
    auto writer = snap.writer(fsafe::mint_permission_root<typename Snap::writer_tag>());

    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    // Pass the WriterHandle to mint_snapshot_reader_session — expects
    // Snap::ReaderHandle&.
    [[maybe_unused]] auto bad = fsnap::mint_snapshot_reader_session<Snap>(ctx, writer);
    return 0;
}
