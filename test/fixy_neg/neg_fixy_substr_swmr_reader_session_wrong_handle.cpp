// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SWMR session mint fixture 2 for
// safety::proto::swmr_session::mint_reader_session:
// rejects a WriterHandle (wrong role).
//
// `mint_reader_session<Swmr>(ctx, handle)` takes
// `typename Swmr::ReaderHandle&`.  Passing a WriterHandle fails type
// match at the call site — the role-inverse of
// neg_fixy_substr_swmr_wrong_handle.cpp (which passes a ReaderHandle
// to mint_writer_session).  The fixture proves the §XXI
// role-discriminating signature.
//
// Distinct mismatch class from neg_fixy_substr_swmr_reader_session_non_ctx
// (#3, non-ExecCtx): this supplies a valid HotFg-class ctx but the
// wrong handle ROLE; #3 supplies the right handle but a non-ExecCtx ctx.
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

namespace neg_fixy_substr_swmr_reader_session_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_swmr_reader_session_wrong_handle

int main() {
    using Snap = conc::PermissionedSnapshot<int, neg_fixy_substr_swmr_reader_session_wrong_handle::UserTag>;

    Snap snap{};
    auto writer = snap.writer(fsafe::mint_permission_root<typename Snap::writer_tag>());

    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    // Pass the WriterHandle to mint_reader_session — fails because the
    // reader-session mint expects Snap::ReaderHandle&.
    [[maybe_unused]] auto bad = fswmr::mint_reader_session<Snap>(ctx, writer);
    return 0;
}
