// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074e fixture #2 for fixy::substr::swmr::mint_reader_session:
// rejects a WriterHandle (wrong role).
//
// `mint_reader_session<Swmr>(ctx, handle)` takes
// `typename Swmr::ReaderHandle&`.  Passing a WriterHandle fails type
// match at the call site — the role-inverse of
// neg_fixy_substr_swmr_wrong_handle.cpp (which passes a ReaderHandle
// to mint_writer_session).  Routing through
// `fixy::substr::swmr::mint_reader_session` must reject identically,
// proving the §XXI role-discriminating signature survives the fixy
// re-export.
//
// Distinct mismatch class from neg_fixy_substr_swmr_reader_session_non_ctx
// (#3, non-ExecCtx): this supplies a valid HotFg-class ctx but the
// wrong handle ROLE; #3 supplies the right handle but a non-ExecCtx ctx.
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

namespace neg_fixy_substr_swmr_reader_session_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_swmr_reader_session_wrong_handle

int main() {
    using Snap = conc::PermissionedSnapshot<int,
        neg_fixy_substr_swmr_reader_session_wrong_handle::UserTag>;

    Snap snap{};
    auto writer = snap.writer(
        fsafe::mint_permission_root<typename Snap::writer_tag>());

    eff::BgCompileCtx ctx{};
    // Pass the WriterHandle to mint_reader_session — fails because the
    // reader-session mint expects Snap::ReaderHandle&.
    [[maybe_unused]] auto bad =
        fsubstr::swmr::mint_reader_session<Snap>(ctx, writer);
    return 0;
}
