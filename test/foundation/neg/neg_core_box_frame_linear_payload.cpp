// A linear object leaves a Box only through consume.  The body of a frame
// door could move it out through the reference and leave the Box full of a
// moved-from value.  So a Box of a linear payload has no frame door.  A Hole
// is linear, so a Box of a Hole is a Box of a linear payload.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace fc = ::foundation::core;
using Credit = fc::Hole<8, 8>;

int main() {
    constexpr auto alloc = ::foundation::effects::testing::test().alloc;
    fc::Box<Credit> box = fc::mint_box<Credit>(alloc, fc::mint_hole<8, 8>(alloc));
    static_cast<void>(fc::with_mut(std::move(box), [](Credit& credit) noexcept { static_cast<void>(credit); }));
    return 0;
}
