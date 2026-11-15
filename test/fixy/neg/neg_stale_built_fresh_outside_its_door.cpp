// A Stale takes its grade only through a named door.  The constructor from
// a value and a grade is private, so no caller states a fresh grade for a
// value that nothing measured.

#include <fixy/Stale.h>

int main() {
    ::fixy::Stale<int> const claimed{7, ::foundation::algebra::lattices::staleness::at(0)};
    return claimed.peek();
}
