// A Ref from get of a temporary FixedArray would dangle at the end of the
// full expression, so get of an rvalue is refused.

#include <foundation/core/Array.h>

int main() {
    auto element = ::foundation::core::FixedArray<int, 4>{}.get(0);
    return element.is_some() ? 1 : 0;
}
