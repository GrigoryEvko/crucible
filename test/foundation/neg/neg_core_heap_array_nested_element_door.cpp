// A body of the element door with_element of a HeapArray calls the door of
// the same array again, for the same element.  The two bodies would hold two
// references to one element, and the inner door could replace the storage
// that the outer body reads.  The element is an Atomic cell, so the door that
// is not const lends it in place.  The door carries the annotation
// borrow_match, so the rule borrow-match refuses a call of a member of the
// array that is not const in the body.

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    using Cell = ::foundation::core::Atomic<int>;
    ::foundation::core::HeapArray<Cell> array = ::foundation::core::mint_heap_array_with<Cell>(
        ::foundation::effects::testing::test().alloc, 4, [](std::size_t) noexcept { return Cell{0}; });
    array.with_element(
        0,
        [&array](Cell& first) noexcept {
            array.with_element(
                0, [&first](Cell& second) noexcept { second.store_release(first.load_acquire() + 1); }, [] noexcept {});
            first.store_release(2);
        },
        [] noexcept {});
    return array.view().size() == 4 ? 0 : 1;
}
