// Tier 1, the reference shape.  A reference is not an object type, so
// there is nothing for the binding to hold, and a binding over one would
// describe the referent's discipline while owning none of it.
//
// The payload to name instead is the referent, or fixy::Borrowed for a
// borrow whose lifetime the type system tracks.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int&> refused{};
    return 0;
}
