// A View of a dynamic extent has no indices(), because no type holds its
// count, so it has no enumerate() either.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> const values{1, 2, 3, 4};
    ::foundation::core::View<int const> const elements = values.view();
    int total = 0;
    for (auto const [position, element] : elements.enumerate()) {
        total += *element + static_cast<int>(position.raw());
    }
    return total;
}
