// A default-built binding that states a right.
//
// The Usage grade atom::copy gives the binding the right to copy its
// value.  A default constructor would grant that right over int{}, a value
// that no mint admitted under the pack.  So a pack that names an atom has
// no default constructor, on an axis of a right as on an axis of a fact.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::copy> unchecked{};
    return 0;
}
