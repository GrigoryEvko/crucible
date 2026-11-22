// A body of the frame door with_mut of a HeapArray opens the door of the same
// array again.  The two bodies would hold two ViewMuts of one run, and each
// could write it beside the other.  The door takes the array by move, so the
// caller holds no name for the array while the body runs, and the twin of a
// named array refuses the inner door.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    ::foundation::core::HeapArray<int> array =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0);
    array = ::foundation::core::with_mut(std::move(array), [&array](auto outer) noexcept {
        array = ::foundation::core::with_mut(array, [](auto inner) noexcept { static_cast<void>(inner.size()); });
        static_cast<void>(outer.size());
    });
    return static_cast<int>(array.size());
}
