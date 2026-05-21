// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: invoking `AlignedBuffer<T, A>(const AlignedBuffer<T, A>&)`
// — the copy constructor is `= delete("AlignedBuffer is move-only")`
// at AlignedBuffer.h:90.  Direct copy construction from a const
// lvalue triggers the deleted-function diagnostic with the move-
// only-discipline message verbatim.
//
// Discipline rationale (AlignedBuffer.h:1-26, 89-90):
//   AlignedBuffer<T, Alignment> is a move-only RAII wrapper over a
//   `std::aligned_alloc` region of T[N], freed in dtor via std::free.
//   Replaces the hand-rolled `T* p = static_cast<T*>(aligned_alloc(...))`
//   patterns at consumer sites that cross scopes and must pair every
//   alloc with a free.  Same fundamental discipline as
//   HugePageBuffer/SwissTableBuffer — duplicating ownership via copy-
//   construction would mean two destructors both `std::free`-ing the
//   same backing pointer.  The deleted copy ctor is the structural
//   guarantee that ownership lives in exactly one AlignedBuffer
//   instance per allocation, transferred via move-construction.
//
// HS14 — distinct-class fixture pair for AlignedBuffer:
//   * Class T-copy-ctor (THIS file): direct copy construction
//     triggers the deleted ctor at the construction boundary —
//     pins the rebuild()-return / factory-result production path.
//   * Class T-copy-assign (sibling neg_alignedbuffer_copy_assign_
//     rejected): copy-assignment of an existing AlignedBuffer from
//     a const lvalue source triggers the deleted operator= at the
//     assignment boundary — pins the rebuild()-then-assign
//     production path.
//
//   Two structurally separate enforcement points on the same
//   wrapper.  Mirrors the U-153 + U-156 HugePageBuffer +
//   SwissTableBuffer pair pattern, completing the move-only RAII
//   buffer trio (HugePageBuffer + SwissTableBuffer + AlignedBuffer).
//
// FIXY-U-158 — first AlignedBuffer HS14 fixture (wrapper had ZERO
// neg-compile coverage before this ship; closes its slice of #146
// A8-P2 alongside U-153 + U-156).

#include <crucible/safety/AlignedBuffer.h>

#include <cstdint>

namespace {

// Anchor: move-construction compiles cleanly — the canonical
// transfer-of-ownership pattern.
[[maybe_unused]] static
::crucible::safety::AlignedBuffer<std::uint64_t>
anchor_move_construct() {
    auto buf = ::crucible::safety::AlignedBuffer<std::uint64_t>::allocate(8);
    return buf;   // NRVO or move — both legal.
}

// VIOLATION: AlignedBuffer<T, A>(const AlignedBuffer<T, A>&) is
// `= delete("AlignedBuffer is move-only")`.  Direct copy construction
// from a const lvalue source triggers the deleted-function diagnostic
// with the move-only-discipline message verbatim.
[[maybe_unused]] static
::crucible::safety::AlignedBuffer<std::uint64_t>
offending_copy_construct(
    const ::crucible::safety::AlignedBuffer<std::uint64_t>& source)
{
    return ::crucible::safety::AlignedBuffer<std::uint64_t>{source};
    // ERROR: use of deleted function 'AlignedBuffer<T, A>(const AlignedBuffer<T, A>&)'
    // diagnostic message: "AlignedBuffer is move-only"
}

}  // namespace

int main() { return 0; }
