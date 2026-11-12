// A member access through a temporary CacheLine borrows a cell that ends
// at the end of the full expression.  The rvalue overload of the arrow
// operator is deleted.

#include <foundation/core/Atomic.h>

int main() { return static_cast<int>(::foundation::core::CacheLine<::foundation::core::Tally>{}->read()); }
