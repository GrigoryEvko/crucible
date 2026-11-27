// A Box gives no Ref of its object.  A Ref that the caller keeps would
// outlive a move assignment to the Box, which ends the object that the Ref
// names.  The frame door with_mut and the read frame with_view lend the
// object for the time of a call.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::Box<int> box =
        ::foundation::core::mint_box<int>(::foundation::effects::testing::test().alloc, 1);
    auto const kept = box.view();
    box = ::foundation::core::mint_box<int>(::foundation::effects::testing::test().alloc, 2);
    return *kept;
}
