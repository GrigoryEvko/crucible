// A WriteOnceNonNull takes and gives a Ref of its pointee, and no Ref of void
// exists.  So the pointee is an object type, and the mint refuses void*.  A
// foreign handle uses a declared opaque class, not void.

#include <fixy/Mutation.h>

int main() {
    auto slot = fixy::mint_write_once_non_null<void*>();
    return slot.has_value() ? 1 : 0;
}
