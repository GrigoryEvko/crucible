// The body of the read frame of a Box holds a const reference to the object.
// A consume of the Box in the body would free the object while the reference
// reads it.  The door carries the annotation borrow_match, so the rule
// borrow-match refuses the move of the Box in the body.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace fc = ::foundation::core;

int main() {
    fc::Box<int> box = fc::mint_box<int>(::foundation::effects::testing::test().alloc, 41);
    int const sum = fc::with_view(box, [&box](int const& object) noexcept {
        int const before = object;
        int const taken = std::move(box).consume();
        return before + taken;
    });
    return sum == 82 ? 0 : 1;
}
