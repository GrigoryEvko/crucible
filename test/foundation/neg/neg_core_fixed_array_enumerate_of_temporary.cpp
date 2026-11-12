// The Refs of an enumerate loop over a temporary FixedArray would dangle at
// the end of the full expression, so enumerate() of an rvalue is refused.

#include <foundation/core/Array.h>

int main() {
    auto pairs = ::foundation::core::FixedArray<int, 4>{}.enumerate();
    return pairs.begin() == pairs.end() ? 1 : 0;
}
