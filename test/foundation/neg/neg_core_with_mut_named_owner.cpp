// A frame door of a heap owner gets a named owner.  The caller would keep the
// name while the body runs, so the body could take a second borrow of the
// owner through a capture beside its ViewMut.  The door takes the owner only
// by move.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::HeapArray<int> array = ::foundation::core::mint_heap_array(alloc, 4, 1);
    static_cast<void>(::foundation::core::with_mut(array, [](auto out) noexcept { static_cast<void>(out.size()); }));
    return 0;
}
