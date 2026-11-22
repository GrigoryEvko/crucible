// The caller takes two Views of one HeapArray and writes the same element
// through both.  The two Views would name the same elements, so no proof that
// two runs do not overlap would hold for them.  view() gives a View of the
// const elements, and a write takes with_element or the frame door with_mut.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::HeapArray<int> array =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0);
    ::foundation::core::View<int> const first = array.view(), second = array.view();
    *first.get(0).expect("the array holds four elements") = 1;
    *second.get(0).expect("the array holds four elements") = 2;
    return *array.get(0).expect("the array holds four elements");
}
