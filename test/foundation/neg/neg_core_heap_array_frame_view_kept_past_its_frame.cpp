// The body of the frame door with_mut of a HeapArray keeps the View of its
// ViewMut in a variable that it captures by reference.  The door gives the
// array back, and the array then ends, so a read through the kept View reads
// freed storage.  The store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) refuses the body.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    ::foundation::core::HeapArray<int> array =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, 0);
    ::foundation::core::HeapArray<int> back =
        ::foundation::core::with_mut(std::move(array), [&kept](auto run) noexcept {
            kept = ::foundation::core::Option<::foundation::core::View<int const>>::some(run.view());
        });
    return back.size() == 4 ? 0 : 1;
}
