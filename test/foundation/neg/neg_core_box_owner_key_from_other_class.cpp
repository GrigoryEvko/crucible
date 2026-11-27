// Only a member of the owner builds the key of the owner door of a Box.
// Code outside the owner builds no key, so it reads no object through the
// door.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

struct Holder {};

int main() {
    ::foundation::core::Box<int> const box =
        ::foundation::core::mint_box<int>(::foundation::effects::testing::test().alloc, 1);
    return ::foundation::core::borrow_owned(::foundation::core::box_owner_key<Holder>{}, box);
}
