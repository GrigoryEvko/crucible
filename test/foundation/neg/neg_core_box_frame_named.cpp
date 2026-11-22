// The frame door of a Box takes the Box into its frame.  A named Box would
// stay with the caller, and the body could end its object through a capture
// while it holds the reference.  So the door refuses a named Box.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

namespace fc = ::foundation::core;

int main() {
    fc::Box<int> box = fc::mint_box<int>(::foundation::effects::testing::test().alloc, 41);
    box = fc::with_mut(box, [](int& object) noexcept { object += 1; });
    return 0;
}
