// A body of the element door with_element of a HeapArray calls the door of
// the same array again, for the same element.  The two bodies would hold two
// references to one element, and each could write it beside the other.  The
// door carries the annotation borrow_match, so the rule borrow-match refuses
// a call of a member of the array that is not const in the body.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::HeapArray<int> array =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0);
    array.with_element(
        0,
        [&array](int& first) noexcept {
            array.with_element(0, [&first](int& second) noexcept { second = first + 1; }, [] noexcept {});
            first = 2;
        },
        [] noexcept {});
    return array.view().size() == 4 ? 0 : 1;
}
