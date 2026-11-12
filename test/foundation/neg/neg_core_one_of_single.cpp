// A OneOf of one alternative is a value of that type, so the gate asks for
// two alternatives at least.

#include <foundation/core/OneOf.h>

int main() { return sizeof(::foundation::core::OneOf<int>) > 0 ? 0 : 1; }
