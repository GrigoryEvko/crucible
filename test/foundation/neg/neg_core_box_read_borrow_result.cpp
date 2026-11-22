// The result of the read frame of a Box outlives the call.  A Ref of the
// object in the result would outlive a later move or end of the Box, and it
// would then name an object that ended.  So the door refuses a body whose
// result holds a borrow.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

namespace fc = ::foundation::core;

int main() {
    fc::Box<int> const box = fc::mint_box<int>(::foundation::effects::testing::test().alloc, 41);
    auto kept = fc::with_view(box, [](int const& object) noexcept { return fc::to(object); });
    return *kept;
}
