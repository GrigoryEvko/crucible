// A unit of the base that is not a SegmentList calls the door of a SegmentList
// to the door of a Ref, and makes a Ref of a brand that it names to an object
// of its own.  SegmentList is the one friend of the door, so its member is
// private here.

#include <foundation/core/SegmentList.h>

struct ArenaBrand {};

int main() {
    int target = 1;
    return *foundation::core::SegmentRefDoor::lend_<ArenaBrand>(target);
}
