// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing an `AllocClass<Heap, T>` value to a function
// whose `requires` clause demands `AllocClass<Pool>::satisfies<...>`
// — the production hot-path admission gate.
//
// THE LOAD-BEARING REJECTION FOR "no malloc on hot path"
// (CLAUDE.md §VIII).  PoolAllocator::slot_ptr_pinned returns
// AllocClass<Pool, void*>; the hot-path consumer requires Pool tier
// (or stronger).  An AllocClass<Heap, void*> coming from jemalloc
// MUST be rejected at the call boundary — its setup cost (50-200 ns)
// is two orders of magnitude above the per-call shape budget.
//
// Lattice direction:
//     HugePage(weakest) ⊑ Mmap ⊑ Heap ⊑ Arena ⊑ Pool ⊑ Stack(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Heap to satisfy
// Pool, we'd need leq(Pool, Heap) — but Pool is STRONGER than Heap,
// so leq(Pool, Heap) is FALSE.  The requires-clause rejects the
// call.
//
// Concrete bug-class this catches: a refactor that introduces a
// "convenience" overload accepting `void*` raw and re-wrapping
// internally as Pool — bypassing the type-system's tier check.
// Without this fixture, the regression of the Pool fence would
// silently admit jemalloc allocations into TraceRing/MetaLog/Vigil
// slot dereferences, breaking the per-call budget.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/AllocClass.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: hot-path slot dereferencer that demands
// Pool tier or stronger.  Models PoolAllocator::slot_ptr_pinned()
// → consumer pattern.
template <typename Slot>
    requires (Slot::template satisfies<AllocClassTag_v::Pool>)
static void* hot_path_slot_consumer(Slot slot) noexcept {
    return std::move(slot).consume();
}

int main() {
    int storage = 42;

    // Pinned at Heap — origin is malloc.  This is what hot-path
    // admission gates MUST reject.  The wrapper carries the tier;
    // the consumer's requires clause sees Heap doesn't satisfy
    // Pool (Pool ⊐ Heap in the lattice) and excludes the overload.
    AllocClass<AllocClassTag_v::Heap, int*> heap_value{&storage};

    // Should FAIL: hot_path_slot_consumer requires Pool-or-stronger;
    // heap_value carries Heap, which is STRICTLY WEAKER than Pool.
    // Without the requires-clause fence, jemalloc allocations would
    // silently flow into hot-path slot dereferences.
    void* result = hot_path_slot_consumer(std::move(heap_value));
    (void)result;
    return 0;
}
