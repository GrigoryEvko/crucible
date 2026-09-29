// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing an `alloc_class::Heap<T>` band to a function whose
// `requires` clause demands `satisfies_v<Slot, AllocClassTag_v::Pool>`,
// the production hot-path admission gate.
//
// THE LOAD-BEARING REJECTION FOR "no malloc on hot path"
// (CLAUDE.md §VIII).  PoolAllocator::slot_ptr_pinned returns
// alloc_class::Pool<void*>; the hot-path consumer requires Pool tier
// (or stronger).  A heap band coming from jemalloc MUST be rejected at
// the call boundary: its setup cost (50-200 ns) is two orders of
// magnitude above the per-call shape budget.
//
// Lattice direction:
//     HugePage(weakest) ⊑ Mmap ⊑ Heap ⊑ Arena ⊑ Pool ⊑ Stack(strongest)
//
// satisfies_v<B, Required> = leq(Required, tier of B).  For Heap to
// satisfy Pool, leq(Pool, Heap) would have to hold, but Pool is
// STRONGER than Heap, so it is false and the requires clause rejects
// the call.
//
// Concrete bug class this catches: a refactor that introduces a
// "convenience" overload accepting a raw `void*` and re-wrapping it
// internally as Pool, bypassing the tier check.

#include <fixy/Bands.h>

#include <utility>

// Production-like consumer: hot-path slot dereferencer that demands
// Pool tier or stronger.  Models the PoolAllocator::slot_ptr_pinned()
// to consumer pattern.
template <typename Slot>
    requires(::fixy::satisfies_v<Slot, ::fixy::AllocClassTag_v::Pool>)
static void* hot_path_slot_consumer(Slot slot) noexcept {
    return std::move(slot).consume();
}

int main() {
    int storage = 42;

    // Pinned at Heap: the origin is malloc.  The band carries the tier,
    // and the consumer's requires clause sees that Heap does not satisfy
    // Pool and excludes the overload.
    ::fixy::alloc_class::Heap<int*> heap_value{&storage, {}};

    void* result = hot_path_slot_consumer(std::move(heap_value));
    (void)result;
    return 0;
}
