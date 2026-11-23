// A unit of the base that is not a SlotMap calls the door of a SlotMap to the
// door of a Ref, and makes a Ref of a brand that it names to an object of its
// own.  SlotMap is the one friend of the door, so its member is private here.

#include <foundation/core/SlotMap.h>

struct ArenaBrand {};

int main() {
    int target = 1;
    return *foundation::core::SlotMapRefDoor::lend_<ArenaBrand>(target);
}
