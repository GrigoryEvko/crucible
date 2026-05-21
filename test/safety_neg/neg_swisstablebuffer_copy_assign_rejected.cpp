// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: invoking `SwissTableBuffer<SlotPtr>::operator=(const
// SwissTableBuffer&)` — the copy assignment operator is
// `= delete("SwissTableBuffer is move-only")` at
// SwissTableBuffer.h:83.  Copy assignment triggers overload-
// resolution failure citing the deleted-function diagnostic with the
// move-only-discipline message verbatim.
//
// Discipline rationale (SwissTableBuffer.h:3-28, 82-83):
//   SwissTableBuffer<SlotPtr> is a move-only RAII wrapper over a
//   single aligned heap allocation holding a Swiss-table's coupled
//   control-byte array + slot-pointer array in one contiguous backing
//   region.  Same fundamental discipline as HugePageBuffer:
//   duplicating ownership via copy-assignment would mean two
//   `std::free` calls on the same backing pointer — corrupting both
//   the Swiss-table consumer (ExprPool / forge::RecipePool) and any
//   reader holding a slot reference.
//
//   The wrapper's discipline message ("SwissTableBuffer is move-
//   only") encodes the rebuild-then-assign pattern documented at
//   SwissTableBuffer.h:25-27: rebuild() is destructive and returns
//   the new buffer; the caller assigns and the old buffer's dtor
//   frees.  That assignment MUST be move-assignment — copy-
//   assignment would leak the old buffer AND alias the new one.
//
// HS14 — distinct-class fixture pair across move-only RAII siblings:
//   * Class T-copy-ctor-rejected (sibling neg_hugepagebuffer_copy_
//     rejected): same move-only discipline, exercised through the
//     copy CONSTRUCTOR on HugePageBuffer.
//   * Class T-copy-assign-rejected (THIS file, SwissTableBuffer):
//     same move-only discipline, exercised through the copy
//     ASSIGNMENT operator on SwissTableBuffer — pins the rebuild()
//     usage discipline at the assignment boundary.
//
//   Both fixtures pin the deleted-copy discipline that is the
//   structural guarantee of move-only RAII; together they cover
//   both halves of the deleted-copy surface (ctor + assign) across
//   the two structurally similar wrappers.  Mirrors the
//   Pinned-copy + NonMovable-move pair pattern under fixy-U-144.
//
// FIXY-U-153 — second of the HugePageBuffer / SwissTableBuffer pair
// (closes their slice of #146 A8-P2).

#include <crucible/safety/SwissTableBuffer.h>

#include <cstdint>

namespace {

// Anchor: move-assignment compiles cleanly — the canonical
// rebuild-then-assign pattern at SwissTableBuffer.h:25-27.
[[maybe_unused]] static void anchor_move_assign() {
    auto dest = ::crucible::safety::SwissTableBuffer<const std::uint64_t*>::allocate(16);
    dest = ::crucible::safety::SwissTableBuffer<const std::uint64_t*>::allocate(32);
    // Move-assignment is the supported transfer path; the old
    // buffer's dtor frees the original 16-cap allocation.
}

// VIOLATION: SwissTableBuffer<SlotPtr>::operator=(const
// SwissTableBuffer&) is `= delete("SwissTableBuffer is move-only")`.
// Copy-assignment from a const lvalue triggers the deleted-function
// diagnostic.
[[maybe_unused]] static void offending_copy_assign(
    ::crucible::safety::SwissTableBuffer<const std::uint64_t*>& dest,
    const ::crucible::safety::SwissTableBuffer<const std::uint64_t*>& source)
{
    dest = source;
    // ERROR: use of deleted function 'SwissTableBuffer& operator=(const SwissTableBuffer&)'
    // diagnostic message: "SwissTableBuffer is move-only"
}

}  // namespace

int main() { return 0; }
