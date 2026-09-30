// The platform floor refuses an Apple aarch64 target, because its cores have
// 128-byte cache lines.  The test compiles this file with -U__x86_64__
// -D__aarch64__ -D__APPLE__.  The compiler then sees the two macros of an
// Apple aarch64 target, and the check in foundation/Platform.h stops the
// build with its #error.

#include <foundation/Platform.h>

int main() { return 0; }
