// mint_box_built builds the object of a Box of T from the prvalue that the
// build gives.  A build that gives another type would build an object of
// another type in the storage of a T.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::Box<int> box = ::foundation::core::mint_box_built<int>(
        ::foundation::effects::testing::test().alloc, []() noexcept { return 1L; });
    return *box;
}
