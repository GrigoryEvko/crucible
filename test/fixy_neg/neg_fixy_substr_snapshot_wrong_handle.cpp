// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A4-022 negative fixture #1 (HS14 ≥2 floor): role-mismatch route.
//
// `mint_snapshot_writer_session<Snap>(ctx, handle)` takes
// `typename Snap::WriterHandle&`.  Passing a `ReaderHandle` (wrong
// role) fails type match at the call site.  Routing through
// `fixy::substr::snapshot::mint_snapshot_writer_session` must reject
// identically — proves the §XXI Universal Mint Pattern's
// role-discriminating signature is preserved across the fixy re-export.
//
// Reject sequence: template instantiation begins with Snap fixed →
// non-deducible second-parameter type is `Snap::WriterHandle&` →
// caller-provided argument is `ReaderHandle&` (the wrong nested type)
// → no implicit conversion exists → overload resolution fails.
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
namespace fsafe   = crucible::safety;

namespace neg_fixy_substr_snapshot_wrong_handle {
struct UserTag {};
}  // namespace neg_fixy_substr_snapshot_wrong_handle

int main() {
    using Snap =
        conc::PermissionedSnapshot<int,
            neg_fixy_substr_snapshot_wrong_handle::UserTag>;

    Snap snap{};
    auto writer = snap.writer(
        fsafe::mint_permission_root<typename Snap::writer_tag>());
    auto reader_opt = snap.reader();
    (void)writer;

    eff::BgCompileCtx ctx{};
    // Pass the (optional unwrapped) ReaderHandle to the WRITER session
    // mint — fails because mint_snapshot_writer_session expects
    // `Snap::WriterHandle&`.
    [[maybe_unused]] auto bad =
        fsubstr::snapshot::mint_snapshot_writer_session<Snap>(ctx, *reader_opt);
    return 0;
}
