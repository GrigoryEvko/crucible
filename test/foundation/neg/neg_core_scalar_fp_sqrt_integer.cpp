// fp::sqrt takes a Float: the square root of an integer does not compile,
// so no promotion to double hides a conversion.

#include <foundation/core/Float.h>

int main() { return ::foundation::core::fp::sqrt(4) == 2 ? 0 : 1; }
