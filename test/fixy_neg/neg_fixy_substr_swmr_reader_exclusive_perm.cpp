// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SWMR token mint fixture 4 for
// safety::proto::swmr_session::mint_swmr_reader
// (token mint, two-argument overload, sessions/SwmrSession.h).  That overload
// takes a fractional `SharedPermission<typename Swmr::reader_tag>` proof
// by value.  Passing an EXCLUSIVE `Permission<reader_tag>` (minted via
// mint_permission_root) is a distinct, non-convertible type — the
// permission MODE (exclusive vs fractional) is wrong even though the tag
// is correct.
//
// Distinct mismatch class from
// neg_fixy_substr_swmr_reader_non_surface.cpp (#3): there the surface
// failed the SwmrSessionSurface concept on the single-argument overload;
// here the surface is valid and the reader_tag matches, so the ONLY
// reason for rejection is the exclusive-vs-shared permission-mode
// mismatch on the two-argument overload's proof parameter.
//
// Expected diagnostic: "cannot convert" / "no matching function"
// pointing at SharedPermission<reader_tag> vs Permission<reader_tag>.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/sessions/SwmrSession.h>

namespace fswmr = ::crucible::safety::proto::swmr_session;
namespace conc = crucible::concurrent;
namespace fsafe = crucible::safety;

namespace neg_fixy_substr_swmr_reader_exclusive_perm {
struct UserTag {};
}  // namespace neg_fixy_substr_swmr_reader_exclusive_perm

int main() {
    using Snap = conc::PermissionedSnapshot<int, neg_fixy_substr_swmr_reader_exclusive_perm::UserTag>;

    Snap snap{};

    // Two-argument mint_swmr_reader wants
    // SharedPermission<typename Snap::reader_tag>; an exclusive root
    // Permission for the same tag is a distinct, non-convertible type.
    [[maybe_unused]] auto bad =
        fswmr::mint_swmr_reader(snap, fsafe::mint_permission_root<typename Snap::reader_tag>());
    return 0;
}
