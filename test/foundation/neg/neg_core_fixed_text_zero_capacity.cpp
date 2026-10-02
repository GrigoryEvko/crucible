// A FixedText holds at least one character.  A capacity of zero gives a
// buffer of no bytes, which no format() can fill, so it is refused.

#include <foundation/core/Text.h>

int main() { return sizeof(::foundation::core::FixedText<0>) > 0 ? 0 : 1; }
