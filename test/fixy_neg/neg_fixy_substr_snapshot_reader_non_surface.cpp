// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Snapshot token mint fixture 3 for
// safety::proto::snapshot_session::mint_snapshot_reader
// (token mint, single-argument overload, sessions/_SnapshotSession.h).  The
// template-parameter constraint `SnapshotSessionSurface Snap` rejects a
// plain type that exposes NONE of the required surface — it fails at the
// very first requirement (`typename Snap::value_type`).
//
// Distinct mismatch class from
// neg_fixy_substr_snapshot_reader_near_miss_surface.cpp (#4): there a type
// that satisfies EVERY surface requirement except the reader() return-type
// contract is rejected on that one method-signature clause; here the type
// is not a snapshot at all and fails on the first nested-type requirement.
//
// Expected diagnostic: SnapshotSessionSurface / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/sessions/_SnapshotSession.h>

namespace fsnap = ::crucible::safety::proto::snapshot_session;

namespace neg_fixy_substr_snapshot_reader_non_surface {
// No nested types, no writer()/reader() — SnapshotSessionSurface must
// reject at the first `typename Snap::value_type` requirement.
struct FakeSurface {};
}  // namespace neg_fixy_substr_snapshot_reader_non_surface

int main() {
    neg_fixy_substr_snapshot_reader_non_surface::FakeSurface fake{};

    [[maybe_unused]] auto bad = fsnap::mint_snapshot_reader(fake);
    return 0;
}
