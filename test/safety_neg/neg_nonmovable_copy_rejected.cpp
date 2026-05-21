// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: copy-constructing a type that CRTP-derives from
// `crucible::safety::NonMovable<Derived>`.  The mixin deletes the
// copy ctor with a SECOND named reason distinct from the move-
// rejection — "NonMovable<T>: exclusive ownership — copy would
// duplicate a singleton resource" (Pinned.h:71).
//
// Discipline rationale (Pinned.h:58-75):
//   `NonMovable<T>` enforces exclusive resource ownership.  Copy
//   and move are BOTH forbidden, but for distinct structural
//   reasons:
//     * Copy (THIS file): would DUPLICATE a singleton resource —
//       two destructors would either double-release (double-
//       close fd, double-free arena base) or skip the cleanup
//       entirely if one tries to detect the other.  The reason
//       string cites "copy would duplicate a singleton resource".
//     * Move (sibling neg_nonmovable_move_rejected): would leave
//       a "moved-from shell" the caller might mistake for a
//       valid handle.  Same exclusive-ownership wrapper, but the
//       failure-mode the reason calls out is different.
//
//   Both reason strings are LOAD-BEARING on the production
//   surface — each one points at a distinct safety failure mode.
//   A refactor softening either reason slips past a single-
//   fixture audit.  THIS file pins the copy-reason.
//
// HS14 — NonMovable now has TWO distinct fixtures pinning DISTINCT
// deleted-special-member reasons:
//   * Class T-copy (THIS file): copy ctor deletion + "copy would
//     duplicate a singleton resource" reason.
//   * Class T-move (sibling neg_nonmovable_move_rejected): move
//     ctor deletion + "move would leave a moved-from shell that
//     callers may mistake for valid" reason.
//
//   Two structurally separate enforcement points on the same
//   wrapper.  Mirrors the Pinned pair (neg_pinned_copy_rejected +
//   neg_pinned_move_rejected) — together the four fixtures cover
//   BOTH wrappers across BOTH deletion classes, locking down the
//   full deleted-special-member surface of the address-stability
//   / exclusive-ownership CRTP family.
//
// FIXY-U-155 — audit-remediation closing the strict-HS14 floor on
// NonMovable (paired with neg_pinned_move_rejected for Pinned).

#include <crucible/safety/Pinned.h>

namespace {
    // Production-shape NonMovable consumer: a type whose IDENTITY
    // is an owned resource (synthetic handle_id_).  Copying would
    // hand two owners the same handle — when both destructors
    // fire, they double-release.
    struct OwnedHandle : ::crucible::safety::NonMovable<OwnedHandle> {
        unsigned long handle_id_ = 0;
    };
}  // namespace

// Anchor: default-construction is allowed — the only path to
// OBTAIN a NonMovable handle is in-place construction.
[[maybe_unused]] static OwnedHandle anchor_make_nonmovable() {
    return OwnedHandle{};
}

// VIOLATION: OwnedHandle inherits a deleted copy ctor from
// NonMovable<OwnedHandle>.  Attempting copy-construction triggers
// the deletion with its load-bearing reason — "NonMovable<T>:
// exclusive ownership — copy would duplicate a singleton
// resource".  GCC emits "use of deleted function" + this reason
// verbatim.
[[maybe_unused]] static OwnedHandle offending_nonmovable_copy(const OwnedHandle& source) {
    return OwnedHandle{source};   // ERROR: copy ctor deleted
}

int main() { return 0; }
