// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: invoking `SwissTableBuffer<SlotPtr>(const
// SwissTableBuffer&)` — the copy constructor is
// `= delete("SwissTableBuffer is move-only")` at
// SwissTableBuffer.h:82.  Direct copy construction from a const
// lvalue triggers the deleted-function diagnostic with the move-
// only-discipline message verbatim.
//
// Discipline rationale (SwissTableBuffer.h:3-28, 82-83):
//   SwissTableBuffer<SlotPtr> is a move-only RAII wrapper over a
//   single aligned heap allocation holding a Swiss-table's coupled
//   control-byte array + slot-pointer array in one contiguous
//   backing region (ExprPool / forge::RecipePool consumers).
//   Copy-construction must be rejected at the construction
//   boundary because the production "rebuild() returns a fresh
//   buffer" pattern (SwissTableBuffer.h:25-27) MUST be wired
//   through MOVE-construction:
//
//     auto fresh = SwissTableBuffer<SlotPtr>::allocate(new_cap);
//     // fresh moves into the consumer; old buffer's dtor frees.
//
//   A copy-construct would clone the heap pointer; two
//   destructors would later `std::free` the same backing region.
//
// HS14 — SwissTableBuffer now has TWO distinct fixtures pinning
// DISTINCT deleted-special-member-function enforcement points:
//   * Class T-copy-ctor (THIS file): direct copy construction
//     triggers the deleted copy ctor at SwissTableBuffer.h:82.
//     Construction-boundary enforcement — the rebuild()-return
//     production pattern.
//   * Class T-copy-assign (sibling neg_swisstablebuffer_copy_
//     assign_rejected): copy-assignment from a const lvalue
//     triggers the deleted copy-assign op= at SwissTableBuffer.h:
//     83.  Assignment-boundary enforcement.
//
//   Two structurally separate enforcement points on the same
//   wrapper.  Mirrors the HugePageBuffer pair
//   (neg_hugepagebuffer_copy_rejected for ctor +
//   neg_hugepagebuffer_copy_assign_rejected for assign) —
//   together the four fixtures lock the full move-only deleted-
//   surface of BOTH RAII buffer wrappers at BOTH boundaries.
//
// FIXY-U-156 — audit-remediation closing the strict-HS14 floor on
// SwissTableBuffer (paired with neg_hugepagebuffer_copy_assign_
// rejected for HugePageBuffer).

#include <crucible/safety/SwissTableBuffer.h>

#include <cstdint>

namespace {

// Anchor: move-construction compiles cleanly — the canonical
// rebuild-then-assign transfer path.
[[maybe_unused]] static
::crucible::safety::SwissTableBuffer<const std::uint64_t*>
anchor_move_construct() {
    auto buf = ::crucible::safety::SwissTableBuffer<const std::uint64_t*>::allocate(16);
    return buf;   // NRVO or move — both legal.
}

// VIOLATION: SwissTableBuffer<SlotPtr>(const SwissTableBuffer&)
// is `= delete("SwissTableBuffer is move-only")`.  Direct copy
// construction from a const lvalue source triggers the deleted-
// function diagnostic with the move-only-discipline message
// verbatim.
[[maybe_unused]] static
::crucible::safety::SwissTableBuffer<const std::uint64_t*>
offending_copy_construct(
    const ::crucible::safety::SwissTableBuffer<const std::uint64_t*>& source)
{
    return ::crucible::safety::SwissTableBuffer<const std::uint64_t*>{source};
    // ERROR: use of deleted function 'SwissTableBuffer(const SwissTableBuffer&)'
    // diagnostic message: "SwissTableBuffer is move-only"
}

}  // namespace

int main() { return 0; }
