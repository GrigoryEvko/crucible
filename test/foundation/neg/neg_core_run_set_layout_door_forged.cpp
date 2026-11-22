// A unit that is not a RunSet, such as a door of another owner of the base or
// a file of src/, calls the door of the set to make a View of any pointer and
// any count.  RunSet is the one friend of the door, so its member is private
// here.

#include <foundation/core/RunSet.h>

int main() {
    static_cast<void>(::foundation::core::RunSetLayoutDoor::held_run_(static_cast<int*>(nullptr), std::size_t{4}));
    return 0;
}
