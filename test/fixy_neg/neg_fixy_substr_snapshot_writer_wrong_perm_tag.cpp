// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074j fixture #1 for fixy::substr::snapshot::mint_snapshot_writer
// (token mint, SnapshotSession.h:102).  The mint's second parameter is the
// EXACT type `Permission<typename Snap::writer_tag>&&`; supplying a
// Permission carrying an UNRELATED tag fails the parameter match.
//
// Distinct mismatch class from
// neg_fixy_substr_snapshot_writer_non_surface.cpp (#2): here the surface
// (PermissionedSnapshot) IS a valid SnapshotSessionSurface, so the ONLY
// reason the call is rejected is the permission-tag identity mismatch.
// #2 inverts this — valid permission, invalid surface.
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at Permission<writer_tag> vs Permission<WrongTag>.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace conc    = crucible::concurrent;
namespace fsafe   = crucible::safety;

namespace neg_fixy_substr_snapshot_writer_wrong_perm_tag {
struct UserTag {};
struct WrongTag {};
}  // namespace neg_fixy_substr_snapshot_writer_wrong_perm_tag

int main() {
    using Snap = conc::PermissionedSnapshot<int,
        neg_fixy_substr_snapshot_writer_wrong_perm_tag::UserTag>;

    Snap snap{};

    // mint_snapshot_writer wants Permission<typename Snap::writer_tag>&&;
    // an exclusive root token for WrongTag is a distinct, non-convertible
    // Permission instantiation.
    [[maybe_unused]] auto bad =
        fsubstr::snapshot::mint_snapshot_writer(
            snap,
            fsafe::mint_permission_root<
                neg_fixy_substr_snapshot_writer_wrong_perm_tag::WrongTag>());
    return 0;
}
