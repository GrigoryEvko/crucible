// A Box owns one object.  An array has a length that the Box does not
// keep, so the payload gate refuses an array type.

#include <foundation/core/Ref.h>

int main() { return static_cast<int>(sizeof(::foundation::core::Box<int[4]>)); }
