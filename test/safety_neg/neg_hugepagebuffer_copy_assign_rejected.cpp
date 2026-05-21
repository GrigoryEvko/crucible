// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: invoking `HugePageBuffer<T>::operator=(const
// HugePageBuffer<T>&)` — the copy-assignment operator is
// `= delete("HugePageBuffer is move-only")` at HugePageBuffer.h:103.
// Copy-assignment from a const lvalue triggers the deleted-function
// diagnostic with the move-only-discipline message verbatim.
//
// Discipline rationale (HugePageBuffer.h:14-21, 102-103):
//   HugePageBuffer<T> is a move-only RAII wrapper over a 2-MB-
//   aligned `aligned_alloc` region with MADV_HUGEPAGE applied.
//   Copy-assignment must be rejected at the assignment boundary
//   because the production rebuild-then-assign pattern (TraceRing
//   resize, MetaLog reallocation, Cipher warm-tier shard growth)
//   MUST go through MOVE-assignment:
//
//     buf = HugePageBuffer<T>::allocate(new_size);
//
//   Move-assignment frees the old backing pointer in the
//   destructor of the temporary's source (after the swap).
//   Copy-assignment would attempt to clone the source's heap
//   pointer into the destination — the destination's existing
//   pointer would leak, AND the two pointers would alias the
//   source's backing, ready to double-free on next destructor.
//
// HS14 — HugePageBuffer now has TWO distinct fixtures pinning
// DISTINCT deleted-special-member-function enforcement points:
//   * Class T-copy-ctor (sibling neg_hugepagebuffer_copy_rejected):
//     direct copy construction triggers the deleted copy ctor at
//     HugePageBuffer.h:102.  Construction-boundary enforcement.
//   * Class T-copy-assign (THIS file): copy-assignment of an
//     existing HugePageBuffer from a const lvalue source triggers
//     the deleted copy-assign op= at HugePageBuffer.h:103.
//     Assignment-boundary enforcement — the rebuild-then-assign
//     production pattern.
//
//   Two structurally separate enforcement points on the same
//   wrapper.  Mirrors the SwissTableBuffer pair
//   (neg_swisstablebuffer_copy_rejected for ctor +
//   neg_swisstablebuffer_copy_assign_rejected for assign) —
//   together the four fixtures lock the full move-only deleted-
//   surface of BOTH RAII buffer wrappers at BOTH boundaries.
//
// FIXY-U-156 — audit-remediation closing the strict-HS14 floor on
// HugePageBuffer (paired with neg_swisstablebuffer_copy_rejected
// for SwissTableBuffer).

#include <crucible/safety/HugePageBuffer.h>

#include <cstdint>

namespace {

// Anchor: move-assignment compiles cleanly — the canonical
// rebuild-then-assign pattern.
[[maybe_unused]] static void anchor_move_assign() {
    auto dest = ::crucible::safety::HugePageBuffer<std::uint64_t>::allocate(16);
    dest = ::crucible::safety::HugePageBuffer<std::uint64_t>::allocate(32);
    // Move-assign transfers ownership; old 16-cap backing freed by
    // the temporary's destructor.
}

// VIOLATION: HugePageBuffer<T>::operator=(const HugePageBuffer<T>&)
// is `= delete("HugePageBuffer is move-only")`.  Copy-assignment
// from a const lvalue source triggers the deleted-function
// diagnostic with the move-only-discipline message verbatim.
[[maybe_unused]] static void offending_copy_assign(
    ::crucible::safety::HugePageBuffer<std::uint64_t>& dest,
    const ::crucible::safety::HugePageBuffer<std::uint64_t>& source)
{
    dest = source;
    // ERROR: use of deleted function 'HugePageBuffer& operator=(const HugePageBuffer&)'
    // diagnostic message: "HugePageBuffer is move-only"
}

}  // namespace

int main() { return 0; }
