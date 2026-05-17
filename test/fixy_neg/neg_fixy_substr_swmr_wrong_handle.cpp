// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Substr fixture #2: mint_writer_session via fixy:: alias
// rejects a ReaderHandle (wrong role).
//
// Violation: `mint_writer_session<Swmr>(ctx, handle)` takes
// `typename Swmr::WriterHandle&`.  Passing a ReaderHandle fails
// type match at the call site.  Routing through
// `fixy::substr::swmr::mint_writer_session` must reject identically.
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at WriterHandle vs ReaderHandle.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

#include <utility>

namespace fsubstr = crucible::fixy::substr;
namespace conc    = crucible::concurrent;
namespace eff     = crucible::effects;

namespace neg_fixy_substr_swmr_wrong_handle {
struct UserTag {};
}

int main() {
    using Snap =
        conc::PermissionedSnapshot<int, neg_fixy_substr_swmr_wrong_handle::UserTag>;

    Snap snap{};
    auto reader = snap.reader();

    eff::BgCompileCtx ctx{};
    // Pass the ReaderHandle to mint_writer_session — fails.
    [[maybe_unused]] auto bad =
        fsubstr::swmr::mint_writer_session<Snap>(ctx, reader);
    return 0;
}
