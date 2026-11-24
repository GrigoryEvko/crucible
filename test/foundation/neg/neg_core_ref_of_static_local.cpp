// A unit gives the door of a Ref of static storage a local object.  The Ref
// would carry the brand of static storage and dangle at the end of the
// function.  A local object is no template argument of a reference type, so
// the call does not compile.

#include <foundation/core/Ref.h>

int main() {
    int local = 1;
    return *foundation::core::ref_of_static<local>();
}
