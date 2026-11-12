// A type two times in a OneOf would let a value select two alternatives, so
// the gate refuses it.

#include <foundation/core/OneOf.h>

int main() { return sizeof(::foundation::core::OneOf<int, long, int>) > 0 ? 0 : 1; }
