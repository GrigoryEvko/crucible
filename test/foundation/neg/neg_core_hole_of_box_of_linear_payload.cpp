// The mint of a Hole from a Box ends the object of the Box.  The end of a
// linear object drops it, so a Box of a linear payload gives no Hole.  A
// Hole is linear, so a Box of a Hole is a Box of a linear payload.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    using Credit = ::foundation::core::Hole<8, 8>;
    constexpr auto alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::Box<Credit> box =
        ::foundation::core::mint_box<Credit>(alloc, ::foundation::core::mint_hole<8, 8>(alloc));
    auto storage = ::foundation::core::mint_hole(std::move(box));
    std::move(storage).consume();
    return 0;
}
