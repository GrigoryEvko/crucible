// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074j fixture #4 for fixy::substr::snapshot::mint_snapshot_reader
// (token mint, single-argument overload, SnapshotSession.h:110).  The
// template-parameter constraint `SnapshotSessionSurface Snap` requires
// `{ snap.reader() } -> std::same_as<std::optional<ReaderHandle>>`
// (SnapshotSession.h:90-91).  NearMissSnap satisfies EVERY other clause —
// all five nested types, writer()->WriterHandle, publish() noexcept,
// load()->value_type, try_load()->optional<value_type> — but reader()
// returns ReaderHandle DIRECTLY instead of optional<ReaderHandle>, so the
// concept fails on exactly that one method-signature clause.
//
// Distinct mismatch class from
// neg_fixy_substr_snapshot_reader_non_surface.cpp (#3): there a bare type
// failed the FIRST nested-type requirement; here a near-miss surface fails
// a single method-return-shape clause.  This catches the real-world bug
// "reader() returns the handle directly instead of an optional".
//
// Expected diagnostic: SnapshotSessionSurface / constraints not satisfied /
// no matching function.

#include <optional>

// PermissionedSnapshot.h transitively provides crucible::safety::Permission
// (via crucible/permissions/Permission.h) — no direct include needed, same
// as the writer fixtures' use of fsafe::mint_permission_root.
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/fixy/Substr.h>

namespace fsubstr = crucible::fixy::substr;
namespace fsafe   = crucible::safety;

namespace neg_fixy_substr_snapshot_reader_near_miss_surface {
struct WTag {};
struct RTag {};

struct FakeWriterHandle {
    void publish(int const&) noexcept {}
};
struct FakeReaderHandle {
    int load() { return 0; }
    std::optional<int> try_load() { return std::nullopt; }
};

// Satisfies every SnapshotSessionSurface clause EXCEPT reader()'s return
// type: reader() returns ReaderHandle, not optional<ReaderHandle>.
struct NearMissSnap {
    using value_type   = int;
    using writer_tag   = WTag;
    using reader_tag   = RTag;
    using WriterHandle = FakeWriterHandle;
    using ReaderHandle = FakeReaderHandle;

    FakeWriterHandle writer(fsafe::Permission<WTag>&&) noexcept { return {}; }
    // BUG: must return std::optional<FakeReaderHandle>.
    FakeReaderHandle reader() noexcept { return {}; }
};
}  // namespace neg_fixy_substr_snapshot_reader_near_miss_surface

int main() {
    neg_fixy_substr_snapshot_reader_near_miss_surface::NearMissSnap snap{};

    [[maybe_unused]] auto bad = fsubstr::snapshot::mint_snapshot_reader(snap);
    return 0;
}
