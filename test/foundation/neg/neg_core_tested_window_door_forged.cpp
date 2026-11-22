// A unit that is not a tested window, such as a door of another owner of the
// base or a file of src/, calls the door of a tested window to start objects
// over any bytes and make their View with no test.  The tested window and the
// door of the list of windows are the friends of the door, so its member is
// private here.

#include <foundation/AlignedBytes.h>

int main() {
    static_cast<void>(
        ::foundation::core::TestedWindowDoor::started_run_<int>(static_cast<std::uint8_t*>(nullptr), std::size_t{4}));
    return 0;
}
