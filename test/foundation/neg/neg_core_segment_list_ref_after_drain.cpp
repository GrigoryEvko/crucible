// A Ref of an element is read after a drain of its SegmentList.  The drain ends
// each element and frees the segments, so the read is of freed storage.  The
// list states owns_its_storage{}, and the rule view-escape still reads the drain
// as an end, because the drain moves the list into its frame and ends it there.

#include <foundation/core/Ref.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    ::foundation::core::SegmentList<int> list;
    list.append(::foundation::effects::testing::test().alloc, 3);
    auto const element = list.get(0).expect("the list holds one element");
    std::move(list).drain([](int) noexcept {});
    return *element;
}
