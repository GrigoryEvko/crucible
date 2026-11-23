// A unit of the base that is no friend of the door of a Ref calls it, and
// makes a Ref of a brand that it names, such as the brand of an arena, to an
// object of its own.  The friends of the door are the doors of Ref.h and the
// door classes of the owners above it, so its member is private here.

#include <foundation/core/Ref.h>

struct ArenaBrand {};

int main() {
    int target = 1;
    return *foundation::core::detail::RefDoor::lend<ArenaBrand>(target);
}
