// A function returns the View of a segment of a local SegmentList.  The list
// ends with the frame of the function and frees the segment, so a read of the
// result reads freed storage.  The rule view-escape refuses the return.

#include <foundation/core/Region.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

namespace {

::foundation::core::View<int const> run_of_local_list() {
    ::foundation::core::SegmentList<int> list;
    list.append(::foundation::effects::testing::test().alloc, 3);
    return *list.segments().begin();
}

}  // namespace

int main() { return run_of_local_list().size() == 1 ? 0 : 1; }
