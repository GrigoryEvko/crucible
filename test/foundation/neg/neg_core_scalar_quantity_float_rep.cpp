// The number of a Quantity is an Integer: a Quantity of a double does not
// compile.

#include <foundation/core/Quantity.h>

int main() { return ::foundation::core::Quantity<::foundation::core::unit::Byte, double>{2.0}.raw() > 1.0 ? 0 : 1; }
