// An alternative is an object type, so a reference alternative is refused:
// a OneOf would hold a reference that can outlive its object.

#include <foundation/core/OneOf.h>

int main() { return sizeof(::foundation::core::OneOf<int&, long>) > 0 ? 0 : 1; }
