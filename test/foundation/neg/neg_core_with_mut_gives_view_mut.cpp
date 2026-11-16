// A body of a frame door gives its ViewMut out.  The door gives the owner
// back after the body, so the ViewMut would name the storage of an owner
// that the caller holds again.  The result of a frame door holds no borrow.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    ::foundation::core::HeapArray<int> array =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0);
    auto [kept, back] = ::foundation::core::with_mut(std::move(array), [](auto out) noexcept { return out; });
    static_cast<void>(kept);
    static_cast<void>(back);
    return 0;
}
