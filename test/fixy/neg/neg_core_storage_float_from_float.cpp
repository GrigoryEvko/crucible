// No float converts to a storage type without a call that names its
// rounding.

#include <fixy/Core.h>

int main() {
    fixy::fp8_e4m3 const value = 1.5F;
    return value.raw() == 0 ? 1 : 0;
}
