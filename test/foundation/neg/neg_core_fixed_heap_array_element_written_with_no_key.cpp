// A body that writes an element of a FixedHeapArray with no key.  Another name
// of the array could lend the same element.  An array with no key lends no
// element that a body writes.

#include <foundation/core/FixedHeapArray.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::FixedHeapArray<int, 4> array =
        ::foundation::core::mint_fixed_heap_array<int, 4>(::foundation::effects::testing::test().alloc, 0);
    array.with_element(::foundation::core::index<0>(), [](int& element) noexcept { element = 1; });
    return array.at(::foundation::core::index<0>());
}
