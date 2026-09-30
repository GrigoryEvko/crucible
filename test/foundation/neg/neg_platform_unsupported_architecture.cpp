// The platform floor refuses a target that is not x86_64 or aarch64.  The
// test compiles this file with -U__x86_64__ -U__aarch64__.  The compiler then
// sees a target with no supported architecture macro, and the check in
// foundation/Platform.h stops the build with its #error.

#include <foundation/Platform.h>

int main() { return 0; }
