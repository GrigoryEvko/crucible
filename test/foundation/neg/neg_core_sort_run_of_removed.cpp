// A unit of the base calls the run of the sort with a pointer and a count, to
// make a View of any pointer.  The runs of the sort come only from the door of
// the sort to its friends (SortRunDoor of foundation/core/Sort.h).  The one
// function of that name in detail takes a ViewMut, so no call matches.

#include <foundation/core/Sort.h>

int main() {
    int value = 0;
    static_cast<void>(foundation::core::detail::run_of_(&value, foundation::core::detail::SortCount{2}));
    return 0;
}
