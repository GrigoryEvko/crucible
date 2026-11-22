// A unit that is not a BumpArena, such as a door of another owner of the base
// or a file of src/, calls the door of the arena to make a View of any pointer
// and any count.  BumpArena is the one friend of the door, so its member is
// private here.

#include <foundation/core/BumpArena.h>

int main() {
    static_cast<void>(::foundation::core::ArenaArrayDoor::built_run_(static_cast<int*>(nullptr), std::size_t{4}));
    return 0;
}
