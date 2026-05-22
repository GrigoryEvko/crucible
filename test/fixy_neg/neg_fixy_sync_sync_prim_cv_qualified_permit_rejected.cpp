// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-085 HS14 fixture #1 of 2 for fixy/sync/SyncPrim.h:
// IsBannedSyncPrim concept propagates discipline through cv/ref
// qualifiers — `permit_mutex const&` is exactly as banned as bare
// `permit_mutex`.
//
// Why this matters: a regression that drops the `std::remove_cvref_t`
// in IsBannedSyncPrim would let a hot-path function silently admit
// `permit_mutex const&` as a "different type" — defeating the entire
// load-bearing reject because virtually every production permit-tag
// site flows through a reference parameter.  This fixture pins the
// cv/ref-pierce discipline at the SURFACE so a substrate refactor
// cannot drift it.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// CV-REF PROPAGATION half — IsBannedSyncPrim must apply on the
// cv-ref-stripped type.  Sibling fixture
// `neg_fixy_sync_sync_prim_hot_path_permit_rejected.cpp` exercises
// the LOAD-BEARING REJECT half (HotPathSyncPrimSafe<Hot, ...>).
//
// Expected diagnostic: "static assertion failed" / "is_banned_sync_prim_v"
// / "IsBannedSyncPrim" / "permit_mutex".

#include <crucible/fixy/sync/SyncPrim.h>

namespace fixy_sync_prim = crucible::fixy::sync::sync_prim;

// The load-bearing claim: `permit_mutex const&` is concept-positive
// for IsBannedSyncPrim.  A static_assert in the negated direction is
// the load-bearing error — if the cv-ref pierce were broken, the
// negated assertion would PASS (and this fixture would compile),
// silently letting hot-path callers bypass the discipline by adding
// `const&` to the permit-tag parameter type.
//
// We assert the NEGATION of the correct behavior — so a correct
// SyncPrim.h surface MUST reject this static_assert at compile time.

static_assert(!fixy_sync_prim::IsBannedSyncPrim<fixy_sync_prim::permit_mutex const&>,
    "FIXY-V-085 HS14 fixture #1: cv/ref propagation is the load-bearing "
    "discipline.  If this static_assert passes, IsBannedSyncPrim has "
    "lost the std::remove_cvref_t pierce and hot-path callers can "
    "evade the ban by spelling `permit_mutex const&` instead of "
    "`permit_mutex`.");

int main() { return 0; }
