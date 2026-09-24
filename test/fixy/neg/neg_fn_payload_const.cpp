// Tier 1, the cv-qualified shape.  A const payload is refused rather
// than stripped, because stripping would make the grade describe a
// different type from the one the caller wrote: `fn<const int>` and
// `fn<int>` would resolve to one binding while their Type-axis grades
// read differently.
//
// The qualifier belongs on the binding that HOLDS the fn — a
// `const fn<int>` is a constant binding over a mutable payload type —
// not inside it.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<const int> refused{};
    return 0;
}
