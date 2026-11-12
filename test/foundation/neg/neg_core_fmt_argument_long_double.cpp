// A long double has 80 value bits on x86_64 and 128 on aarch64, so one
// value would have two texts.  The Report family writes a float and a
// double only, and a report refuses a long double argument at the call.

#include <foundation/core/Report.h>

int main() { ::foundation::core::fatal("the ratio is {}", 0.5L); }
