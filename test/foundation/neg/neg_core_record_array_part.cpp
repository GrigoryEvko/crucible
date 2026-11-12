// A C array part is a FixedArray, so the part gate refuses an array type.

#include <foundation/core/Record.h>

int main() { return static_cast<int>(sizeof(::foundation::core::Record<int[4]>)); }
