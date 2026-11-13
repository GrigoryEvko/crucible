// A copy of a GrowText would allocate with no allocation capability at its
// call, so a text from mint_grow_text does not copy.

#include <foundation/core/Grow.h>

int main() {
    auto const text = ::foundation::core::mint_grow_text(16);
    auto const copy = text;
    return static_cast<int>(copy.size());
}
