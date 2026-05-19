// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A4-022 negative fixture #2 (HS14 ≥2 floor): SnapshotSessionSurface
// concept-rejection route (distinct mismatch class from #1's
// role-mismatch).
//
// `mint_snapshot_writer_session<Snap>(ctx, handle)` is constrained by
// `SnapshotSessionSurface<Snap>` — the substrate must expose the
// (value_type, writer_tag, reader_tag, WriterHandle, ReaderHandle,
// writer/reader factories, publish/load) shape that PermissionedSnapshot
// satisfies.  Passing an UNRELATED substrate (e.g., a raw int / struct /
// SwmrSession-like type that lacks the publish() method) must reject
// at the concept gate.
//
// Pre-A4-022 the snapshot family had NO session header — the
// concept did not exist; callers reached the raw substrate via
// `fixy::substr::concurrent::PermissionedSnapshot` with no §XXI
// requires-clause guarding misuse.  Post-A4-022 the concept gate IS
// the guard.
//
// Reject sequence: `mint_snapshot_writer_session<Snap>` template
// instantiation begins → SnapshotSessionSurface<Snap> evaluates →
// fails (Snap has no `writer_tag` member type, no `writer(...)`, etc.)
// → overload resolution finds no candidate.
//
// Expected diagnostic: "constraints not satisfied" /
// "SnapshotSessionSurface" / "no matching function".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

#include <utility>

namespace fsubstr = crucible::fixy::substr;
namespace eff     = crucible::effects;

namespace neg_fixy_substr_snapshot_unfit_snap {

// An unrelated type that does NOT satisfy SnapshotSessionSurface.
// It has none of: value_type, writer_tag, WriterHandle, writer(perm),
// reader(), publish(), load().  Failing the concept gate is the
// expected diagnostic.
struct NotASnapshot {
    int dummy = 0;
};

struct FakeHandle {
    int dummy = 0;
};

}  // namespace neg_fixy_substr_snapshot_unfit_snap

int main() {
    using Snap = neg_fixy_substr_snapshot_unfit_snap::NotASnapshot;
    using Handle = neg_fixy_substr_snapshot_unfit_snap::FakeHandle;

    Handle handle{};
    eff::BgCompileCtx ctx{};
    // Snap does not satisfy SnapshotSessionSurface → concept-rejection.
    [[maybe_unused]] auto bad =
        fsubstr::snapshot::mint_snapshot_writer_session<Snap>(ctx, handle);
    return 0;
}
