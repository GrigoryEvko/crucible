// The make of append_with appends to the list that it builds an element for.
// The two appends would take one slot.  append_with carries the annotation
// borrow_match, and the rule borrow-match refuses a call of a member of the
// list that is not const in make.

#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::SegmentList<int> list;
    list.append_with(alloc, [&](std::size_t) noexcept {
        list.append(alloc, 2);
        return 1;
    });
    return static_cast<int>(list.size());
}
