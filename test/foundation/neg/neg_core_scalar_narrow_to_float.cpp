// narrow gives an integer.  IEEE 754 defines each conversion of an integer
// to a float, so it stays a static_cast, and narrow to a float does not
// compile.

#include <foundation/core/Convert.h>

int main() { return ::foundation::core::narrow<double>(7).is_some() ? 0 : 1; }
