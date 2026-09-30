// The platform floor refuses a target core with a cache line longer than 64
// bytes.  The test compiles this file with --param l1-cache-line-size=128 and
// --param constructive-interference-size=128, the two values that GCC sets
// for a core with 128-byte lines.  GCC then defines __GCC_CONSTRUCTIVE_SIZE
// as 128, and the check in foundation/Platform.h stops the build.

#include <foundation/Platform.h>

int main() { return 0; }
