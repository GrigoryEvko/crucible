// The platform floor refuses an x86_64 build in which GCC separates shared
// data by a distance other than 64 bytes.  The test compiles this file with
// --param destructive-interference-size=128.  GCC then defines
// __GCC_DESTRUCTIVE_SIZE as 128, and the check in foundation/Platform.h
// stops the build.

#include <foundation/Platform.h>

int main() { return 0; }
