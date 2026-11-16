// The target namespace refuses a set of vector extensions that skips a step
// of its ladder.  The test compiles this file with -mavx512f -U__AVX2__.  The
// compiler then sees AVX-512F with no AVX2, and the check in
// foundation/Platform.h stops the build with its #error.  Such a set has no
// name, so two bodies could share the name of the step below.

#include <foundation/Platform.h>

int main() { return 0; }
