// The Report family writes a float and a double only.  A _Float16 is a
// different type, and a report refuses it at the call.

#include <foundation/core/Report.h>

int main() {
    _Float16 const half = 0.5F16;
    ::foundation::core::fatal("the ratio is {}", half);
}
