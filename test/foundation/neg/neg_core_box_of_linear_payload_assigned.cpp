// A Box of a linear payload is linear, and it has no move assignment.  The
// assignment ends the payload that the target holds, and a linear payload
// must not end with no consume.  A Hole is linear, so a Box of a Hole is a
// Box of a linear payload.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    using Credit = ::foundation::core::Hole<8, 8>;
    constexpr auto alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::Box<Credit> target =
        ::foundation::core::mint_box<Credit>(alloc, ::foundation::core::mint_hole<8, 8>(alloc));
    ::foundation::core::Box<Credit> source =
        ::foundation::core::mint_box<Credit>(alloc, ::foundation::core::mint_hole<8, 8>(alloc));
    target = std::move(source);
    std::move(target).consume().consume();
    return 0;
}
