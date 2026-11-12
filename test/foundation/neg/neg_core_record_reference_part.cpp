// A reference part would hide a borrow, so the part gate refuses a
// reference type.  A borrow is a Ref or a View.

#include <foundation/core/Record.h>

int main() { return static_cast<int>(sizeof(::foundation::core::Record<int&, int>)); }
