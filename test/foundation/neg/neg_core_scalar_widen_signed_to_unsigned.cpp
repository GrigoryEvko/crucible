// widen has no check, so it takes only a pair whose values all fit.  A
// signed value does not fit an unsigned type: -1 is not an unsigned value.

#include <foundation/core/Convert.h>

int main() { return ::foundation::core::widen<unsigned>(-1) == 0u ? 0 : 1; }
