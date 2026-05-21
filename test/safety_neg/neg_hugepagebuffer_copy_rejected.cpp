// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: invoking `HugePageBuffer<T>(const HugePageBuffer<T>&)`
// — the copy constructor is `= delete("HugePageBuffer is move-only")`
// at HugePageBuffer.h:102.  Copy construction triggers overload-
// resolution failure citing the deleted-function diagnostic with the
// move-only-discipline message verbatim.
//
// Discipline rationale (HugePageBuffer.h:14-21, 102-103):
//   HugePageBuffer<T> is a move-only RAII wrapper over a 2-MB-aligned
//   `aligned_alloc` region with MADV_HUGEPAGE applied.  Duplicating
//   the ownership token would mean two destructors both `std::free`-
//   ing the same pointer — classic double-free + use-after-free
//   landmine.  The deleted copy is the structural guarantee that
//   ownership lives in exactly one HugePageBuffer instance per
//   allocation, transferred via move-construction or move-assignment.
//
//   Production sites: TraceRing's 64MB ring buffer (8 cache lines ×
//   1M entries on hugepages), MetaLog's parallel SPSC backing
//   (~144MB on hugepages), Cipher warm-tier mmap'd shard regions.
//   In every site, the allocation MUST be uniquely owned — a copied
//   HugePageBuffer would alias the hugepage region across two
//   independent destructors, corrupting both readers and writers
//   when the second destructor fires after the first frees.
//
// HS14 — distinct-class fixture pair across move-only RAII siblings:
//   * Class T-copy-ctor-rejected (THIS file, HugePageBuffer):
//     `HugePageBuffer<int> copy = original;` triggers the deleted
//     copy ctor with the wrapper's discipline message in the
//     diagnostic — pins the duplicate-ownership rejection at the
//     construction boundary.
//   * Class T-copy-assign-rejected (sibling neg_swisstablebuffer_
//     copy_assign_rejected): same move-only discipline, different
//     enforcement point — the copy-assignment operator on the twin
//     SwissTableBuffer wrapper.
//
//   Both wrappers share the move-only-RAII-buffer shape (private
//   direct ctor + `allocate()` factory + deleted copy/copy-assign +
//   defaulted move/move-assign + `std::free()`-in-dtor).  The pair
//   pins both halves of the deleted-copy discipline (ctor + assign)
//   across two structurally similar wrappers, mirroring the
//   Pinned-copy + NonMovable-move pair pattern under fixy-U-144.
//
// FIXY-U-153 — first of the HugePageBuffer / SwissTableBuffer pair
// (closes their slice of #146 A8-P2; both wrappers had zero
// fixtures before this ship).

#include <crucible/safety/HugePageBuffer.h>

#include <cstdint>

namespace {

// Anchor: move-construction compiles cleanly — the canonical
// transfer-of-ownership pattern.
[[maybe_unused]] static
::crucible::safety::HugePageBuffer<std::uint64_t>
anchor_move_construct() {
    auto buf = ::crucible::safety::HugePageBuffer<std::uint64_t>::allocate(8);
    return buf;   // NRVO or move — both legal.
}

// VIOLATION: HugePageBuffer<T>(const HugePageBuffer<T>&) is
// `= delete("HugePageBuffer is move-only")`.  Direct copy
// construction triggers the deleted-function diagnostic.
[[maybe_unused]] static
::crucible::safety::HugePageBuffer<std::uint64_t>
offending_copy_construct(
    const ::crucible::safety::HugePageBuffer<std::uint64_t>& source)
{
    return ::crucible::safety::HugePageBuffer<std::uint64_t>{source};
    // ERROR: use of deleted function 'HugePageBuffer<T>::HugePageBuffer(const HugePageBuffer<T>&)'
    // diagnostic message: "HugePageBuffer is move-only"
}

}  // namespace

int main() { return 0; }
