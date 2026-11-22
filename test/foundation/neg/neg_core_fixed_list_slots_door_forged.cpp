// A unit that is not a FixedList, such as a door of another owner of the base
// or a file of src/, calls the door of the list to make a View of any pointer
// and any count.  FixedList is the one friend of the door, so its member is
// private here.

#include <foundation/core/List.h>

int main() {
    static_cast<void>(::foundation::core::FixedListSlotsDoor::live_slots_(static_cast<int*>(nullptr), std::size_t{4}));
    return 0;
}
