// A function returns the Ref of a local, with the fresh brand of to() in its
// deduced return type.  The local ends with the frame of the function, so a
// read of the result reads a dead stack frame.  The rule view-escape refuses
// the return.

#include <foundation/core/Ref.h>

namespace fc = ::foundation::core;

namespace {

auto ref_of_local() {
    int value = 1;
    return fc::to(value);
}

}  // namespace

// The call does not read the result, so an optimized build gives no warning
// about the dead local in main.
int main() {
    static_cast<void>(ref_of_local());
    return 0;
}
