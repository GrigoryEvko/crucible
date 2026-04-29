// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling AllocClass<WeakerTag, T>::relax<StrongerTag>()
// when StrongerTag > WeakerTag in the AllocClassLattice.
//
// THE LOAD-BEARING REJECTION FOR THE no-malloc-on-hot-path
// DISCIPLINE (CLAUDE.md §VIII).  Without it, a value sourced from
// jemalloc/Heap could be re-typed as Stack/Pool/Arena and silently
// flow into a hot-path admission gate, defeating the per-call
// shape budget — jemalloc ~50-200ns vs Arena bump ~2-3ns.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `AllocClassLattice::leq(WeakerTag, Tag)` to a permissive form
// — would silently allow a Heap-tier value to claim Arena
// compliance.  The dispatcher's hot-path admission gate would
// then admit malloc-bearing code into TraceRing/MetaLog/Vigil
// foreground call sites.
//
// Lattice direction: Stack is at the TOP (cheapest, no allocator
// at all); HugePage is at the BOTTOM (mmap + huge-page setup).
// Going DOWN (Stack → Pool → Arena → Heap → Mmap → HugePage) is
// allowed.  Going UP is FORBIDDEN.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/AllocClass.h>

using namespace crucible::safety;

int main() {
    // Pinned at Heap — bytes derive from jemalloc/malloc.  This is
    // what hot-path admission gates MUST reject; the relax<> below
    // is the bug-introduction path the wrapper fences.
    AllocClass<AllocClassTag_v::Heap, int> heap_value{42};

    // Should FAIL: relax<Arena> on a Heap-pinned wrapper.  The
    // requires-clause `AllocClassLattice::leq(Arena, Heap)` is
    // FALSE — Arena is above Heap in the chain — so the relax<>
    // overload is excluded.  Without this fence, jemalloc-bearing
    // values could claim Arena compliance and silently enter hot-
    // path TraceRing call sites, breaking CLAUDE.md §VIII.
    auto arena_claim = std::move(heap_value).relax<AllocClassTag_v::Arena>();
    return arena_claim.peek();
}
