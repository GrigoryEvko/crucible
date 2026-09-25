// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SWMR session mint fixture 2 for
// safety::proto::swmr_session::mint_writer_session:
// rejects a ReaderHandle (wrong role).
//
// Violation: `mint_writer_session<Swmr>(ctx, handle)` takes
// `typename Swmr::WriterHandle&`.  Passing a ReaderHandle fails
// type match at the call site.
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at WriterHandle vs ReaderHandle.

#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/_SwmrSession.h>

#include <utility>

namespace fswmr = ::crucible::safety::proto::swmr_session;
namespace conc = crucible::concurrent;
namespace eff = crucible::effects;

namespace neg_fixy_substr_swmr_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_swmr_wrong_handle

int main() {
    using Snap = conc::PermissionedSnapshot<int, neg_fixy_substr_swmr_wrong_handle::UserTag>;

    Snap snap{};
    auto reader = snap.reader();

    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    // Pass the ReaderHandle to mint_writer_session — fails.
    [[maybe_unused]] auto bad = fswmr::mint_writer_session<Snap>(ctx, reader);
    return 0;
}
