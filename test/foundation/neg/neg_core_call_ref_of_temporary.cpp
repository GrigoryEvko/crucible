// Negative-compile test. This file must fail to compile.
//
// A CallRef borrows its callable.  A lambda that is a temporary ends at the
// end of the full expression, so a CallRef of it would call an object that
// ended.  The constructor from an rvalue is deleted.
//
// Expected diagnostic: use of the deleted constructor of CallRef, with its
// reason.

#include <foundation/core/CallRef.h>

int main() {
    ::foundation::core::CallRef<int() noexcept> const call_ref{[]() noexcept { return 1; }};
    return call_ref();
}
