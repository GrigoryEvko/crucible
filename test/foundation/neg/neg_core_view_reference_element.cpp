// A View borrows a run of objects.  A reference is not an object and has
// no address of its own, so the element gate refuses it.

#include <foundation/core/Region.h>

int main() { return static_cast<int>(::foundation::core::View<int&>{}.size()); }
