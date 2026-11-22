// A file of src/ calls the checked door from raw parts to a View, to make a
// View of a pointer that it holds.  The base has no such door: a pointer and
// a count prove no run, and only the mint of a foreign run checks raw parts,
// at the edge of a foreign ABI.  So the name of the door is not found.

#include <foundation/core/Region.h>

int main() {
    int value = 0;
    static_cast<void>(foundation::core::detail::view_over_(&value, std::size_t{1}));
    return 0;
}
