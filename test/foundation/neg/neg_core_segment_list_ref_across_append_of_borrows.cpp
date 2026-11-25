// A Ref of an element of a SegmentList of Views is read after an append.  The
// element holds a borrow, so the list does not state owns_its_storage{}, and the
// rule view-escape reads the append as an end of each borrow of an element.
// test_core_segment_list.cpp reads a Ref across the appends of a list whose
// element holds no borrow, which states the mark.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::FixedArray<int, 2> const items{1, 2};
    ::foundation::core::SegmentList<::foundation::core::View<int const>> list;
    list.append(::foundation::effects::testing::test().alloc, items.view());
    auto const element = list.get(0).expect("the list holds one element");
    list.append(::foundation::effects::testing::test().alloc, items.view());
    return static_cast<int>(element->size());
}
