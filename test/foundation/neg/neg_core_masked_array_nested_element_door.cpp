// A body of the element door with_masked of a MaskedArray calls the door of
// the same array again, for a key with the same low bits.  The two bodies
// would hold two references to one element, and each could write it beside
// the other.  The door carries the annotation borrow_match, so the rule
// borrow-match refuses a call of a member of the array that is not const in
// the body.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::MaskedArray<int> array =
        ::foundation::core::mint_masked_array<int>(::foundation::effects::testing::test().alloc, 4);
    array.with_masked(0, [&array](int& first) noexcept {
        array.with_masked(4, [&first](int& second) noexcept { second = first + 1; });
        first = 2;
    });
    return array.at_masked(0);
}
