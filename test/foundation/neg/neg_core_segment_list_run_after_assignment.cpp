// A View of a segment is read after an assignment of its SegmentList.  The
// assignment ends each element of the list and frees the segments, so the read
// is of freed storage.  The rule view-escape refuses it.

#include <foundation/core/Region.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::SegmentList<int> list;
    list.append(::foundation::effects::testing::test().alloc, 3);
    ::foundation::core::View<int const> const run = *list.segments().begin();
    list = ::foundation::core::SegmentList<int>{};
    return static_cast<int>(run.size());
}
