// An Option holds an object, not a reference.  A reference payload would
// give a borrow that the Option cannot keep alive, so the payload gate
// refuses it.

#include <foundation/core/Choice.h>

int main() { return ::foundation::core::Option<int&>{}.is_some() ? 1 : 0; }
