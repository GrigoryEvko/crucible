// Tier 1, the array shape.  A binding holds its payload by value, and an
// array is not copyable or returnable as a value: it decays.  Tier 1
// refuses it rather than decaying it, because decaying would make the
// binding describe a pointer while the caller wrote an array.
//
// fixy::FixedArray is the payload to name instead, or a std::span over
// storage the binding does not own.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int[4]> refused{};
    return 0;
}
