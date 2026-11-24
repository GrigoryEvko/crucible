// WriteOnceNonNull reads a null pointer as the unset state, so a set of
// null would store a value that the slot can never report back.  The set
// takes a Ref, and no Ref holds a null pointer, so no set of null compiles.

#include <fixy/Mutation.h>

int main() {
    fixy::WriteOnceNonNull<int*> slot = fixy::mint_write_once_non_null<int*>();
    static_cast<void>(slot.set(nullptr));
    return 0;
}
