// A body of the element door with_masked of a MaskedArray calls the door of
// the same array again, for a key with the same low bits.  The two bodies
// would hold two references to one element, and the inner door could replace
// the storage that the outer body reads.  The element is an Atomic cell, so
// the door that is not const lends it in place.  The door carries the
// annotation borrow_match, so the rule borrow-match refuses a call of a
// member of the array that is not const in the body.

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Effect.h>

int main() {
    using Cell = ::foundation::core::Atomic<int>;
    ::foundation::core::MaskedArray<Cell> array =
        ::foundation::core::mint_masked_array<Cell>(::foundation::effects::testing::test().alloc, 4);
    array.with_masked(0, [&array](Cell& first) noexcept {
        array.with_masked(4, [&first](Cell& second) noexcept { second.store_release(first.load_acquire() + 1); });
        first.store_release(2);
    });
    return array.at_masked(0).load_acquire();
}
