// A Sealed gives its object only as a const object, so no code appends to a
// sealed list while a reader on another thread reads it.

#include <foundation/core/Sealed.h>
#include <foundation/core/SegmentList.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::Sealed const sealed{::foundation::core::SegmentList<int>{}};
    sealed->append(::foundation::effects::testing::test().alloc, 1);
    return static_cast<int>(sealed->size());
}
