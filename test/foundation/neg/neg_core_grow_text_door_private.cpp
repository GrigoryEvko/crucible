// A unit of the base that is not a HeapText or a GrowText calls the door of
// the two texts to make a TextView of any pointer and any count.  The two
// texts are the friends of the door, so its member is private here.

#include <foundation/core/Grow.h>

int main() {
    foundation::core::TextView const text = foundation::core::GrowTextDoor::text_(static_cast<char const*>(nullptr), 4);
    return static_cast<int>(text.size());
}
