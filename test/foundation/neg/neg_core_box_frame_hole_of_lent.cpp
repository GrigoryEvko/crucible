// The body of the frame door of a Box holds a reference to the object.  A
// Hole of the Box would end that object and keep its storage, and a Box of a
// second type in the Hole would then put a float where the reference reads
// an int.  The door carries the annotation borrow_match, so the rule
// borrow-match refuses the move of the Box in the body.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace fc = ::foundation::core;

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::Box<int> box = fc::mint_box<int>(alloc, 41);
    auto [seen, back] = fc::with_mut(std::move(box), [&box](int& object) noexcept {
        fc::Box<float> other = fc::mint_box<float>(fc::mint_hole(std::move(box)), 2.5f);
        static_cast<void>(other);
        return object;
    });
    static_cast<void>(back);
    return seen;
}
