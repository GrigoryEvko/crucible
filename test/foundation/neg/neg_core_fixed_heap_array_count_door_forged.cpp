// A unit that is not a FixedHeapArray, such as a door of another owner of the
// base or a file of src/, calls the door of the array to make a View of a
// fixed extent of any pointer.  FixedHeapArray is the one friend of the door,
// so its member is private here.

#include <foundation/core/FixedHeapArray.h>

int main() {
    static_cast<void>(::foundation::core::FixedHeapArrayCountDoor::held_run_<4>(static_cast<int*>(nullptr)));
    return 0;
}
