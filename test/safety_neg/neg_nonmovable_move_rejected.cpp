// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: moving a type that CRTP-derives from
// `crucible::safety::NonMovable<Derived>`.  The mixin deletes the
// move ctor with a named reason string ("NonMovable<T>: exclusive
// ownership — move would leave a moved-from shell that callers may
// mistake for valid").
//
// Discipline rationale (Pinned.h, NonMovable section):
//   `NonMovable<T>` marks types whose IDENTITY is their RESOURCE —
//   an owned file descriptor, an arena backing pointer, a thread
//   handle.  A move would produce a "valid but empty" shell at the
//   source side; subsequent destructor calls on both objects would
//   either double-free the resource or skip the cleanup entirely.
//
//   Distinct from `Pinned<T>`: Pinned protects ADDRESS (interior
//   pointers held elsewhere stay valid because the object never
//   relocates); NonMovable protects RESOURCE (exclusive ownership
//   never branches).  Both are CRTP mixins that delete copy AND
//   move with named reasons — the headers are co-housed and the
//   discipline is parallel, but the semantic guarantees differ.
//
// HS14 — paired with neg_pinned_copy_rejected for distinct
// mismatch classes:
//   * Class T-copy (sibling):   Pinned-copy with address-as-identity
//     reason — "stable address" semantic.
//   * Class T-move (THIS file): NonMovable-move with exclusive-
//     ownership reason — "exclusive resource" semantic.
// Together the pair pins both soundness layers of the address-
// stability / exclusive-ownership CRTP mixin family:
//   (a) Pinned<T> protects ADDRESS as identity; and
//   (b) NonMovable<T> protects RESOURCE as ownership.
// Both deletion reasons are distinct strings on the production
// surface; softening either would slip past a single-fixture audit.
//
// U-144 — Class T-move fixture (closes NonMovable slice of #146).

#include <crucible/safety/Pinned.h>

#include <utility>

namespace {
    // Production-shape NonMovable consumer: a type whose IDENTITY is
    // an owned resource (here: a synthetic "handle id").  Cipher.h /
    // TraceLoader.h use this shape for ScopedFd / ScopedFile-style
    // owners.  A move would leave the source side with the same
    // handle_id_, allowing a later destructor to double-release.
    struct OwnedHandle : ::crucible::safety::NonMovable<OwnedHandle> {
        unsigned long handle_id_ = 0;
    };
}

// Anchor: default-construction is allowed — the only path to OBTAIN
// a NonMovable handle is in-place construction.  This compiles.
[[maybe_unused]] static OwnedHandle anchor_make_nonmovable() {
    return OwnedHandle{};
}

// VIOLATION: OwnedHandle inherits a deleted move ctor from
// NonMovable<OwnedHandle>.  Attempting move-construction (typical
// from `std::move(source)` patterns) triggers the deletion with its
// load-bearing reason string.  GCC emits "use of deleted function"
// + the "exclusive ownership" reason.
[[maybe_unused]] static OwnedHandle offending_nonmovable_move(OwnedHandle&& source) {
    return OwnedHandle{std::move(source)};   // ERROR: move ctor deleted
}

int main() { return 0; }
