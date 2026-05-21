// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: invoking `AlignedBuffer<T, A>::operator=(const
// AlignedBuffer<T, A>&)` — the copy-assignment operator is
// `= delete("AlignedBuffer is move-only")` at AlignedBuffer.h:91.
// Copy-assignment from a const lvalue source triggers the deleted-
// function diagnostic with the move-only-discipline message verbatim.
//
// Discipline rationale (AlignedBuffer.h:1-26, 89-91):
//   AlignedBuffer<T, Alignment> is a move-only RAII wrapper over an
//   `std::aligned_alloc` region of T[N], freed in dtor via std::free.
//   Copy-assignment must be rejected at the assignment boundary
//   because the production rebuild-then-assign pattern MUST go
//   through MOVE-assignment:
//
//     buf = AlignedBuffer<T>::allocate(new_count);
//
//   Move-assignment frees the destination's old backing pointer in
//   reset() before transferring the new buffer's data_+size_;
//   copy-assignment would attempt to clone the source's heap
//   pointer, leaking the destination's existing allocation AND
//   aliasing the source's backing ready to double-free on next
//   destructor.
//
// HS14 — distinct-class fixture pair for AlignedBuffer:
//   * Class T-copy-ctor (sibling neg_alignedbuffer_copy_ctor_
//     rejected): direct copy construction triggers the deleted ctor
//     at AlignedBuffer.h:89.  Construction-boundary enforcement.
//   * Class T-copy-assign (THIS file): copy-assignment from a const
//     lvalue source triggers the deleted operator= at
//     AlignedBuffer.h:91.  Assignment-boundary enforcement — the
//     rebuild-then-assign production pattern.
//
//   Two structurally separate enforcement points on the same
//   wrapper.  Completes the move-only RAII buffer trio
//   (HugePageBuffer + SwissTableBuffer + AlignedBuffer) at both
//   the construction AND the assignment boundary.
//
// FIXY-U-158 — second AlignedBuffer HS14 fixture (closes its slice
// of #146 A8-P2 alongside U-153 + U-156).

#include <crucible/safety/AlignedBuffer.h>

#include <cstdint>

namespace {

// Anchor: move-assignment compiles cleanly — the canonical
// rebuild-then-assign pattern.
[[maybe_unused]] static void anchor_move_assign() {
    auto dest = ::crucible::safety::AlignedBuffer<std::uint64_t>::allocate(16);
    dest = ::crucible::safety::AlignedBuffer<std::uint64_t>::allocate(32);
    // Move-assign transfers ownership; reset() frees the old 16-cap
    // backing; the temporary's source is left empty (data_=nullptr).
}

// VIOLATION: AlignedBuffer<T, A>::operator=(const AlignedBuffer<T, A>&)
// is `= delete("AlignedBuffer is move-only")`.  Copy-assignment from
// a const lvalue triggers the deleted-function diagnostic with the
// move-only-discipline message verbatim.
[[maybe_unused]] static void offending_copy_assign(
    ::crucible::safety::AlignedBuffer<std::uint64_t>& dest,
    const ::crucible::safety::AlignedBuffer<std::uint64_t>& source)
{
    dest = source;
    // ERROR: use of deleted function 'AlignedBuffer& operator=(const AlignedBuffer&)'
    // diagnostic message: "AlignedBuffer is move-only"
}

}  // namespace

int main() { return 0; }
