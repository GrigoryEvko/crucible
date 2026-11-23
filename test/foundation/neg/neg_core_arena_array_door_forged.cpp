// A unit that is not an arena, such as a door of another owner of the base or
// a file of src/, calls the door of the arena to make a run of any pointer
// and any count.  The core of the arena is the one friend of the door, so its
// member is private here.

#include <foundation/core/BumpArena.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

int main() {
    static_cast<void>(
        ::foundation::core::ArenaArrayDoor::built_run_<int, Family>(static_cast<int*>(nullptr), std::size_t{4}));
    return 0;
}
