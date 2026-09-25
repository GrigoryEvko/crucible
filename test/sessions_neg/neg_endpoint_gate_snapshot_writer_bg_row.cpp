// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_snapshot_writer_session over a snapshot whose value carries the
// background row refuses the foreground context.  The writer sends the value,
// so its protocol does not fit a context that admits no background row.

#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/effects/_Computation.h>
#include <crucible/sessions/_SnapshotSession.h>

namespace eff = ::crucible::effects;
namespace ses = ::crucible::safety::proto::snapshot_session;

namespace {
struct Tag {};
using BgInt = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using Snap = ::crucible::concurrent::PermissionedSnapshot<BgInt, Tag>;
}  // namespace

inline void mint_under_foreground(Snap::WriterHandle& handle) {
    auto session = ses::mint_snapshot_writer_session<Snap>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
