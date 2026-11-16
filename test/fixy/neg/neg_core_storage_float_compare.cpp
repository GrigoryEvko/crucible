// A storage value has no compare.  A compare of two values goes through a
// widen, and a compare of two words goes through raw().

#include <fixy/Core.h>

int main() {
    fixy::f16 const left = fixy::f16::from_bits(0x3C00);
    fixy::f16 const right = fixy::f16::from_bits(0x3C00);
    return left == right ? 0 : 1;
}
