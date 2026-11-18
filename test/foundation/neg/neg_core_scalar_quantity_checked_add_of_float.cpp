// The integer set takes an Integer Rep only: checked_add of two quantities of
// a double does not compile.  A float quantity has the float set: add.

#include <foundation/core/Quantity.h>

int main() {
    namespace core = ::foundation::core;
    core::Quantity<core::unit::Byte, double> const bytes{2.0};
    return core::checked_add(bytes, bytes).is_some() ? 0 : 1;
}
