// A unit that is not a Reservation, such as a door of another owner of the
// base or a file of src/, calls the door of a committed range to make a View
// of any bytes.  Reservation is the one friend of the door, so its member is
// private here.

#include <foundation/os/PageSource.h>

int main() {
    static_cast<void>(
        ::foundation::core::CommittedRangeDoor::committed_bytes_(static_cast<std::uint8_t*>(nullptr), std::size_t{4}));
    return 0;
}
