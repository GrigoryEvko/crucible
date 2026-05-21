// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: move-constructing a type that CRTP-derives from
// `crucible::safety::Pinned<Derived>`.  The mixin deletes the move
// ctor with a SECOND named reason string distinct from the copy-
// rejection — "Pinned<T>: stable address — move would invalidate
// references held by another thread or by self" (Pinned.h:53).
//
// Discipline rationale (Pinned.h:35-56):
//   `Pinned<T>` enforces address stability over the object's full
//   lifetime.  Copy and move are BOTH forbidden, but for distinct
//   structural reasons:
//     * Copy (sibling neg_pinned_copy_rejected): two distinct
//       addresses claiming to be the "same" object — interior
//       pointers held elsewhere read stale data, producer writes
//       to an unobserved buffer.  The reason string emphasizes
//       "address-as-identity OR interior pointers into own
//       storage".
//     * Move (THIS file): the object's CONTENTS migrate to a new
//       address; any thread that captured a reference at the
//       prior address now points at moved-from storage.  The
//       reason string emphasizes "move would invalidate
//       references held by ANOTHER THREAD or by self" — a
//       different structural concern than the copy case, naming
//       cross-thread interior references and self-referential
//       pointers explicitly.
//
//   The TWO reason strings are LOAD-BEARING DOCUMENTATION on the
//   production surface: each one points at a different audit-time
//   question.  A refactor that softens either reason (or collapses
//   the pair to a single `=delete` without a reason) would slip
//   past a single-fixture audit.  THIS file pins the move-reason.
//
// HS14 — Pinned now has TWO distinct fixtures pinning DISTINCT
// deleted-special-member-function reasons:
//   * Class T-copy (sibling neg_pinned_copy_rejected): copy ctor
//     deletion + "address-as-identity / interior pointers into own
//     storage" reason.
//   * Class T-move (THIS file): move ctor deletion + "move would
//     invalidate references held by another thread or by self"
//     reason.
//
//   Two structurally separate enforcement points on the same
//   wrapper.  Mirrors the Pinned-copy + NonMovable-move pair
//   pattern from U-144 by completing it across BOTH deletion
//   classes on EACH wrapper.
//
// FIXY-U-155 — audit-remediation closing the strict-HS14 floor on
// Pinned (paired with neg_nonmovable_copy_rejected for NonMovable).

#include <crucible/safety/Pinned.h>

#include <utility>

namespace {
    // Production-shape Pinned consumer: a cache-line-aligned ring
    // whose atomic head_/tail_ are READ by other threads holding
    // pointer-into-this-storage references.  Moving the ring would
    // invalidate those interior references — the precise scenario
    // the move-deletion reason cites.
    struct PinnedRing : ::crucible::safety::Pinned<PinnedRing> {
        unsigned long head_ = 0;
        unsigned long tail_ = 0;
    };
}  // namespace

// Anchor: default construction in place is the only path to OBTAIN
// a Pinned object.  This compiles cleanly.
[[maybe_unused]] static PinnedRing anchor_make_pinned() {
    return PinnedRing{};
}

// VIOLATION: PinnedRing inherits a deleted move ctor from
// Pinned<PinnedRing>.  Attempting move-construction (canonical
// `std::move(source)` pattern) triggers the deletion with its
// load-bearing reason string — "Pinned<T>: stable address — move
// would invalidate references held by another thread or by self".
// GCC emits "use of deleted function" + this reason verbatim.
[[maybe_unused]] static PinnedRing offending_pinned_move(PinnedRing&& source) {
    return PinnedRing{std::move(source)};   // ERROR: move ctor deleted
}

int main() { return 0; }
