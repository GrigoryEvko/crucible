// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SWMR runtime session mint fixture for
// safety::proto::swmr_session::mint_reader_runtime_session:
// rejects a WriterHandle (wrong role).  mint_reader_runtime_session
// takes `typename Swmr::ReaderHandle&` (sessions/_SwmrSession.h); passing a
// WriterHandle fails type match — the role-inverse of the writer
// runtime fixture.
//
// Distinct mismatch class from
// neg_fixy_substr_swmr_reader_runtime_session_non_ctx.cpp (role swap
// vs non-ExecCtx).
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at WriterHandle vs ReaderHandle.

#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_SwmrSession.h>

namespace fswmr = ::crucible::safety::proto::swmr_session;
namespace conc = crucible::concurrent;
namespace eff = crucible::effects;
namespace fsafe = crucible::safety;

namespace neg_fixy_substr_swmr_reader_runtime_session_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_swmr_reader_runtime_session_wrong_handle

int main() {
    using Snap = conc::PermissionedSnapshot<int, neg_fixy_substr_swmr_reader_runtime_session_wrong_handle::UserTag>;

    Snap snap{};
    auto writer = snap.writer(fsafe::mint_permission_root<typename Snap::writer_tag>());

    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    // Pass the WriterHandle to the reader-runtime mint — expects
    // Snap::ReaderHandle&.
    [[maybe_unused]] auto bad = fswmr::mint_reader_runtime_session<Snap>(ctx, writer);
    return 0;
}
