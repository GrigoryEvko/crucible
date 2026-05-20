// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074f fixture for fixy::substr::swmr::mint_writer_runtime_session:
// rejects a ReaderHandle (wrong role).  mint_writer_runtime_session
// takes `typename Swmr::WriterHandle&` (SwmrSession.h:241); passing a
// ReaderHandle fails type match at the call site, identically to
// neg_fixy_substr_swmr_wrong_handle.cpp's writer_session route.
//
// Distinct mismatch class from
// neg_fixy_substr_swmr_writer_runtime_session_non_ctx.cpp (role swap
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

namespace neg_fixy_substr_swmr_writer_runtime_session_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_swmr_writer_runtime_session_wrong_handle

int main() {
    using Snap = conc::PermissionedSnapshot<int,
        neg_fixy_substr_swmr_writer_runtime_session_wrong_handle::UserTag>;

    Snap snap{};
    auto reader = snap.reader();

    eff::BgCompileCtx ctx{};
    // Pass the ReaderHandle to the writer-runtime mint — expects
    // Snap::WriterHandle&.
    [[maybe_unused]] auto bad =
        fsubstr::swmr::mint_writer_runtime_session<Snap>(ctx, reader);
    return 0;
}
