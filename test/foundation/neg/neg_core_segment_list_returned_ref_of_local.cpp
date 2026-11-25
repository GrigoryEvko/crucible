// A function returns a Ref of an element of a local SegmentList.  The list ends
// with the frame of the function and frees the element, so a read of the result
// reads freed storage.  The rule view-escape refuses the return.

#include <foundation/core/Ref.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

namespace {

auto ref_of_local_list() {
    ::foundation::core::SegmentList<int> list;
    list.append(::foundation::effects::testing::test().alloc, 3);
    return list.get(0).expect("the list holds one element");
}

}  // namespace

int main() { return *ref_of_local_list() == 3 ? 0 : 1; }
