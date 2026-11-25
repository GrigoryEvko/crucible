// append moves its value into the slot of the list, and the address of a pinned
// element is its identity.  So append refuses the element, and append_with
// builds it in place.

#include <foundation/core/Atomic.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::SegmentList<::foundation::core::Atomic<int>> cells;
    ::foundation::core::Atomic<int> const cell{1};
    cells.append(::foundation::effects::testing::test().alloc, cell);
    return static_cast<int>(cells.size());
}
