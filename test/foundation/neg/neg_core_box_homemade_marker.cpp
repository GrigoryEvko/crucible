// A marker of a test file with the member grants_allocation calls mint_box.
// The gate of the mint takes the allocation capability Alloc and no other
// type, so the marker does not pass, and each allocation shows the word
// Alloc at its call site.

#include <foundation/core/Ref.h>

struct HomemadeMarker {
    using grants_allocation = void;
};

int main() {
    foundation::core::Box<int> const box = foundation::core::mint_box<int>(HomemadeMarker{}, 7);
    return *box;
}
