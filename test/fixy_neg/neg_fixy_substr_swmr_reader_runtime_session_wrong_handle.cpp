// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074f fixture for fixy::substr::swmr::mint_reader_runtime_session:
// rejects a WriterHandle (wrong role).  mint_reader_runtime_session
// takes `typename Swmr::ReaderHandle&` (SwmrSession.h:250); passing a
// WriterHandle fails type match — the role-inverse of the writer
// runtime fixture.
//
// Distinct mismatch class from
// neg_fixy_substr_swmr_reader_runtime_session_non_ctx.cpp (role swap
// vs non-ExecCtx).
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at WriterHandle vs ReaderHandle.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace conc    = crucible::concurrent;
namespace eff     = crucible::effects;
namespace fsafe   = crucible::safety;

namespace neg_fixy_substr_swmr_reader_runtime_session_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_swmr_reader_runtime_session_wrong_handle

int main() {
    using Snap = conc::PermissionedSnapshot<int,
        neg_fixy_substr_swmr_reader_runtime_session_wrong_handle::UserTag>;

    Snap snap{};
    auto writer = snap.writer(
        fsafe::mint_permission_root<typename Snap::writer_tag>());

    eff::BgCompileCtx ctx{};
    // Pass the WriterHandle to the reader-runtime mint — expects
    // Snap::ReaderHandle&.
    [[maybe_unused]] auto bad =
        fsubstr::swmr::mint_reader_runtime_session<Snap>(ctx, writer);
    return 0;
}
