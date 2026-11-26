// mint_box_built is noexcept, and it calls the build after it takes the
// storage of the Box.  A build that can throw would leave that storage with
// no owner.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::Box<int> box =
        ::foundation::core::mint_box_built<int>(::foundation::effects::testing::test().alloc, [] { return 1; });
    return *box;
}
