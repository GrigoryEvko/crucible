// A function returns the erased Ref of a local.  erase_brand drops the brand
// that would keep the Ref in its scope, and the local ends with the frame of
// the function.  The rule view-escape refuses the return.

#include <foundation/core/Ref.h>

namespace fc = ::foundation::core;

namespace {

auto erased_ref_of_local() {
    int value = 2;
    return fc::erase_brand(fc::to(value));
}

}  // namespace

// The call does not read the result, so an optimized build gives no warning
// about the dead local in main.
int main() {
    static_cast<void>(erased_ref_of_local());
    return 0;
}
