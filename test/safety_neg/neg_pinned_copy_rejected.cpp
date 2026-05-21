// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: copying a type that CRTP-derives from
// `crucible::safety::Pinned<Derived>`.  The mixin deletes the copy
// ctor with a named reason string ("Pinned<T>: stable address —
// address-as-identity or interior pointers into own storage").
// GCC reports the deletion's reason in the diagnostic so a reviewer
// reading the failure sees WHY the copy is forbidden.
//
// Discipline rationale (Pinned.h):
//   `Pinned<T>` marks types whose IDENTITY is their ADDRESS — SPSC
//   rings whose consumer holds an interior pointer, allocator bases,
//   thread-pinned state, self-referential structs.  A copy would
//   create two distinct addresses claiming to be the "same" object;
//   the consumer holding an interior pointer would read stale data
//   AND the producer would write to an unobserved buffer.  Silent
//   correctness corruption with no diagnostic until the race
//   manifests in production.
//
//   The CRTP-by-value form is deliberate per the header's docs:
//   inheritance by an empty non-template base gives generic
//   diagnostics ("PinnedBase::operator=(...) is deleted"); CRTP
//   names the DERIVED type in the error ("PinnedRing's copy ctor
//   is deleted because Pinned<PinnedRing>'s ...").
//
// HS14 — paired with neg_nonmovable_move_rejected for distinct
// mismatch classes:
//   * Class T-copy (THIS file): Pinned-copy with address-as-identity
//     reason — the "stable address" semantic of Pinned.
//   * Class T-move (sibling):   NonMovable-move with exclusive-
//     ownership reason — the "exclusive resource" semantic of
//     NonMovable.
// Together the pair pins both soundness layers of the address-
// stability / exclusive-ownership CRTP mixin family:
//   (a) Pinned<T> protects ADDRESS as identity; and
//   (b) NonMovable<T> protects RESOURCE as ownership.
// Both deletion reasons are distinct strings on the production
// surface (Pinned.h:53-56 vs Pinned.h:73-76); softening either
// would slip past a single-fixture audit.
//
// U-144 — first neg-compile pair for safety::Pinned / NonMovable
// (closes the address-stability slice of backlog #146 A8-P2
// alongside U-140's Machine, U-141's ConstantTime, U-142's Tagged,
// U-143's SealedRefined coverage).

#include <crucible/safety/Pinned.h>

#include <atomic>

namespace {
    // Production-shape Pinned consumer: a cache-line-aligned ring with
    // a cross-thread atomic head.  The address IS the identity —
    // moving it would leave the consumer thread reading from the wrong
    // memory.  CRUCIBLE_HOT TraceRing.h:* uses exactly this shape.
    struct PinnedRing : ::crucible::safety::Pinned<PinnedRing> {
        alignas(64) std::atomic<unsigned long> head_{0};
        alignas(64) std::atomic<unsigned long> tail_{0};
    };
}

// Anchor: default-constructing in place is allowed — that's the only
// path to OBTAIN a Pinned object.  This call compiles.
[[maybe_unused]] static PinnedRing anchor_make_pinned() {
    return PinnedRing{};
}

// VIOLATION: PinnedRing inherits a deleted copy ctor from
// Pinned<PinnedRing>.  Attempting to copy-construct triggers the
// deletion with its load-bearing reason.  GCC emits "use of deleted
// function" + the "stable address" reason string.
[[maybe_unused]] static PinnedRing offending_pinned_copy(const PinnedRing& source) {
    return PinnedRing{source};       // ERROR: copy ctor deleted
}

int main() { return 0; }
