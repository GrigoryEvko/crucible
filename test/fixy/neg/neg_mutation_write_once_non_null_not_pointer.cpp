// The unset state of the slot is the null pointer, which an int does not
// have.  The door refuses a carrier that is not a pointer.

#include <fixy/Mutation.h>

int main() {
    auto slot = fixy::mint_write_once_non_null<int>();
    return slot.has_value() ? 1 : 0;
}
