// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074j fixture #2 for fixy::substr::snapshot::mint_snapshot_writer
// (token mint, SnapshotSession.h:102).  The template-parameter constraint
// `SnapshotSessionSurface Snap` rejects a type that exposes a `writer_tag`
// (so the second parameter `Permission<typename Snap::writer_tag>&&`
// substitutes cleanly) but is MISSING the rest of the surface (value_type,
// reader_tag, WriterHandle/ReaderHandle, writer()/reader(), publish/load).
//
// Distinct mismatch class from
// neg_fixy_substr_snapshot_writer_wrong_perm_tag.cpp (#1): there the
// permission tag was wrong on a valid surface; here the permission tag is
// CORRECT (FakeWriterTag) and binds, so the ONLY reason for rejection is
// the SnapshotSessionSurface concept being unsatisfied.
//
// Expected diagnostic: SnapshotSessionSurface / constraints not satisfied /
// no matching function.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace fsafe   = crucible::safety;

namespace neg_fixy_substr_snapshot_writer_non_surface {
struct FakeWriterTag {};
// Exposes writer_tag so the second parameter substitutes, but lacks the
// remaining SnapshotSessionSurface requirements — concept must reject it.
struct FakeSurface {
    using writer_tag = FakeWriterTag;
};
}  // namespace neg_fixy_substr_snapshot_writer_non_surface

int main() {
    namespace ns = neg_fixy_substr_snapshot_writer_non_surface;
    ns::FakeSurface fake{};

    // Permission tag matches FakeSurface::writer_tag, so the parameter
    // binds; SnapshotSessionSurface<FakeSurface> is the failing gate.
    [[maybe_unused]] auto bad =
        fsubstr::snapshot::mint_snapshot_writer(
            fake,
            fsafe::mint_permission_root<ns::FakeWriterTag>());
    return 0;
}
