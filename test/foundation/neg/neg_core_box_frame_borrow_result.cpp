// The result of the frame door of a Box leaves the frame with the Box.  A
// Ref of the object in the result would outlive the frame, and a later move
// or end of the Box would end the object that the Ref names.  So the door
// refuses a body whose result holds a borrow.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace fc = ::foundation::core;

int main() {
    fc::Box<int> box = fc::mint_box<int>(::foundation::effects::testing::test().alloc, 41);
    auto [kept, back] = fc::with_mut(std::move(box), [](int& object) noexcept { return fc::to(object); });
    static_cast<void>(back);
    return *kept;
}
