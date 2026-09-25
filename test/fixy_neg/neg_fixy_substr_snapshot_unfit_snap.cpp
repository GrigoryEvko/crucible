// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Snapshot session mint negative fixture 2 of 2 (HS14 ≥2 floor):
// SnapshotSessionSurface concept-rejection route (distinct mismatch
// class from #1's role-mismatch).
//
// `mint_snapshot_writer_session<Snap>(ctx, handle)` is constrained by
// `SnapshotSessionSurface<Snap>` — the substrate must expose the
// (value_type, writer_tag, reader_tag, WriterHandle, ReaderHandle,
// writer/reader factories, publish/load) shape that PermissionedSnapshot
// satisfies.  Passing an UNRELATED substrate (e.g., a raw int / struct /
// SwmrSession-like type that lacks the publish() method) must reject
// at the concept gate.
//
// The concept gate is the guard.  Without it, a caller reaches the
// raw PermissionedSnapshot with no §XXI requires-clause.
//
// Reject sequence: `mint_snapshot_writer_session<Snap>` template
// instantiation begins → SnapshotSessionSurface<Snap> evaluates →
// fails (Snap has no `writer_tag` member type, no `writer(...)`, etc.)
// → overload resolution finds no candidate.
//
// Expected diagnostic: "constraints not satisfied" /
// "SnapshotSessionSurface" / "no matching function".

#include <crucible/effects/_ExecCtx.h>
#include <crucible/sessions/SnapshotSession.h>

#include <utility>

namespace fsnap = ::crucible::safety::proto::snapshot_session;
namespace eff = crucible::effects;

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
    eff::BgCompileCtx ctx{::crucible::effects::testing::bg()};
    // Snap does not satisfy SnapshotSessionSurface → concept-rejection.
    [[maybe_unused]] auto bad = fsnap::mint_snapshot_writer_session<Snap>(ctx, handle);
    return 0;
}
