// A Bounds comes only from of() and bounds<Lo, Hi>(), which prove the order
// of the bounds.  The constructor is private, so a caller cannot make a
// range with lo > hi.

#include <foundation/core/Compare.h>

int main() {
    ::foundation::core::Bounds<int> const range{10, 0};
    return range.lo();
}
